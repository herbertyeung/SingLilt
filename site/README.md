# SingLilt project website

The site is plain HTML, CSS and JavaScript. There is no package installation or build step.

From this directory, start a local preview:

```powershell
python -m http.server 4173 --bind 127.0.0.1
```

Open `http://127.0.0.1:4173`. English is the default; `?lang=zh` opens Chinese. The appearance buttons switch genuine desktop screenshots, not the running app.

The screenshots show the project's authored scale exercise. The English light screenshot comes from the isolated `--smoke` diagnostic; the Chinese dark screenshot comes from `--theme-check`. The existing project icon is reused. Project-owned site assets follow the MIT license; see `../LICENSE` and `../docs/BRAND.md` for asset provenance.

The page links to source while no installer is published. Change that copy only when a real public release is available. Keep download targets, required tools and supported features aligned with the root README.
