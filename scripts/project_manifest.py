#!/usr/bin/env python3
# JPP metadata reading and legacy diagnostic-fixture extraction.
# Copyright (c) 2026 Herbert Yeung
# Author: Herbert Yeung
# SPDX-License-Identifier: MIT

"""Read JPP metadata for diagnostic scripts; optionally materialize a legacy fixture."""
import base64
import hashlib
import json
from pathlib import Path
import struct

MAGIC = b'JPP4\r\n\x1a\n'


def read_project(path, *, legacy_fixture=False):
    path = Path(path)
    with path.open('rb') as stream:
        magic = stream.read(8)
        if magic != MAGIC:
            stream.seek(0)
            return json.load(stream)
        size = struct.unpack('>I', stream.read(4))[0]
        if not 0 < size <= 16 * 1024 * 1024:
            raise ValueError('Invalid JPP manifest size')
        manifest = json.loads(stream.read(size))
        if manifest.get('schemaVersion') != 4:
            raise ValueError('Unsupported JPP version')
        resources = manifest['resources']
        if not 1 <= len(resources) <= 4:
            raise ValueError('Invalid JPP resource count')
        media_folder = path.parent / (path.stem + '-fixture-media')
        if legacy_fixture:
            media_folder.mkdir(exist_ok=True)
        seen = set()
        for resource in resources:
            role, extension = resource['role'], resource['extension']
            if role not in ('image', 'original', 'vocals', 'instrumental') or role in seen:
                raise ValueError('Invalid JPP resource role')
            seen.add(role)
            if extension not in ('png', 'mp3', 'wav'):
                raise ValueError('Invalid JPP resource extension')
            remaining = int(resource['size'])
            if not 0 < remaining <= 2 * 1024**3:
                raise ValueError('Invalid JPP resource size')
            hasher = hashlib.sha256()
            extracted = media_folder / (role + '.' + extension)
            output = extracted.open('wb') if legacy_fixture else None
            try:
                while remaining:
                    block = stream.read(min(remaining, 1024 * 1024))
                    if not block:
                        raise ValueError('Truncated JPP resource')
                    remaining -= len(block)
                    hasher.update(block)
                    if output:
                        output.write(block)
            finally:
                if output:
                    output.close()
            if hasher.hexdigest() != resource['sha256']:
                raise ValueError('JPP checksum mismatch')
            if legacy_fixture:
                if role == 'image':
                    manifest['imagePng'] = base64.b64encode(extracted.read_bytes()).decode('ascii')
                else:
                    field = {'original': 'path', 'vocals': 'vocalsPath', 'instrumental': 'instrumentalPath'}[role]
                    manifest['audioSource'][field] = str(extracted.resolve())
        if stream.read(1):
            raise ValueError('Trailing JPP payload')
        if legacy_fixture:
            manifest['schemaVersion'] = 3
            for key in ('resources', 'imageAsset', 'practiceSettings', 'processing'):
                manifest.pop(key, None)
        return manifest
