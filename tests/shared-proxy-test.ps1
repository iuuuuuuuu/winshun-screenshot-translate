param([Parameter(Mandatory)][string]$Plugin)
$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Security
$data = Join-Path ([IO.Path]::GetTempPath()) ('screenshot-common-proxy-' + [guid]::NewGuid().ToString('N'))
$null = [IO.Directory]::CreateDirectory($data)
$entropy = [Text.Encoding]::UTF8.GetBytes('iuuuuuuuu.screenshot-translate/profiles/v1')
$secret = [Convert]::ToBase64String([Security.Cryptography.ProtectedData]::Protect(
    [Text.Encoding]::UTF8.GetBytes('test-only'), $entropy, [Security.Cryptography.DataProtectionScope]::CurrentUser))
$profiles = @{}
$providers = @('mymemory','google','edge','zvo','microsoft','deepl','openai','deepseek','doubao','hunyuan')
foreach ($provider in $providers) {
    $profiles[$provider] = @{secret=$secret;endpoint="http://translation.invalid/$provider";model='test-model';region='';proxy='http://127.0.0.1:1'}
}
[IO.File]::WriteAllText((Join-Path $data 'services.json'),
    (ConvertTo-Json @{version=1;active='deepl';profiles=$profiles} -Depth 10 -Compress), [Text.UTF8Encoding]::new($false))
