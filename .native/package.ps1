param([string]$OutputDirectory = (Join-Path $PSScriptRoot 'dist'))
$ErrorActionPreference = 'Stop'
# Historical native package; not used by the community release.
Add-Type -AssemblyName System.IO.Compression
Add-Type -AssemblyName System.IO.Compression.FileSystem
$manifest = Get-Content -LiteralPath (Join-Path $PSScriptRoot 'plugin.json') -Raw | ConvertFrom-Json
if ($manifest.id -ne 'iuuuuuuuu.screenshot-translate' -or $manifest.version -notmatch '^\d+\.\d+\.\d+$') { throw 'Invalid public identity or version' }
if ($manifest.author -ne 'iuuuuuuuu' -or $manifest.license -ne 'MIT') { throw 'Missing author or license' }
if ($manifest.runtime.program -ne 'ScreenshotTranslate.exe' -or $manifest.runtime.mode -ne 'resident') { throw 'Unexpected native runtime' }
if ($manifest.minWinShun -ne '0.19.0') { throw 'Unexpected minimum host version' }
$expectedPermissions = @('ui','clipboard:read','clipboard:write','network','files:write')
if (Compare-Object ($manifest.permissions | Sort-Object) ($expectedPermissions | Sort-Object)) { throw 'Permissions changed; review before packaging' }
$files = @('plugin.json','ScreenshotTranslate.exe','README.md','LICENSE','NOTICE.md','CHANGELOG.md',
    'ScreenshotTranslate.cs','ServiceProfiles.cs','FluentUi.cs','FreeServices.cs','SpatialText.cs','build.ps1')
$total = 0
foreach ($name in $files) {
    $file = Get-Item -LiteralPath (Join-Path $PSScriptRoot $name)
    if ($file.Attributes -band [IO.FileAttributes]::ReparsePoint) { throw 'Links cannot be packaged' }
    if ($file.Length -gt 1MB) { throw "Unexpected file size: $name" }
    $total += $file.Length
}
if ($total -gt 5MB) { throw 'Package is unexpectedly large' }
$null = [IO.Directory]::CreateDirectory([IO.Path]::GetFullPath($OutputDirectory))
$archivePath = Join-Path $OutputDirectory "$($manifest.id)-$($manifest.version).wsplugin"
if (Test-Path -LiteralPath $archivePath) { throw 'Package already exists; never overwrite a published version' }
$archive = [IO.Compression.ZipFile]::Open($archivePath, [IO.Compression.ZipArchiveMode]::Create)
try {
    foreach ($name in $files) {
        $entry = $archive.CreateEntry($name, [IO.Compression.CompressionLevel]::Optimal)
        $entry.LastWriteTime = [DateTimeOffset]::new(2026,1,1,0,0,0,[TimeSpan]::Zero)
        $inputStream = [IO.File]::OpenRead((Join-Path $PSScriptRoot $name))
        try {
            $outputStream = $entry.Open()
            try { $inputStream.CopyTo($outputStream) }
            finally { $outputStream.Dispose() }
        } finally { $inputStream.Dispose() }
    }
} finally { $archive.Dispose() }
$hash = (Get-FileHash -LiteralPath $archivePath -Algorithm SHA256).Hash.ToLowerInvariant()
[IO.File]::WriteAllText("$archivePath.sha256", "$hash  $([IO.Path]::GetFileName($archivePath))`n", [Text.UTF8Encoding]::new($false))
Write-Output "Native manual-install package: $archivePath"
Write-Output "SHA256: $hash"
