param(
    [string]$QtRoot = '',
    [string]$VsRoot = 'D:\VisualStudio\Product',
    [string]$Output = '',
    [switch]$Clean
)
$ErrorActionPreference = 'Stop'
if (-not $QtRoot) { $QtRoot = Join-Path $PSScriptRoot '..\.host\qt\6.12.0\msvc2022_64' }
if (-not $Output) { $Output = Join-Path $env:TEMP 'winshun-st-native-047' }
& python -B (Join-Path $PSScriptRoot 'build_ecdict.py')
if ($LASTEXITCODE) { throw 'ECDICT dictionary preparation failed' }
Import-Module (Join-Path $VsRoot 'Common7\Tools\Microsoft.VisualStudio.DevShell.dll')
Enter-VsDevShell -VsInstallPath $VsRoot -SkipAutomaticLocation -DevCmdArguments '-arch=x64 -host_arch=x64'
$env:VSLANG = '1033'
$cmake = Join-Path $VsRoot 'Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe'
$ninja = Join-Path $VsRoot 'Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja\ninja.exe'
& $cmake -S $PSScriptRoot -B $Output -G Ninja "-DCMAKE_MAKE_PROGRAM=$ninja" "-DCMAKE_PREFIX_PATH=$QtRoot" -DCMAKE_BUILD_TYPE=Release
if ($LASTEXITCODE) { throw 'Native CMake configuration failed' }
$nativeBuildArgs = @('--build', $Output, '--parallel', '4')
if ($Clean) { $nativeBuildArgs += '--clean-first' }
& $cmake @nativeBuildArgs
if ($LASTEXITCODE) { throw 'Native build failed' }
