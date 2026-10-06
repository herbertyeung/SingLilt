# Local Demucs inference and separated-stem output.
# Copyright (c) 2026 Herbert Yeung
# Author: Herbert Yeung
# SPDX-License-Identifier: MIT

"""Local HTDemucs worker. Stdout is UTF-8 NDJSON; inference never downloads files."""

from __future__ import annotations

import argparse
import contextlib
import hashlib
import importlib.metadata
import json
import math
import os
from pathlib import Path
import random
import sys
import time
import uuid


MODEL_NAME = "htdemucs"
MODEL_SIGNATURE = "955717e8"
MODEL_FILE = "955717e8-8726e21a.th"
MODEL_SHA256 = "8726e21a993978c7ba086d3872e7608d7d5bfca646ca4aca459ffda844faa8b4"
MODEL_VERSION = "demucs-infer-4.2.2"
WORKER_VERSION = 1
SAMPLE_RATE = 44100
PROTOCOL = sys.stdout


def emit(kind: str, **fields) -> None:
    PROTOCOL.write(json.dumps({"type": kind, **fields}, ensure_ascii=False, allow_nan=False) + "\n")
    PROTOCOL.flush()


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as source:
        for block in iter(lambda: source.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def model_metadata(model_dir: Path) -> dict:
    record = json.loads((model_dir / "model.json").read_text(encoding="utf-8-sig"))
    expected = {"schema": 1, "name": MODEL_NAME, "signature": MODEL_SIGNATURE,
                "file": MODEL_FILE, "version": MODEL_VERSION}
    if any(record.get(key) != value for key, value in expected.items()):
        raise RuntimeError("The installed separation model metadata is incompatible.")
    checksum = record.get("sha256", "")
    if checksum != MODEL_SHA256:
        raise RuntimeError("The installed checkpoint checksum is invalid.")
    checkpoint = model_dir / MODEL_FILE
    if not checkpoint.is_file() or sha256(checkpoint) != checksum:
        raise RuntimeError("The installed checkpoint is missing or has changed; rerun setup-separation.ps1.")
    return record


def cached_manifest(path: Path, identity: dict, soundfile) -> dict | None:
    if not path.is_file():
        return None
    try:
        manifest = json.loads(path.read_text(encoding="utf-8"))
        if not manifest.get("complete") or any(manifest.get(key) != value for key, value in identity.items()):
            return None
        expected_frames = manifest.get("inputFrames", 0)
        if not isinstance(expected_frames, int) or expected_frames <= 0:
            return None
        for stem in ("vocals", "instrumental"):
            output = path.parent / f"{stem}.wav"
            if manifest.get(stem) != str(output.resolve()) or not output.is_file():
                return None
            info = soundfile.info(str(output))
            if (info.frames != expected_frames or info.samplerate != SAMPLE_RATE or info.channels != 2
                    or info.format != "WAV" or info.subtype != "FLOAT" or manifest.get(f"{stem}Frames") != expected_frames
                    or sha256(output) != manifest.get(f"{stem}SHA256")):
                return None
        if manifest.get("outputFrames") != expected_frames or manifest.get("offset0") != 0:
            return None
        return manifest
    except (OSError, ValueError, RuntimeError):
        return None


def run(args: argparse.Namespace) -> None:
    started = time.perf_counter()
    source = args.input.resolve(strict=True)
    output_dir = args.output_dir.resolve()
    model_dir = args.model_dir.resolve(strict=True)
    if args.device != "cpu" or not 1 <= args.threads <= 32:
        raise ValueError("This runtime supports CPU inference with 1..32 threads.")
    if not math.isfinite(args.start) or args.start < 0 or (args.seconds is not None
            and (not math.isfinite(args.seconds) or args.seconds <= 0)):
        raise ValueError("The experiment crop must have a finite, positive duration.")
    if source in (output_dir / "vocals.wav", output_dir / "instrumental.wav", output_dir / "manifest.json"):
        raise ValueError("Separation outputs must not overwrite the original audio.")
    emit("progress", percent=1, stage="verify_model")
    record = model_metadata(model_dir)

    import numpy as np
    import soundfile as sf
    import torch
    from demucs_infer.apply import apply_model
    from demucs_infer.pretrained import get_model

    if (importlib.metadata.version("demucs-infer") != "4.2.2" or torch.__version__ != "2.4.1+cpu"
            or np.__version__ != "1.26.4" or sf.__version__ != "0.13.1"):
        raise RuntimeError("The separation runtime does not match its pinned package versions.")
    torch.set_num_threads(args.threads)
    torch.set_num_interop_threads(1)
    torch.manual_seed(0)
    random.seed(0)
    np.random.seed(0)
    torch.use_deterministic_algorithms(True)
    source_hash = sha256(source)
    info = sf.info(str(source))
    if info.frames <= 0 or info.channels not in (1, 2) or not 8000 <= info.samplerate <= 192000:
        raise ValueError("Audio must contain mono or stereo samples at a supported rate.")
    if info.duration > 1200:
        raise ValueError("Separation accepts at most 20 minutes per source.")
    first_frame = int(round(args.start * info.samplerate))
    requested_frames = -1 if args.seconds is None else int(round(args.seconds * info.samplerate))
    decoded_frames = max(0, int(info.frames) - first_frame)
    if requested_frames >= 0:
        decoded_frames = min(decoded_frames, requested_frames)
    if decoded_frames <= 0:
        raise ValueError("The experiment crop contains no audio frames.")
    expected_frames = (decoded_frames * SAMPLE_RATE + info.samplerate - 1) // info.samplerate
    identity = {"schema": 1, "workerVersion": WORKER_VERSION, "source": str(source),
                "inputSHA256": source_hash, "model": MODEL_NAME, "modelName": MODEL_NAME,
                "modelSignature": MODEL_SIGNATURE, "modelVersion": MODEL_VERSION,
                "modelSHA256": record["sha256"], "sampleRate": SAMPLE_RATE, "channels": 2,
                "startSeconds": args.start, "requestedSeconds": args.seconds,
                "fullInput": args.start == 0 and args.seconds is None,
                "torchVersion": torch.__version__, "numpyVersion": np.__version__,
                "soundfileVersion": sf.__version__, "inputFrames": expected_frames,
                "sourceAudioFrames": int(info.frames), "sourceAudioSampleRate": int(info.samplerate),
                "sourceAudioChannels": int(info.channels),
                "configuration": {"shifts": 0, "overlap": 0.25, "segment": "model-default-7.8",
                                  "device": "cpu", "threads": args.threads,
                                  "instrumentalMethod": "mixture-minus-neural-vocals"}}
    output_dir.mkdir(parents=True, exist_ok=True)
    manifest_path = output_dir / "manifest.json"
    cached = cached_manifest(manifest_path, identity, sf)
    if cached is not None:
        emit("progress", percent=100, stage="cache_verified")
        emit("result", manifest=str(manifest_path), path=str(manifest_path), cacheHit=True)
        return
    manifest_path.unlink(missing_ok=True)
    emit("progress", percent=5, stage="decode_original")
    audio, decoded_rate = sf.read(str(source), dtype="float32", always_2d=True,
                                 start=first_frame, frames=requested_frames)
    if len(audio) == 0 or not np.isfinite(audio).all():
        raise ValueError("The decoded audio is empty or contains non-finite samples.")
    if audio.shape[1] == 1:
        audio = np.repeat(audio, 2, axis=1)
    mixture = torch.from_numpy(np.ascontiguousarray(audio.T))
    if decoded_rate != SAMPLE_RATE:
        import torchaudio.functional
        mixture = torchaudio.functional.resample(mixture, decoded_rate, SAMPLE_RATE)
    input_frames = int(mixture.shape[1])
    if input_frames != expected_frames:
        raise RuntimeError("The decoded input frame count does not match the source metadata.")
    emit("progress", percent=9, stage="load_neural_model")
    model = get_model(MODEL_SIGNATURE, repo=model_dir)
    if model.samplerate != SAMPLE_RATE or model.audio_channels != 2 or "vocals" not in model.sources:
        raise RuntimeError("The installed model has an unexpected stem layout.")
    segment = float(model.segment)
    if not 0 < segment <= 8:
        raise RuntimeError("The installed model does not have a bounded inference segment.")
    model.eval()
    stride = int((1 - 0.25) * int(SAMPLE_RATE * segment))
    chunks = max(1, (input_frames + stride - 1) // stride)
    completed = 0

    def progress(status: dict) -> None:
        nonlocal completed
        if status["state"] == "end":
            completed += 1
            emit("progress", percent=min(90, 10 + int(80 * completed / chunks)), stage="separate_vocals")

    reference = mixture.mean(dim=0)
    mean = reference.mean()
    scale = reference.std() + 1e-8
    normalized = (mixture - mean) / scale
    with torch.inference_mode():
        # Explicit segment overrides break HTDemucs' training-segment padding in 4.2.2.
        # Its pinned model already uses the bounded 7.8-second default segment.
        stems = apply_model(model, normalized.unsqueeze(0), shifts=0, split=True, overlap=0.25,
                            device="cpu", num_workers=0, progress=False, segment=None, callback=progress)
    vocals_tensor = stems[0, model.sources.index("vocals")] * scale + mean
    if vocals_tensor.shape != mixture.shape or not torch.isfinite(vocals_tensor).all():
        raise RuntimeError("Neural inference returned malformed audio.")
    vocals = vocals_tensor.transpose(0, 1).contiguous().numpy()
    original = mixture.transpose(0, 1).contiguous().numpy()
    # This residual keeps exact reconstruction without independent stem gain changes.
    instrumental = original - vocals
    residual = original.astype(np.float64) - vocals.astype(np.float64) - instrumental.astype(np.float64)
    manifest = {**identity, "complete": True, "inputFrames": input_frames, "outputFrames": input_frames,
                "sourceAudioFrames": int(info.frames), "sourceAudioSampleRate": int(info.samplerate),
                "sourceAudioChannels": int(info.channels), "offset0": 0,
                "reconstructionMaxAbs": float(np.max(np.abs(residual))),
                "reconstructionRms": float(np.sqrt(np.mean(residual * residual))),
                "modelSegmentSeconds": segment, "decodedBy": "soundfile-libsndfile",
                "instrumentalMethod": "mixture-minus-neural-vocals"}
    emit("progress", percent=93, stage="write_aligned_stems")
    temporary_paths = []
    try:
        for name, waveform in (("vocals", vocals), ("instrumental", instrumental)):
            destination = output_dir / f"{name}.wav"
            temporary = output_dir / f".{name}.{uuid.uuid4().hex}.tmp"
            temporary_paths.append(temporary)
            sf.write(str(temporary), waveform, SAMPLE_RATE, format="WAV", subtype="FLOAT")
            written = sf.info(str(temporary))
            if written.frames != input_frames or written.channels != 2 or written.samplerate != SAMPLE_RATE:
                raise RuntimeError("A written stem does not preserve its frame alignment.")
            checksum = sha256(temporary)
            os.replace(temporary, destination)
            manifest[name] = str(destination)
            manifest[f"{name}Frames"] = input_frames
            manifest[f"{name}SHA256"] = checksum
        if sha256(source) != source_hash:
            raise RuntimeError("The original audio changed during separation.")
        manifest["separationSeconds"] = time.perf_counter() - started
        manifest["elapsedSeconds"] = manifest["separationSeconds"]
        temporary = output_dir / f".manifest.{uuid.uuid4().hex}.tmp"
        temporary_paths.append(temporary)
        with temporary.open("w", encoding="utf-8") as output:
            json.dump(manifest, output, ensure_ascii=False, allow_nan=False, indent=2)
            output.flush()
            os.fsync(output.fileno())
        os.replace(temporary, manifest_path)
    finally:
        for temporary in temporary_paths:
            temporary.unlink(missing_ok=True)
    emit("progress", percent=100, stage="complete")
    emit("result", manifest=str(manifest_path), path=str(manifest_path), cacheHit=False)


def main() -> int:
    sys.stdout.reconfigure(encoding="utf-8", errors="strict", line_buffering=True)
    sys.stderr.reconfigure(encoding="utf-8", errors="replace", line_buffering=True)
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--input", type=Path, required=True)
    parser.add_argument("--output-dir", type=Path, required=True)
    parser.add_argument("--model-dir", type=Path, required=True)
    parser.add_argument("--device", default="cpu")
    parser.add_argument("--threads", type=int, default=6)
    parser.add_argument("--start", type=float, default=0, help="Experiment crop only; GUI uses the entire source.")
    parser.add_argument("--seconds", type=float, help="Experiment crop only; GUI uses the entire source.")
    args = parser.parse_args()
    try:
        with contextlib.redirect_stdout(sys.stderr):
            run(args)
        return 0
    except Exception as error:
        emit("error", message=str(error))
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
