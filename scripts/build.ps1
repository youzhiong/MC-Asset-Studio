param(
    [ValidateSet('Debug', 'Release')]
    [string]$Configuration = 'Release',
    [switch]$Fresh
)

. "$PSScriptRoot\common.ps1"

$build = Invoke-MCStudioBuild -Configuration $Configuration -Fresh:$Fresh
Write-Host ''
Write-Host "Built: $($build.Exe)" -ForegroundColor Green
Write-Host "Qt:    $($build.QtRoot)"
