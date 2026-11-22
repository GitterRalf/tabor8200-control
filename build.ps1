param(
    [string]$QtKit,
    [string]$CompilerDirectory,
    [string]$BuildAliasRoot = (Join-Path ([IO.Path]::GetTempPath()) 'Tabor8200-Control-build'),
    [ValidateSet('x64', 'x86')]
    [string]$Architecture = 'x64'
)
$ErrorActionPreference = 'Stop'

if ($Architecture -eq 'x86') {
    if (!$QtKit) { $QtKit = 'C:/Qt/Qt5.15.19/mingw81_32' }
    if (!$CompilerDirectory) { $CompilerDirectory = 'C:/Qt/Qt5.13.2/Tools/mingw810_32/bin' }
    $qtMajor = 5
    $triple = 'i686-w64-mingw32'
    $buildName = 'build-x86'
} else {
    if (!$QtKit) { $QtKit = 'C:/Qt/Qt6/6.11.0/mingw_64' }
    if (!$CompilerDirectory) { $CompilerDirectory = 'C:/Qt/Tools/mingw13_1_0/mingw64/bin' }
    $qtMajor = 6
    $triple = 'x86_64-w64-mingw32'
    $buildName = 'build'
}
$compiler = "$CompilerDirectory/g++.exe"
if ((& $compiler -dumpmachine) -ne $triple) {
    throw "Für $Architecture wird ein MinGW-Compiler für $triple benötigt."
}
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
$buildDirectory = Join-Path $aliasPath $buildName
$configureArguments = @('-S', $aliasPath, '-B', $buildDirectory, '-G', 'Ninja',
    "-DCMAKE_PREFIX_PATH=$QtKit", "-DCMAKE_CXX_COMPILER=$compiler", "-DDEVICE_QT_MAJOR=$qtMajor", '-DCMAKE_BUILD_TYPE=Release')
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
    if ($qtMajor -eq 6) {
        & "$QtKit/bin/windeployqt.exe" --release --no-translations --no-system-d3d-compiler --no-opengl-sw (Join-Path $buildDirectory 'Tabor8200-Control.exe')
        if ($LASTEXITCODE -ne 0) { throw 'Qt runtime deployment failed.' }
    } else {
        # This Qt 5 MinGW kit's release plugins contain debug info.
        # Copy from the kit directly because windeployqt skips those plugins.
        $qtModules = 'Core', 'Gui', 'Network', 'Svg', 'Widgets'
        foreach ($module in $qtModules) {
            Copy-Item -LiteralPath "$QtKit/bin/Qt5$module.dll" -Destination $buildDirectory
        }
        foreach ($dll in 'libgcc_s_dw2-1.dll', 'libstdc++-6.dll', 'libwinpthread-1.dll') {
            Copy-Item -LiteralPath "$CompilerDirectory/$dll" -Destination $buildDirectory
        }
        Set-Content -LiteralPath (Join-Path $buildDirectory 'qt.conf') -Encoding ascii -Value '[Paths]', 'Prefix=.', 'Plugins=.'
        $pluginRoot = Join-Path $QtKit 'plugins'
        foreach ($group in 'bearer', 'iconengines', 'imageformats', 'styles') {
            $pluginDir = Join-Path $pluginRoot $group
            if (Test-Path -LiteralPath $pluginDir -PathType Container) {
                $target = New-Item -ItemType Directory -Path (Join-Path $buildDirectory $group) -Force
                Get-ChildItem -LiteralPath $pluginDir -File -Filter '*.dll' |
                    Copy-Item -Destination $target.FullName
            }
        }
        $platforms = New-Item -ItemType Directory -Path (Join-Path $buildDirectory 'platforms') -Force
        Copy-Item -LiteralPath (Join-Path $pluginRoot 'platforms/qwindows.dll') -Destination $platforms.FullName
    }
} finally {
    $env:Path = $previousBuildPath
}
Write-Output (Join-Path $projectDirectory "$buildName\Tabor8200-Control.exe")
