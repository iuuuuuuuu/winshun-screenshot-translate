param(
    [string]$QtRoot = '',
    [string]$VsRoot = 'D:\VisualStudio\Product',
    [string]$Output = '',
    [switch]$Clean
)
$ErrorActionPreference = 'Stop'
if (-not $QtRoot) { $QtRoot = Join-Path $PSScriptRoot '..\.host\qt\6.12.0\msvc2022_64' }
if (-not $Output) { $Output = Join-Path $env:TEMP 'winshun-st-native-0410' }
& python -B (Join-Path $PSScriptRoot 'build_ecdict.py')
if ($LASTEXITCODE) { throw 'ECDICT dictionary preparation failed' }
Import-Module (Join-Path $VsRoot 'Common7\Tools\Microsoft.VisualStudio.DevShell.dll')
Enter-VsDevShell -VsInstallPath $VsRoot -SkipAutomaticLocation -DevCmdArguments '-arch=x64 -host_arch=x64'
$env:VSLANG = '1033'
$cmake = Join-Path $VsRoot 'Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe'
$ninja = Join-Path $VsRoot 'Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja\ninja.exe'
# Some localized MSVC installations emit an include prefix that CMake/Ninja
# decodes incorrectly. Then header edits leave stale object layouts behind.
# Independently invalidate all native objects when any local header changes.
$headerState = (Get-ChildItem -LiteralPath $PSScriptRoot -Recurse -File |
    Where-Object { $_.Extension -in '.h', '.hpp', '.inl' } |
    Sort-Object FullName | ForEach-Object { $_.FullName + ':' + (Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash }) -join "`n"
$headerStamp = Join-Path $Output '.st-native-headers'
$headersChanged = !(Test-Path -LiteralPath $headerStamp) -or (Get-Content -LiteralPath $headerStamp -Raw) -cne $headerState
& $cmake -S $PSScriptRoot -B $Output -G Ninja "-DCMAKE_MAKE_PROGRAM=$ninja" "-DCMAKE_PREFIX_PATH=$QtRoot" -DCMAKE_BUILD_TYPE=Release
if ($LASTEXITCODE) { throw 'Native CMake configuration failed' }
$nativeBuildArgs = @('--build', $Output, '--parallel', '4')
if ($Clean -or $headersChanged) { $nativeBuildArgs += '--clean-first' }
& $cmake @nativeBuildArgs
if ($LASTEXITCODE) { throw 'Native build failed' }
[System.IO.File]::WriteAllText($headerStamp, $headerState)
