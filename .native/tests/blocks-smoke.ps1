param([Parameter(Mandatory)][string]$Plugin)
$ErrorActionPreference = 'Stop'
# Historical native regression.
$parts = @('Pull requests','Agents','Security and quality','Insights')
$translations = @("拉取`n请求",'代理','安全与质量','洞察')
foreach ($case in @('google','zvo','microsoft','deepl','openai','google-numeric','openai-numeric','deepl-large')) {
    $provider = if ($case -eq 'deepl-large') {'deepl'} elseif ($case.EndsWith('-numeric')) {$case.Split('-')[0]} else {$case}
    $parts = @('Pull requests','Agents','Security and quality','Insights')
    $translations = @("拉取`n请求",'代理','安全与质量','洞察')
    if ($case.EndsWith('-numeric')) {
        $parts = @('Pull requests','499','Agents','42%')
        $translations = @("拉取`n请求",'第499章','代理','百分之42')
    }
    if ($case -eq 'deepl-large') {
        $parts = @(1..52 | ForEach-Object { "Part $_" })
        $translations = @(1..52 | ForEach-Object { "译文 $_" })
    }
    $server = [Net.Sockets.TcpListener]::new([Net.IPAddress]::Loopback,0)
    $server.Start()
    $port = $server.LocalEndpoint.Port
    $info = [Diagnostics.ProcessStartInfo]::new()
    $info.FileName = $Plugin
    $info.Arguments = "--blocks-test $provider http://127.0.0.1:$port"
    if ($case -eq 'deepl-large') { $info.Arguments += ' 52' }
    if ($case.EndsWith('-numeric')) {
        $partJson = ConvertTo-Json -InputObject $parts -Compress
        $info.Arguments += ' "' + $partJson.Replace('"','\"') + '"'
    }
    $info.UseShellExecute = $false
    $info.CreateNoWindow = $true
    $info.RedirectStandardOutput = $true
    $info.RedirectStandardError = $true
    $info.StandardOutputEncoding = [Text.Encoding]::UTF8
    $process = [Diagnostics.Process]::Start($info)
    $output = $process.StandardOutput.ReadToEndAsync()
    $errors = $process.StandardError.ReadToEndAsync()
    try {
        $requests = if ($provider -eq 'google') {4} elseif ($case -eq 'deepl-large') {2} else {1}
        for ($i=0; $i -lt $requests; $i++) {
            $clock = [Diagnostics.Stopwatch]::StartNew()
            while (-not $server.Pending() -and $clock.ElapsedMilliseconds -lt 9000 -and -not $process.HasExited) { Start-Sleep -Milliseconds 10 }
            if (-not $server.Pending()) { throw "$provider did not send the expected batch request: $($output.Result)" }
            $client = $server.AcceptTcpClient()
            try {
                $stream = $client.GetStream()
                $stream.ReadTimeout = 3000
                $header = [Collections.Generic.List[byte]]::new()
                while ($true) {
                    $b = $stream.ReadByte()
                    if ($b -lt 0) { throw 'EOF in headers' }
                    $header.Add([byte]$b)
                    $n = $header.Count
                    if ($n -ge 4 -and $header[$n-4] -eq 13 -and $header[$n-3] -eq 10 -and $header[$n-2] -eq 13 -and $header[$n-1] -eq 10) { break }
                }
                $headers = [Text.Encoding]::ASCII.GetString($header.ToArray())
                $length = 0
                if ($headers -match '(?im)^Content-Length: (\d+)') { $length = [int]$Matches[1] }
                $bytes = [byte[]]::new($length)
                $offset = 0
                while ($offset -lt $length) {
                    $read = $stream.Read($bytes,$offset,$length-$offset)
                    if ($read -eq 0) { throw 'EOF in body' }
                    $offset += $read
                }
                $body = [Text.Encoding]::UTF8.GetString($bytes)
                if ($provider -eq 'google') {
                    $query = (($headers -split "`r`n")[0] -split ' ')[1].Split('?')[1]
                    $values = @{}
                    foreach ($pair in $query.Split('&')) {
                        $kv = $pair.Split('=',2)
                        $values[[Net.WebUtility]::UrlDecode($kv[0])] = [Net.WebUtility]::UrlDecode($kv[1])
                    }
                    $index = [Array]::IndexOf($parts,$values.q)
                    if ($index -lt 0 -or $values.sl -ne 'en') { throw 'Google lost block text or source language' }
                    $answer = ConvertTo-Json -InputObject @(@(,@($translations[$index],$values.q,$null,$null)),$null,'en') -Depth 10 -Compress
                } elseif ($provider -eq 'zvo') {
                    $values = @{}
                    foreach ($pair in $body.Split('&')) {
                        $kv = $pair.Split('=',2)
                        $values[[Net.WebUtility]::UrlDecode($kv[0])] = [Net.WebUtility]::UrlDecode($kv[1])
                    }
                    $sent = ConvertFrom-Json -NoEnumerate $values.text
                    if ($values.from -ne 'english' -or $values.to -ne 'chinese_simplified' -or $values.lowercase -ne '0') { throw 'Zvo batch metadata changed' }
                    $answer = ConvertTo-Json @{result=1;text=$translations} -Depth 10 -Compress
                } else {
                    $request = ConvertFrom-Json -NoEnumerate $body
                    if ($provider -eq 'microsoft') {
                        $sent = @($request | ForEach-Object Text)
                        $answer = ConvertTo-Json -InputObject @($translations | ForEach-Object { @{translations=@(@{text=$_});detectedLanguage=@{language='en'}} }) -Depth 10 -Compress
                    } elseif ($provider -eq 'deepl') {
                        $sent = $request.text
                        $batchTranslations = if ($case -eq 'deepl-large') {
                            if ($sent.Count -gt 50) { throw 'DeepL text-count limit exceeded' }
                            @($sent | ForEach-Object { $translations[[Array]::IndexOf($parts,$_)] })
                        } else {$translations}
                        $answer = ConvertTo-Json @{translations=@($batchTranslations | ForEach-Object { @{text=$_;detected_source_language='EN'} })} -Depth 10 -Compress
                    } else {
                        $sent = ConvertFrom-Json -NoEnumerate $request.messages[-1].content
                        if ($request.messages[0].content -notlike '*JSON array*') { throw 'AI batch contract is missing' }
                        $answer = ConvertTo-Json @{choices=@(@{message=@{content=(ConvertTo-Json -InputObject $translations -Compress)}})} -Depth 10 -Compress
                    }
                }
                if ($provider -ne 'google' -and $case -ne 'deepl-large' -and (($sent -join '|') -ne ($parts -join '|'))) { throw "$provider changed block order or content" }
                $payload = [Text.Encoding]::UTF8.GetBytes($answer)
                $reply = [Text.Encoding]::ASCII.GetBytes("HTTP/1.1 200 OK`r`nContent-Type: application/json`r`nContent-Length: $($payload.Length)`r`nConnection: close`r`n`r`n")
                $stream.Write($reply,0,$reply.Length)
                $stream.Write($payload,0,$payload.Length)
                $stream.Flush()
            } finally { $client.Dispose() }
        }
        if (-not $process.WaitForExit(3000)) { throw "$provider did not exit" }
        $lines = $output.Result.Trim() -split "`r?`n"
        if ($process.ExitCode -ne 0) { throw "$provider failed: $($output.Result) $($errors.Result)" }
        $mapped = ConvertFrom-Json -NoEnumerate $lines[-1]
        $expected = for ($j = 0; $j -lt $parts.Count; $j++) {
            if ($parts[$j] -match '^[0-9]+(?:[.,][0-9]+)*(?:[%+])?$') {$parts[$j]} else {$translations[$j]}
        }
        if (($mapped -join '|') -ne ($expected -join '|')) { throw "$provider lost internal newlines, numeric literals or block order" }
        Write-Output "PASS: $case batch HTTP contract, ordered block mapping, internal newline preserved, clean shutdown"
    } finally {
        $server.Stop()
        if (-not $process.HasExited) { $process.Kill(); $process.WaitForExit() }
        $process.Dispose()
    }
}
