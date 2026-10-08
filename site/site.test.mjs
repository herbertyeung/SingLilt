import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import { join } from 'node:path';
import { test } from 'node:test';
import { fileURLToPath } from 'node:url';
import { runInNewContext } from 'node:vm';

const site = process.env.SITE_ROOT ?? fileURLToPath(new URL('.', import.meta.url));
const source = readFileSync(join(site, 'app.js'), 'utf8');
const markup = readFileSync(join(site, 'index.html'), 'utf8');

function render(userAgent, language = 'en') {
  const download = {
    dataset: {
      en: 'Download for Windows',
      zh: '下载 Windows 版',
      enLinux: 'Download for Ubuntu',
      zhLinux: '下载 Ubuntu 版'
    },
    textContent: '',
    getAttribute(name) {
      return name === 'data-en-linux' ? this.dataset.enLinux : this.dataset.zhLinux;
    }
  };
  const image = { src: '', alt: '' };
  const copyStatus = { textContent: '' };
  const buildCommand = { textContent: 'pwsh -NoProfile -File scripts/setup.ps1' };
  const buildGuide = { href: 'https://github.com/herbertyeung/SingLilt/blob/main/docs/BUILD.md' };
  const languageButtons = ['en', 'zh'].map(value => ({
    dataset: { language: value },
    setAttribute() {},
    addEventListener(event, callback) { this.click = callback; }
  }));
  const document = {
    documentElement: { lang: '' },
    title: '',
    querySelectorAll(selector) {
      if (selector === '[data-language]') return languageButtons;
      if (selector === '[data-en][data-zh]') return [download];
      return [];
    },
    querySelector(selector) {
      if (selector === '#app-image') return image;
      if (selector === '#copy-status') return copyStatus;
      if (selector === '#build-command') return buildCommand;
      if (selector === '#build-guide') return buildGuide;
      if (selector === 'meta[name="description"]') return { content: '' };
      if (selector === '#copy-command') return { addEventListener() {} };
      throw new Error(`Unexpected selector: ${selector}`);
    }
  };
  const window = { location: { href: 'https://example.test/SingLilt/', search: language === 'zh' ? '?lang=zh' : '' } };
  const context = {
    document, window, navigator: { userAgent }, history: { replaceState() {} },
    URL, URLSearchParams
  };
  runInNewContext(source, context);
  return { download, document, languageButtons, buildCommand, buildGuide };
}

test('Linux desktop visitor sees the Ubuntu-specific download label', () => {
  const page = render('Mozilla/5.0 (X11; Linux x86_64) Firefox/131.0');
  assert.equal(page.download.textContent, 'Download for Ubuntu');
  assert.equal(page.buildCommand.textContent, 'bash scripts/setup-linux.sh && bash scripts/build-linux.sh Release');
  assert.match(page.buildGuide.href, /docs\/LINUX\.md$/);
  page.languageButtons[1].click();
  assert.equal(page.download.textContent, '下载 Ubuntu 版');
});

test('Windows and Mac visitors keep the Windows release fallback', () => {
  for (const agent of ['Mozilla/5.0 (Windows NT 10.0; Win64; x64)', 'Mozilla/5.0 (Macintosh; Intel Mac OS X)']) {
    const page = render(agent);
    assert.equal(page.download.textContent, 'Download for Windows');
    assert.equal(page.buildCommand.textContent, 'pwsh -NoProfile -File scripts/setup.ps1');
  }
});

test('Android and ChromeOS are not classified as Ubuntu desktops', () => {
  for (const agent of ['Mozilla/5.0 (Linux; Android 15)', 'Mozilla/5.0 (X11; CrOS x86_64)']) {
    assert.equal(render(agent).download.textContent, 'Download for Windows');
  }
});

test('both visible release buttons have bilingual Ubuntu text and a release destination', () => {
  const buttons = [...markup.matchAll(/<a\s+class="button primary"[^>]*>/g)].map(match => match[0]);
  assert.equal(buttons.length, 2);
  for (const button of buttons) {
    assert.match(button, /data-en-linux="Download for Ubuntu"/);
    assert.match(button, /data-zh-linux="下载 Ubuntu 版"/);
    assert.match(button, /href="https:\/\/github\.com\/herbertyeung\/SingLilt\/releases\/latest"/);
  }
});
