$ErrorActionPreference = 'Stop'

$script:RepoRoot = Split-Path -Parent $PSScriptRoot

function Get-MCStudioQtRoot {
    $candidates = @()
    if ($env:QTDIR) { $candidates += $env:QTDIR }
    $candidates += @(
        'D:\qit\Qt',
        'C:\Qt\6.12.0\msvc2022_64',
        'C:\Qt\6.9.0\msvc2022_64'
    )

    foreach ($candidate in $candidates) {
        if ($candidate -and (Test-Path -LiteralPath (Join-Path $candidate 'bin\qmake.exe'))) {
            return (Resolve-Path -LiteralPath $candidate).Path
        }
    }

    throw 'Qt 6 MSVC x64 not found. Set QTDIR to the Qt prefix, for example D:\qit\Qt.'
}

function Get-MCStudioBuildDir {
    return (Join-Path $script:RepoRoot 'build\windows-msvc')
}

function Invoke-MCStudioBuild {
    param(
        [ValidateSet('Debug', 'Release')]
        [string]$Configuration = 'Release',
        [switch]$Fresh
    )

    $qtRoot = Get-MCStudioQtRoot
    $buildDir = Get-MCStudioBuildDir

    $configure = @(
        '-S', $script:RepoRoot,
        '-B', $buildDir,
        '-G', 'Visual Studio 18 2026',
        '-A', 'x64',
        "-DCMAKE_PREFIX_PATH=$qtRoot"
    )
    if ($Fresh) { $configure = @('--fresh') + $configure }

    Write-Host "==> Configuring ($qtRoot)" -ForegroundColor Cyan
    & cmake @configure 2>&1 | Out-Host
    if ($LASTEXITCODE -ne 0) { throw 'CMake configure failed.' }

    Write-Host "==> Building $Configuration" -ForegroundColor Cyan
    & cmake --build $buildDir --config $Configuration --target MCStudio 2>&1 | Out-Host
    if ($LASTEXITCODE -ne 0) { throw 'CMake build failed.' }

    $exe = Join-Path $buildDir "bin\$Configuration\MCStudio.exe"
    if (-not (Test-Path -LiteralPath $exe)) {
        throw "Build finished but executable was not found: $exe"
    }

    return [pscustomobject]@{
        QtRoot = $qtRoot
        BuildDir = $buildDir
        Configuration = $Configuration
        Exe = $exe
    }
}

function Deploy-MCStudio {
    param(
        [ValidateSet('Debug', 'Release')]
        [string]$Configuration = 'Release'
    )

    $build = Invoke-MCStudioBuild -Configuration $Configuration
    $dist = Join-Path $script:RepoRoot 'dist\MCStudio'
    New-Item -ItemType Directory -Force -Path $dist | Out-Null

    $targetExe = Join-Path $dist 'MCStudio.exe'
    Copy-Item -LiteralPath $build.Exe -Destination $targetExe -Force

    $env:PATH = "$($build.QtRoot)\bin;$env:PATH"
    $windeployqt = Join-Path $build.QtRoot 'bin\windeployqt.exe'
    if (-not (Test-Path -LiteralPath $windeployqt)) {
        throw "windeployqt not found: $windeployqt"
    }

    $mode = if ($Configuration -eq 'Debug') { '--debug' } else { '--release' }
    Write-Host '==> Deploying Qt runtime' -ForegroundColor Cyan
    & $windeployqt $mode --no-translations --no-system-d3d-compiler --no-opengl-sw --dir $dist $targetExe 2>&1 | Out-Host
    if ($LASTEXITCODE -ne 0) { throw 'windeployqt failed.' }

    return $dist
}

function Get-MCStudioVersion {
    $cmakeFile = Join-Path $script:RepoRoot 'CMakeLists.txt'
    $match = Select-String -LiteralPath $cmakeFile -Pattern 'project\(MCStudio VERSION ([0-9]+\.[0-9]+\.[0-9]+)' |
        Select-Object -First 1
    if ($match) { return $match.Matches[0].Groups[1].Value }
    return '0.2.2'
}
