param(
    [ValidateSet('Debug', 'Release')]
    [string]$Configuration = 'Release'
)

. "$PSScriptRoot\common.ps1"

$build = Invoke-MCStudioBuild -Configuration $Configuration
$env:PATH = "$($build.QtRoot)\bin;$env:PATH"
$env:QT_PLUGIN_PATH = "$($build.QtRoot)\plugins"

Write-Host ''
Write-Host "Launching: $($build.Exe)" -ForegroundColor Green
Start-Process -FilePath $build.Exe -WorkingDirectory (Split-Path -Parent $build.Exe)
