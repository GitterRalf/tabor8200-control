param(
    [string]$QtKit = 'C:/Qt/Qt6/6.11.0/mingw_64',
    [string]$CompilerDirectory = 'C:/Qt/Tools/mingw13_1_0/mingw64/bin',
    [string]$BuildAliasRoot = (Join-Path ([IO.Path]::GetTempPath()) 'Tabor8200-Control-build')
)
$ErrorActionPreference = 'Stop'
$projectDirectory = [IO.Path]::GetFullPath($PSScriptRoot)
$hashAlgorithm = [Security.Cryptography.SHA256]::Create()
try {
    $digest = $hashAlgorithm.ComputeHash([Text.Encoding]::UTF8.GetBytes($projectDirectory.ToLowerInvariant()))
    $projectHash = ([BitConverter]::ToString($digest)).Replace('-', '').Substring(0, 12)
} finally {
    $hashAlgorithm.Dispose()
}
$aliasPath = Join-Path ([IO.Path]::GetFullPath($BuildAliasRoot)) "source-$projectHash"
if ($aliasPath -match '[^\x00-\x7F]') {
    throw 'BuildAliasRoot must use a path without non-ASCII characters for the Qt/MinGW tools.'
}
if (Test-Path -LiteralPath $aliasPath) {
    $entry = Get-Item -LiteralPath $aliasPath
    if ($entry.LinkType -ne 'Junction' -or [IO.Path]::GetFullPath([string]$entry.Target) -ne $projectDirectory) {
        throw "The build alias belongs to a different directory: $aliasPath"
    }
} else {
    $null = New-Item -ItemType Directory -Path (Split-Path -Parent $aliasPath) -Force
    $null = New-Item -ItemType Junction -Path $aliasPath -Target $projectDirectory
}
$buildDirectory = Join-Path $aliasPath 'build'
$configureArguments = @('-S', $aliasPath, '-B', $buildDirectory, '-G', 'Ninja',
    "-DCMAKE_PREFIX_PATH=$QtKit", "-DCMAKE_CXX_COMPILER=$CompilerDirectory/g++.exe", '-DCMAKE_BUILD_TYPE=Release')
$cachePath = Join-Path $buildDirectory 'CMakeCache.txt'
if (Test-Path -LiteralPath $cachePath) {
    $sourceEntry = Select-String -LiteralPath $cachePath -Pattern '^CMAKE_HOME_DIRECTORY:INTERNAL=' | Select-Object -First 1
    if ($sourceEntry -and $sourceEntry.Line.Substring('CMAKE_HOME_DIRECTORY:INTERNAL='.Length) -ne $aliasPath.Replace('\','/')) {
        $configureArguments += '--fresh'
    }
}
$previousBuildPath = $env:Path
try {
    $env:Path = "$CompilerDirectory;$QtKit/bin;" + $env:Path
    & cmake @configureArguments
    if ($LASTEXITCODE -ne 0) { throw 'CMake configuration failed.' }
    & cmake --build $buildDirectory --parallel 4
    if ($LASTEXITCODE -ne 0) { throw 'Tabor 8200 Control build failed.' }
    & "$QtKit/bin/windeployqt.exe" --release --no-translations --no-system-d3d-compiler --no-opengl-sw (Join-Path $buildDirectory 'Tabor8200-Control.exe')
    if ($LASTEXITCODE -ne 0) { throw 'Qt runtime deployment failed.' }
} finally {
    $env:Path = $previousBuildPath
}
Write-Output (Join-Path $projectDirectory 'build\Tabor8200-Control.exe')
