# Language switching and widget-state regression checks.
# Copyright (c) 2026 Herbert Yeung
# Author: Herbert Yeung
# SPDX-License-Identifier: MIT

param([ValidateSet('Debug','Release')][string]$Configuration='Release',[string]$Executable='', [string]$OutputDirectory='')
$ErrorActionPreference='Stop'
$root=Split-Path -Parent $PSScriptRoot
Set-Location $root
if(-not $Executable){$Executable="$root/build/bin/$Configuration/SingLilt.exe"}
$Executable=(Resolve-Path -LiteralPath $Executable).Path
if(-not $OutputDirectory){$OutputDirectory="$root/build/i18n-dropdown/integration-$Configuration"}
if(-not [IO.Path]::IsPathRooted($OutputDirectory)){$OutputDirectory=Join-Path $root $OutputDirectory}
$OutputDirectory=[IO.Path]::GetFullPath($OutputDirectory)
if(-not $OutputDirectory.StartsWith((Resolve-Path build).Path+[IO.Path]::DirectorySeparatorChar,[StringComparison]::OrdinalIgnoreCase)){throw 'Output must stay under build'}
New-Item -ItemType Directory -Force $OutputDirectory|Out-Null

$english=@{};$chinese=@{}
foreach($locale in 'en_US','zh_CN') {
    $catalog=if($locale -eq 'en_US'){$english}else{$chinese}
    foreach($file in Get-ChildItem "language/$locale" -Filter '*.json') {
        $object=Get-Content $file.FullName -Raw|ConvertFrom-Json -AsHashtable
        foreach($entry in $object.GetEnumerator()) {
            if($catalog.ContainsKey($entry.Key)){throw "Duplicate key $($entry.Key)"}
            if($entry.Value -isnot [string]){throw "Non-string translation $($entry.Key)"}
            $catalog[$entry.Key]=$entry.Value
        }
    }
}
foreach($key in $english.Keys) {
    if(-not $chinese.ContainsKey($key)){throw "Missing Chinese key $key"}
    $a=@([regex]::Matches($english[$key],'%[1-9]')|ForEach-Object Value|Sort-Object)
    $b=@([regex]::Matches($chinese[$key],'%[1-9]')|ForEach-Object Value|Sort-Object)
    if(($a -join ',') -ne ($b -join ',')){throw "Placeholder mismatch $key"}
}
foreach($key in $chinese.Keys){if(-not $english.ContainsKey($key)){throw "Missing English key $key"}}
$references=[Collections.Generic.HashSet[string]]::new()
foreach($file in Get-ChildItem src -Recurse -File|Where-Object Extension -In '.h','.cpp') {
    foreach($match in [regex]::Matches((Get-Content $file.FullName -Raw),'"((?:ui|messages|app|cli|common)\.[A-Za-z0-9_.]+)"')) {
        [void]$references.Add($match.Groups[1].Value)
    }
}
foreach($key in $references){if(-not $english.ContainsKey($key)){throw "Missing referenced resource $key"}}
Write-Output "RESOURCE_PARITY keys=$($english.Count) references=$($references.Count) PASS"

foreach($language in 'zh_CN','en_US') {
    $report="$OutputDirectory/catalog-$language.json"
    $p=Start-Process -FilePath $Executable -ArgumentList "--catalog-check --language $language --report `"$report`"" -WindowStyle Hidden -PassThru -Wait
    if($p.ExitCode -ne 0){throw "Catalog check $language failed: $($p.ExitCode)"}
    $r=Get-Content $report -Raw|ConvertFrom-Json
    if(-not $r.passed){throw "External $language catalog invalid"}
    Write-Output "EXTERNAL_CATALOG $language PASS exit=0"
}
$report="$OutputDirectory/ui.json"
$p=Start-Process -FilePath $Executable -ArgumentList "--ui-localization-check --language zh_CN --report `"$report`"" -WindowStyle Hidden -PassThru -Wait
$r=Get-Content $report -Raw|ConvertFrom-Json
if($p.ExitCode -ne 0 -or -not $r.passed){throw "UI localization/contrast check failed: $($p.ExitCode); see $report"}
Write-Output "GUI_SWITCH_AND_POPUP_CONTRAST PASS exit=0"
Write-Output "English popup: blue=$($r.englishPopup.selectedBluePixels), whiteText=$($r.englishPopup.selectedWhiteTextPixels)"
Write-Output "Chinese popup: blue=$($r.chinesePopup.selectedBluePixels), whiteText=$($r.chinesePopup.selectedWhiteTextPixels)"
$summary=@{passed=$true;resourceKeys=$english.Count;referencedKeys=$references.Count;ui=$r;executableSHA256=(Get-FileHash $Executable).Hash}
$summary|ConvertTo-Json -Depth 8|Set-Content "$OutputDirectory/summary.json" -Encoding utf8
