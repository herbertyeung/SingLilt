# SingLilt project website

The site is plain HTML, CSS and JavaScript. There is no package installation or build step.

Live site: [herbertyeung.github.io/SingLilt](https://herbertyeung.github.io/SingLilt/).

## Publishing

GitHub Pages serves this directory. `.github/workflows/pages.yml` checks JavaScript, uploads `site/` and deploys it when site files are pushed to `main`. It can also be run manually from GitHub Actions. The `github-pages` environment accepts deployments from `main` only.

CSS, JavaScript and image URLs stay relative so the page works under the `/SingLilt/` project path. No external hosting configuration or account is needed.

## Local preview

From this directory, start a local preview:

```powershell
python -m http.server 4173 --bind 127.0.0.1
```

Open `http://127.0.0.1:4173`. English is the default; `?lang=zh` opens Chinese. The appearance buttons switch genuine desktop screenshots, not the running app.

The screenshots show the project's authored scale exercise. All four English/Chinese and light/dark variants come from the isolated `--theme-check` diagnostic, with `ui/language` set to the matching locale before launch. Website language and screenshot appearance are independent choices. The existing project icon is reused. Project-owned site assets follow the MIT license; see `../LICENSE` and `../docs/BRAND.md` for asset provenance.

The hero and get-started buttons link to the latest public GitHub Release. Windows visitors see the Windows installer/portable ZIP label; Linux desktop visitors see "Download for Ubuntu" for the Ubuntu 24.04 x64 archive. Android and ChromeOS keep the default text. The English/Chinese switch also updates the platform-specific copy, build command and guide link. Source downloads remain visible. Publish a Release containing both platforms before deploying a site update that advertises Ubuntu. Run `node --test site/site.test.mjs` after edits.
