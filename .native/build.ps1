param([string]$Output = '')
$ErrorActionPreference = 'Stop'
# Historical native build; not used by the community release.
if (-not $Output) {
    $manifest = Get-Content (Join-Path $PSScriptRoot 'plugin.json') -Raw | ConvertFrom-Json
    $Output = Join-Path $PSScriptRoot $manifest.runtime.program
}
$framework = Join-Path $env:SystemRoot 'Microsoft.NET\Framework64\v4.0.30319'
$metadata = Join-Path $env:SystemRoot 'System32\WinMetadata'
$references = @(
    'System.dll', 'System.Core.dll', 'System.Drawing.dll', 'System.Windows.Forms.dll',
    'System.Net.Http.dll', 'System.Web.Extensions.dll', 'System.Runtime.WindowsRuntime.dll', 'System.Security.dll'
) | ForEach-Object { Join-Path $framework $_ }
$references += @(
    (Join-Path $framework 'WPF\UIAutomationClient.dll'),
    (Join-Path $framework 'WPF\UIAutomationTypes.dll'),
    (Join-Path $framework 'WPF\WindowsBase.dll'),
    (Join-Path $framework 'System.Runtime.InteropServices.WindowsRuntime.dll')
)
$runtimeFacade = Get-ChildItem (Join-Path $env:SystemRoot 'Microsoft.NET\assembly\GAC_MSIL\System.Runtime') -Recurse -Filter System.Runtime.dll |
    Select-Object -First 1 -ExpandProperty FullName
$references += $runtimeFacade
$references += @('Windows.Foundation.winmd', 'Windows.Globalization.winmd', 'Windows.Graphics.winmd', 'Windows.Media.winmd', 'Windows.Storage.winmd', 'Windows.System.winmd') |
    ForEach-Object { Join-Path $metadata $_ }
$arguments = @('/nologo', '/target:exe', '/platform:anycpu', '/optimize+', '/utf8output', ('/out:' + $Output))
$arguments += $references | ForEach-Object { '/reference:' + $_ }
$arguments += Join-Path $PSScriptRoot 'ScreenshotTranslate.cs'
$arguments += Join-Path $PSScriptRoot 'ServiceProfiles.cs'
$arguments += Join-Path $PSScriptRoot 'FluentUi.cs'
$arguments += Join-Path $PSScriptRoot 'FreeServices.cs'
$arguments += Join-Path $PSScriptRoot 'SpatialText.cs'
& (Join-Path $framework 'csc.exe') @arguments
if ($LASTEXITCODE -ne 0) { throw 'Native plugin compilation failed' }
Write-Output ('Built ' + $Output)
