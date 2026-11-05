# MIT. One request, one Windows OCR/UI Automation worker, then exit.
$ErrorActionPreference = 'Stop'
[Console]::InputEncoding = [Text.Encoding]::UTF8
[Console]::OutputEncoding = New-Object Text.UTF8Encoding($false)

function Await-Operation($operation, [Type]$type) {
    $task = $script:AsTask.MakeGenericMethod($type).Invoke($null, @($operation))
    $task.GetAwaiter().GetResult()
}

function Read-Words([byte[]]$png, $engine, [double]$scale, [double]$padding) {
    $stream = $writer = $bitmap = $null
    try {
        $stream = New-Object Windows.Storage.Streams.InMemoryRandomAccessStream
        $writer = New-Object Windows.Storage.Streams.DataWriter($stream)
        $writer.WriteBytes($png)
        $null = Await-Operation ($writer.StoreAsync()) ([UInt32])
        $null = $writer.DetachStream()
        $null = $stream.Seek(0)
        $decoder = Await-Operation ([Windows.Graphics.Imaging.BitmapDecoder]::CreateAsync($stream)) ([Windows.Graphics.Imaging.BitmapDecoder])
        $bitmap = Await-Operation ($decoder.GetSoftwareBitmapAsync()) ([Windows.Graphics.Imaging.SoftwareBitmap])
        $result = Await-Operation ($engine.RecognizeAsync($bitmap)) ([Windows.Media.Ocr.OcrResult])
        $words = New-Object System.Collections.Generic.List[object]
        foreach ($line in $result.Lines) {
            foreach ($word in $line.Words) {
                $r = $word.BoundingRect
                $left = [Math]::Max(0, ($r.X - $padding) / $scale)
                $top = [Math]::Max(0, ($r.Y - $padding) / $scale)
                $right = [Math]::Min($script:Width, ($r.X + $r.Width - $padding) / $scale)
                $bottom = [Math]::Min($script:Height, ($r.Y + $r.Height - $padding) / $scale)
                if ($right -gt $left -and $bottom -gt $top) {
                    $words.Add(@{ text = [string]$word.Text; box = @($left, $top, ($right - $left), ($bottom - $top)) })
                }
            }
        }
        return ,$words.ToArray()
    }
    finally {
        if ($bitmap) { $bitmap.Dispose() }
        if ($writer) { $writer.Dispose() }
        if ($stream) { $stream.Dispose() }
    }
}

function Resize-Png([Drawing.Bitmap]$image, [double]$scale, [int]$border) {
    $width = [Math]::Max(1, [int](($image.Width + $border * 2) * $scale))
    $height = [Math]::Max(1, [int](($image.Height + $border * 2) * $scale))
    $bitmap = New-Object Drawing.Bitmap($width, $height)
    $graphics = [Drawing.Graphics]::FromImage($bitmap)
    $memory = New-Object IO.MemoryStream
    try {
        $graphics.Clear($image.GetPixel(0, 0))
        $graphics.InterpolationMode = [Drawing.Drawing2D.InterpolationMode]::HighQualityBicubic
        $graphics.PixelOffsetMode = [Drawing.Drawing2D.PixelOffsetMode]::HighQuality
        $graphics.DrawImage($image, [single]($border * $scale), [single]($border * $scale),
            [single]($image.Width * $scale), [single]($image.Height * $scale))
        $bitmap.Save($memory, [Drawing.Imaging.ImageFormat]::Png)
        return ,$memory.ToArray()
    }
    finally { $graphics.Dispose(); $bitmap.Dispose(); $memory.Dispose() }
}

