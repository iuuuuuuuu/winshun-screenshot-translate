param(
    [string]$QtRoot = '',
    [string]$VsRoot = 'D:\VisualStudio\Product',
    [string]$Output = ''
)
$ErrorActionPreference = 'Stop'
if (-not $QtRoot) { $QtRoot = Join-Path $PSScriptRoot '..\.host\qt\6.10.0\msvc2022_64' }
if (-not $Output) { $Output = Join-Path $env:TEMP 'winshun-st-native-040' }
Import-Module (Join-Path $VsRoot 'Common7\Tools\Microsoft.VisualStudio.DevShell.dll')
Enter-VsDevShell -VsInstallPath $VsRoot -SkipAutomaticLocation -DevCmdArguments '-arch=x64 -host_arch=x64'
$env:VSLANG = '1033'
$cmake = Join-Path $VsRoot 'Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe'
$ninja = Join-Path $VsRoot 'Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja\ninja.exe'
& $cmake -S $PSScriptRoot -B $Output -G Ninja "-DCMAKE_MAKE_PROGRAM=$ninja" "-DCMAKE_PREFIX_PATH=$QtRoot" -DCMAKE_BUILD_TYPE=Release
if ($LASTEXITCODE) { throw 'Native CMake configuration failed' }
& $cmake --build $Output --parallel 4
if ($LASTEXITCODE) { throw 'Native build failed' }
