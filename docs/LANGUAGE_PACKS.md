# 外置语言包

界面翻译从程序目录旁的 `language` 读取，不编入 EXE。新增语言包后重启程序即可，无需重新 build。

```text
SingLilt.exe
language/
  config.json
  en_US/
    app.json
    common.json
    ui.json
    ...
  zh_CN/
    ...
  fr_FR/
    ...
```

## 配置

`config.json` 指定默认和回退语言；这些 ID 来自部署数据，不是 C++ 中的语言枚举：

```json
{
  "defaultLocale": "zh_CN",
  "fallbackLocale": "en_US"
}
```

两个目录都要存在。已有语言偏好仍优先使用；命令行 `--language` 可以指定语言。语言 ID 不区分大小写，连字符与下划线等价；语言简称在目录可明确匹配时也可使用。

## 新增翻译

1. 从回退语言目录复制一份到新目录，例如 `fr_FR`。
2. 翻译 JSON 的值，保留原键名以及 `%1`、`%2` 等参数。
3. 修改 `common.json` 中的 `common.language_name`，作为下拉框中的名称。
4. 将目录放进安装目录的 `language`，然后重启程序。

每个语言目录内的 `*.json` 合并为一份词典，文件须为 UTF-8、顶层对象、字符串键值。同一键在多个文件中重复会被拒绝。缺少的键使用配置的回退语言；参数不匹配、无效 JSON 或非字符串值会使该语言包失效，不影响其他有效语言的切换。

翻译校验：

```powershell
SingLilt.exe --language fr_FR --catalog-check --report catalog-report.json
```

此诊断会报告缺少的键和损坏的语言包。完整翻译应通过校验；部分翻译可使用回退，但仍会列出缺项。

当前采用启动时发现，不监控运行中的文件变更。图标、字体和样式仍由 Qt 资源系统内嵌；歌曲、音源、模型与语言包是外置数据。课程的名称和说明属于课程 JSON，增加界面语言包不会自动翻译课程内容。