try {
    $request = [Console]::ReadLine() | ConvertFrom-Json
    if ($request.mode -eq 'selection') {
        Add-Type -AssemblyName UIAutomationClient, UIAutomationTypes
        $focus = [Windows.Automation.AutomationElement]::FocusedElement
        $pattern = $null
        $text = ''
        if ($focus -and $focus.TryGetCurrentPattern([Windows.Automation.TextPattern]::Pattern, [ref]$pattern)) {
            $text = (($pattern.GetSelection() | ForEach-Object { $_.GetText(-1) }) -join "`n").Trim()
        }
        [Console]::WriteLine((@{ text = $text } | ConvertTo-Json -Compress))
        exit 0
    }
    Add-Type -AssemblyName System.Runtime.WindowsRuntime, System.Drawing
    $null = [Windows.Storage.Streams.InMemoryRandomAccessStream, Windows.Foundation, ContentType = WindowsRuntime]
    $null = [Windows.Graphics.Imaging.BitmapDecoder, Windows.Foundation, ContentType = WindowsRuntime]
    $null = [Windows.Media.Ocr.OcrEngine, Windows.Foundation, ContentType = WindowsRuntime]
    $null = [Windows.Globalization.Language, Windows.Foundation, ContentType = WindowsRuntime]
    $script:AsTask = ([System.WindowsRuntimeSystemExtensions].GetMethods() | Where-Object {
        $_.Name -eq 'AsTask' -and $_.IsGenericMethodDefinition -and
        $_.GetParameters().Count -eq 1 -and $_.GetParameters()[0].ParameterType.Name -eq 'IAsyncOperation`1'
    })[0]
    $engine = [Windows.Media.Ocr.OcrEngine]::TryCreateFromUserProfileLanguages()
    if (-not $engine) {
        foreach ($tag in @('zh-Hans', 'en-US', 'ja-JP')) {
            $engine = [Windows.Media.Ocr.OcrEngine]::TryCreateFromLanguage((New-Object Windows.Globalization.Language($tag)))
            if ($engine) { break }
        }
    }
    if (-not $engine) { throw 'No Windows OCR language is installed' }
    $png = [Convert]::FromBase64String($request.png)
    $memory = New-Object IO.MemoryStream (,$png)
    $image = New-Object Drawing.Bitmap($memory)
    try {
        $script:Width = $image.Width
        $script:Height = $image.Height
        $maximum = [Windows.Media.Ocr.OcrEngine]::MaxImageDimension
        $scale = [Math]::Min(1, ($maximum - 1) / [double][Math]::Max($image.Width, $image.Height))
        $inputPng = if ($scale -lt 1) { Resize-Png $image $scale 0 } else { $png }
        $words = Read-Words $inputPng $engine $scale 0
        $text = ($words | ForEach-Object { $_.text }) -join ' '
        $latin = [regex]::Matches($text, '[A-Za-z]').Count
        $han = [regex]::Matches($text, '[\u3400-\u9fff]').Count
        $kana = [regex]::Matches($text, '[\u3040-\u30ff\uac00-\ud7af]').Count
        if ($latin -gt ($han * 2 + $kana * 2) -and $engine.RecognizerLanguage.LanguageTag -notmatch '^en') {
            $english = [Windows.Media.Ocr.OcrEngine]::TryCreateFromLanguage((New-Object Windows.Globalization.Language('en-US')))
            if ($english) {
                $candidate = Read-Words $inputPng $english $scale 0
                $candidateText = ($candidate | ForEach-Object { $_.text }) -join ' '
                $hasChineseLine = $text -match '[\u3400-\u9fff]{3}'
                if (-not $hasChineseLine -and [regex]::Matches($candidateText, '[A-Za-z]').Count -ge $latin * .75) {
                    $words = $candidate
                    $engine = $english
                }
            }
        }
        $enlarged = @()
        if ($image.Height -le 800 -and ($image.Width + 12) * 2 -lt $maximum) {
            $enlarged = Read-Words (Resize-Png $image 2 6) $engine 2 12
        }
        elseif ($words.Count -eq 0 -and ([Math]::Max($image.Width, $image.Height) + 24) * 3 -lt $maximum) {
            $words = Read-Words (Resize-Png $image 3 12) $engine 3 36
        }
        [Console]::WriteLine((@{ words = @($words); enlarged = @($enlarged);
            engine = [string]$engine.RecognizerLanguage.LanguageTag } | ConvertTo-Json -Compress -Depth 8))
    }
    finally { $image.Dispose(); $memory.Dispose() }
}
catch {
    [Console]::WriteLine((@{ error = 'Windows OCR or selection reading failed. Check installed OCR languages.' } | ConvertTo-Json -Compress))
    exit 1
}