$proxy = [Net.Sockets.TcpListener]::new([Net.IPAddress]::Loopback, 0)
$fallback = [Net.Sockets.TcpListener]::new([Net.IPAddress]::Loopback, 0)
$proxy.Start(); $fallback.Start()
$proxyAddress = "http://127.0.0.1:$($proxy.LocalEndpoint.Port)"
$fallbackAddress = "http://127.0.0.1:$($fallback.LocalEndpoint.Port)"
$info = [Diagnostics.ProcessStartInfo]::new()
$info.FileName = $Plugin
$info.UseShellExecute = $false
$info.CreateNoWindow = $true
$info.RedirectStandardInput = $true
$info.RedirectStandardOutput = $true
$info.RedirectStandardError = $true
$info.StandardInputEncoding = [Text.Encoding]::UTF8
$info.StandardOutputEncoding = [Text.Encoding]::UTF8
$info.StandardErrorEncoding = [Text.Encoding]::UTF8
$info.EnvironmentVariables['WS_DATA_DIR'] = $data
$info.EnvironmentVariables['OPENAPI_TRANSLATE_PROXY'] = $fallbackAddress
$process = [Diagnostics.Process]::Start($info)
$errors = $process.StandardError.ReadToEndAsync()
$script:nextRead = $null
function Send($method, $parameters, $id) {
    $message = @{jsonrpc='2.0';method=$method;params=$parameters}
    if ($null -ne $id) { $message.id = $id }
    $process.StandardInput.WriteLine((ConvertTo-Json $message -Depth 20 -Compress))
    $process.StandardInput.Flush()
}
function Reply($id) {
    $clock = [Diagnostics.Stopwatch]::StartNew()
    while ($clock.ElapsedMilliseconds -lt 10000) {
        if ($null -eq $script:nextRead) { $script:nextRead = $process.StandardOutput.ReadLineAsync() }
        if (-not $script:nextRead.Wait(100)) { continue }
        $line = $script:nextRead.Result; $script:nextRead = $null
        if ($null -eq $line) { throw 'Plugin exited before its reply' }
        $message = ConvertFrom-Json $line
        if ($message.method -eq 'winshun/tool') { throw 'Background test requested host UI' }
        if ("$($message.id)" -eq "$id") {
            if ($message.error) { throw 'Protocol error' }
            return $message.result
        }
    }
    throw 'Reply timed out'
}
function Respond($listener, $provider) {
    $clock = [Diagnostics.Stopwatch]::StartNew()
    while (-not $listener.Pending() -and $clock.ElapsedMilliseconds -lt 9000 -and -not $process.HasExited) { Start-Sleep -Milliseconds 10 }
    if (-not $listener.Pending()) {
        $result = Reply $id
        throw ("Common proxy was not used by $provider; response=" + (ConvertTo-Json $result -Depth 10 -Compress))
    }
    $client = $listener.AcceptTcpClient()
    try {
        $stream = $client.GetStream(); $stream.ReadTimeout = 3000
        $header = [Collections.Generic.List[byte]]::new()
        while ($true) {
            $b = $stream.ReadByte()
            if ($b -lt 0) { throw 'Unexpected proxy EOF' }
            $header.Add([byte]$b)
            $n = $header.Count
            if ($n -ge 4 -and $header[$n-4] -eq 13 -and $header[$n-3] -eq 10 -and $header[$n-2] -eq 13 -and $header[$n-1] -eq 10) { break }
        }
        $headers = [Text.Encoding]::ASCII.GetString($header.ToArray())
        $length = 0
        if ($headers -match '(?im)^Content-Length: (\d+)') { $length = [int]$Matches[1] }
        $body = [byte[]]::new($length)
        $offset = 0
        while ($offset -lt $length) {
            $read = $stream.Read($body, $offset, $length-$offset)
            if ($read -eq 0) { throw 'Unexpected proxy body EOF' }
            $offset += $read
        }
        if ($provider -in @('mymemory','edge')) {
            $hostName = if ($provider -eq 'mymemory') {'api.mymemory.translated.net'} else {'edge.microsoft.com'}
            if ($headers -notlike "CONNECT $($hostName):443 *") { throw "$provider did not tunnel through the shared proxy" }
            $status = '502 Bad Gateway'; $answer = '{}'
        } else {
            if ($headers -notmatch "^(GET|POST) http://translation.invalid/$provider") { throw 'Wrong channel or proxy request URI' }
            if ($provider -in @('microsoft','deepl') -and $headers -notlike '*test-only*') { throw 'Saved provider credential was not used' }
            if ($provider -in @('openai','deepseek','doubao','hunyuan') -and $headers -notlike '*Bearer test-only*') { throw 'Host shared fields replaced a saved credential' }
            $status = '200 OK'
            $answer = switch ($provider) {
                'google' {'[[["translated","Hello world",null,null]],null,"en"]'}
                'zvo' {'{"result":1,"text":["translated"]}'}
                'microsoft' {'[{"translations":[{"text":"translated"}],"detectedLanguage":{"language":"en"}}]'}
                'deepl' {'{"translations":[{"text":"translated","detected_source_language":"EN"}]}'}
                default {'{"choices":[{"message":{"content":"translated"}}]}'}
            }
        }
        $payload = [Text.Encoding]::UTF8.GetBytes($answer)
        $reply = [Text.Encoding]::ASCII.GetBytes("HTTP/1.1 $status`r`nContent-Type: application/json`r`nContent-Length: $($payload.Length)`r`nConnection: close`r`n`r`n")
        $stream.Write($reply,0,$reply.Length); $stream.Write($payload,0,$payload.Length); $stream.Flush()
    } finally { $client.Dispose() }
}
try {
    Send 'initialize' @{settings=@{provider='google';proxy=$proxyAddress;target='zh'};locale='en';capabilities=@{hostHotkeys=$false}} 1
    $null = Reply 1
    $id = 1
    foreach ($provider in $providers) {
        Send 'settingsChanged' @{settings=@{provider=$provider;proxy=$proxyAddress;target='zh';apiKey='wrong-host-key'}} $null
        $id++
        Send 'query' @{command='clip';text='Hello world';queryId="proxy-$id"} $id
        Respond $proxy $provider
        $result = Reply $id
        if ($provider -in @('mymemory','edge')) {
            if ($result.items[0].id -ne 'error-translation') { throw 'Failed proxy tunnel was not reported' }
        } elseif ($result.items[0].data.translated -ne 'translated') { throw "$provider did not parse the proxy response" }
        if ($fallback.Pending()) { throw 'Environment proxy overrode the explicit shared proxy' }
        Write-Output "PASS: $provider uses host shared proxy and saved provider configuration"
    }
    Send 'settingsChanged' @{settings=@{provider='openai';proxy='';target='zh'}} $null
    $id++
    Send 'query' @{command='clip';text='Hello world';queryId="proxy-$id"} $id
    Respond $fallback 'openai'
    if ((Reply $id).items[0].data.translated -ne 'translated' -or $proxy.Pending()) { throw 'Clearing shared proxy retained a stale proxy' }
    Write-Output 'PASS: clearing shared proxy returns to environment fallback'
    Send 'shutdown' @{} ($id+1)
    $null = Reply ($id+1)
    $process.StandardInput.Close()
    if (-not $process.WaitForExit(3000) -or $process.ExitCode -ne 0) { throw 'Plugin shutdown failed' }
    Write-Output 'PASS: no real translation endpoint contacted, no host UI requests, clean shutdown'
} finally {
    $proxy.Stop(); $fallback.Stop()
    if (-not $process.HasExited) { $process.Kill(); $process.WaitForExit() }
    $process.Dispose()
    foreach ($name in @('services.json','mymemory-usage.json')) {
        $file = Join-Path $data $name
        if (Test-Path -LiteralPath $file) { Remove-Item -LiteralPath $file }
    }
    Remove-Item -LiteralPath $data
}
