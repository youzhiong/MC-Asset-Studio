param(
    [ValidateSet('Debug', 'Release')]
    [string]$Configuration = 'Release',
    [switch]$ForceDeploy,
    [switch]$NoBuild
)

. "$PSScriptRoot\common.ps1"

$dist = Join-Path $script:RepoRoot 'dist\MCStudio'
$deployedExe = Join-Path $dist 'MCStudio.exe'
$deployedQt = Join-Path $dist 'Qt6Core.dll'

if ($NoBuild) {
    if (-not (Test-Path -LiteralPath $deployedExe) -or
        -not (Test-Path -LiteralPath $deployedQt)) {
        throw "NoBuild was requested, but dist\MCStudio is incomplete."
    }
    Write-Host "Packaging existing deployment: $dist" -ForegroundColor Cyan
} elseif ($ForceDeploy -or -not (Test-Path -LiteralPath $deployedExe) -or
    -not (Test-Path -LiteralPath $deployedQt)) {
    $dist = Deploy-MCStudio -Configuration $Configuration
} else {
    # Keep an existing deployment while refreshing only our executable. This
    # avoids re-running windeployqt for every package build.
    $build = Invoke-MCStudioBuild -Configuration $Configuration
    Copy-Item -LiteralPath $build.Exe -Destination $deployedExe -Force
    Write-Host "Using existing Qt deployment: $dist" -ForegroundColor Cyan
}

$version = Get-MCStudioVersion
$zip = Join-Path $script:RepoRoot "dist\MCStudio-$version-win64.zip"

# Runtime data (imported projects, source.json, sessions, settings) lives in
# dist\MCStudio\data and must never be shipped inside the application archive.
$stageRoot = Join-Path $script:RepoRoot 'dist\package'
$stage = Join-Path $stageRoot 'MCStudio'
if (Test-Path -LiteralPath $stage) {
    Remove-Item -LiteralPath $stage -Recurse -Force
}
New-Item -ItemType Directory -Force -Path $stage | Out-Null
Get-ChildItem -LiteralPath $dist -Force |
    Where-Object { $_.Name -ne 'data' } |
    ForEach-Object {
        Copy-Item -LiteralPath $_.FullName -Destination $stage -Recurse -Force
    }

Write-Host '==> Packaging' -ForegroundColor Cyan
Compress-Archive -Path $stage -DestinationPath $zip -Force

Write-Host ''
Write-Host "Package: $zip" -ForegroundColor Green
