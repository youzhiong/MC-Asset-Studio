param(
    [string]$LegacyRoot = "",
    [string]$TestRoot = ""
)

$ErrorActionPreference = 'Stop'
$repo = Split-Path -Parent $PSScriptRoot

if (-not $LegacyRoot) {
    $LegacyRoot = Join-Path $repo 'dist\MCStudio\data'
}

if (-not (Test-Path (Join-Path $LegacyRoot 'settings.ini'))) {
    Write-Host "找不到旧数据目录：$LegacyRoot" -ForegroundColor Red
    Write-Host "请确认旧版程序还在 dist\MCStudio 下。" -ForegroundColor Yellow
    exit 1
}

if ($TestRoot) {
    $appdata = Join-Path $TestRoot 'appdata'
    $docsRoot = Join-Path $TestRoot 'documents'
} else {
    $appdata = Join-Path $env:LOCALAPPDATA 'MCStudio'
    $docsRoot = [Environment]::GetFolderPath('MyDocuments')
    if (-not $docsRoot) {
        $docsRoot = Join-Path $env:USERPROFILE 'Documents'
    }
}

$projects = Join-Path $docsRoot 'MC Asset Studio'
$legacyProjects = Join-Path $LegacyRoot 'projects'

Write-Host ''
Write-Host '=== 把旧版数据复制到新版位置 ===' -ForegroundColor Cyan
Write-Host "旧数据：$LegacyRoot"
Write-Host "新设置：$appdata"
Write-Host "新项目：$projects"
Write-Host ''
Write-Host '只复制，不删除、不移动旧数据。' -ForegroundColor Yellow

if (-not $TestRoot) {
    $answer = Read-Host '继续？(Y/n)'
    if ($answer -and $answer -notmatch '^[Yy]') {
        Write-Host '已取消。'
        exit 0
    }
}

New-Item -ItemType Directory -Force -Path $appdata | Out-Null
New-Item -ItemType Directory -Force -Path $projects | Out-Null

# 先备份新版可能已经生成的空设置，避免直接覆盖。
$newSettings = Join-Path $appdata 'settings.ini'
if (Test-Path $newSettings) {
    $backup = "$newSettings.bak-$(Get-Date -Format 'yyyyMMdd-HHmmss')"
    Copy-Item -LiteralPath $newSettings -Destination $backup -Force
}

Copy-Item -LiteralPath (Join-Path $LegacyRoot 'settings.ini') -Destination $newSettings -Force

foreach ($sub in @('cache', 'game')) {
    $src = Join-Path $LegacyRoot $sub
    if (Test-Path $src) {
        Copy-Item -LiteralPath $src -Destination (Join-Path $appdata $sub) -Recurse -Force
    }
}

$copied = 0
$skipped = 0
if (Test-Path $legacyProjects) {
    Get-ChildItem -LiteralPath $legacyProjects -Directory | ForEach-Object {
        $dest = Join-Path $projects $_.Name
        if (Test-Path $dest) {
            $skipped++
        } else {
            Copy-Item -LiteralPath $_.FullName -Destination $dest -Recurse -Force
            $copied++
        }
    }
}

# settings.ini 里的“最近项目”和“上次项目”仍然指向旧的 dist 路径，改到新位置。
$oldPrefix = ($legacyProjects -replace '\\', '/')
$newPrefix = ($projects -replace '\\', '/')
$text = [System.IO.File]::ReadAllText($newSettings)
$text = $text.Replace($oldPrefix, $newPrefix)
$text = $text.Replace($oldPrefix.Replace('/', '\'), $newPrefix.Replace('/', '\'))
[System.IO.File]::WriteAllText($newSettings, $text, (New-Object System.Text.UTF8Encoding($false)))

Set-Content -LiteralPath (Join-Path $appdata '.migrated-from-legacy-data') -Value 'copied by migrate-data.ps1' -Encoding ascii

Write-Host ''
Write-Host "完成：复制 $copied 个项目，跳过 $skipped 个已存在的项目。" -ForegroundColor Green
Write-Host "现在可以双击 scripts\run.bat 打开新版，项目会出现在“打开项目”和“最近项目”里。"
Write-Host "旧的 dist\MCStudio\data 仍在原处，没有动。"
