// Website language and screenshot controls. Copyright (c) 2026 Herbert Yeung. SPDX-License-Identifier: MIT
const languageButtons = document.querySelectorAll("[data-language]");
const appearanceButtons = document.querySelectorAll("[data-appearance]");
const image = document.querySelector("#app-image");
const copyStatus = document.querySelector("#copy-status");
const buildCommand = document.querySelector("#build-command");
const buildGuide = document.querySelector("#build-guide");
const linuxDesktop = /Linux/i.test(navigator.userAgent) && !/Android|CrOS/i.test(navigator.userAgent);
let language = "en";
let appearance = "dark";

if (linuxDesktop) {
  buildCommand.textContent = "bash scripts/setup-linux.sh && bash scripts/build-linux.sh Release";
  buildGuide.href = "https://github.com/herbertyeung/SingLilt/blob/main/docs/LINUX.md";
}

function updateScreenshot() {
  image.src = `assets/app-${language}-${appearance}.png`;
}

function setLanguage(selected) {
  language = selected === "zh" ? "zh" : "en";
  document.documentElement.lang = language === "zh" ? "zh-CN" : "en";
  document.querySelectorAll("[data-en][data-zh]").forEach(element => {
    element.textContent = (linuxDesktop && element.getAttribute(`data-${language}-linux`)) || element.dataset[language];
  });
  document.querySelectorAll("[data-en-label][data-zh-label]").forEach(element => {
    element.setAttribute("aria-label", element.getAttribute(`data-${language}-label`));
  });
  languageButtons.forEach(button => button.setAttribute("aria-pressed", String(button.dataset.language === language)));
  image.alt = language === "zh"
    ? "SingLilt 的内置音阶与节奏练习，包含速度、移调和乐句循环控制。"
    : "SingLilt's built-in scales and rhythm exercise, with tempo, transposition and phrase-loop controls.";
  updateScreenshot();
  document.title = language === "zh" ? "SingLilt — 听谱、改谱、练唱" : "SingLilt — Score playback and singing practice";
  document.querySelector('meta[name="description"]').content = language === "zh"
    ? "用 SingLilt 导入乐谱、核对音符并练唱。支持 Windows 和 Ubuntu。"
    : "Open a score, check the notes and practise singing with SingLilt on Windows or Ubuntu.";
  copyStatus.textContent = "";
  const url = new URL(window.location.href);
  if (language === "zh") url.searchParams.set("lang", "zh");
  else url.searchParams.delete("lang");
  history.replaceState(null, "", url);
}

languageButtons.forEach(button => button.addEventListener("click", () => setLanguage(button.dataset.language)));
appearanceButtons.forEach(button => button.addEventListener("click", () => {
  appearance = button.dataset.appearance;
  updateScreenshot();
  appearanceButtons.forEach(choice => choice.setAttribute("aria-pressed", String(choice === button)));
}));

document.querySelector("#copy-command").addEventListener("click", async () => {
  try {
    await navigator.clipboard.writeText(document.querySelector("#build-command").textContent);
    copyStatus.textContent = language === "zh" ? "已复制" : "Copied";
  } catch {
    copyStatus.textContent = language === "zh" ? "请选中命令并复制。" : "Select the command and copy it.";
  }
});

setLanguage(new URLSearchParams(window.location.search).get("lang"));
