// Website language and screenshot controls. Copyright (c) 2026 Herbert Yeung. SPDX-License-Identifier: MIT
const languageButtons = document.querySelectorAll("[data-language]");
const appearanceButtons = document.querySelectorAll("[data-appearance]");
const image = document.querySelector("#app-image");
const copyStatus = document.querySelector("#copy-status");
let language = "en";
let appearance = "dark";

function updateScreenshot() {
  image.src = `assets/app-${language}-${appearance}.png`;
}

function setLanguage(selected) {
  language = selected === "zh" ? "zh" : "en";
  document.documentElement.lang = language === "zh" ? "zh-CN" : "en";
  document.querySelectorAll("[data-en][data-zh]").forEach(element => {
    element.textContent = element.dataset[language];
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
    ? "SingLilt 是开源 Windows 桌面程序，用于乐谱播放、音符校正和唱歌练习。"
    : "SingLilt is an open-source Windows app for score playback, notation correction and singing practice.";
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
