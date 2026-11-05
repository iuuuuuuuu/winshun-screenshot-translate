param([Parameter(Mandatory)][string]$Plugin)
$ErrorActionPreference = 'Stop'
$data = Join-Path ([IO.Path]::GetTempPath()) ('screenshot-trigger-test-' + [guid]::NewGuid().ToString('N'))
$null = [IO.Directory]::CreateDirectory($data)
$server = [Net.Sockets.TcpListener]::new([Net.IPAddress]::Loopback, 0)
$server.Start()
$info = [Diagnostics.ProcessStartInfo]::new()
$info.FileName = $Plugin
$info.Arguments = '--input-test'
$info.UseShellExecute = $false
$info.CreateNoWindow = $true
$info.RedirectStandardInput = $true
$info.RedirectStandardOutput = $true
$info.RedirectStandardError = $true
$info.StandardInputEncoding = [Text.Encoding]::UTF8
$info.StandardOutputEncoding = [Text.Encoding]::UTF8
$info.StandardErrorEncoding = [Text.Encoding]::UTF8
$info.EnvironmentVariables['WS_DATA_DIR'] = $data
$process = [Diagnostics.Process]::Start($info)
$errors = $process.StandardError.ReadToEndAsync()
$script:nextRead = $null
function Send($method, $parameters, $id) {
    $message = @{jsonrpc='2.0';method=$method;params=$parameters}
    if ($null -ne $id) { $message.id = $id }
    $process.StandardInput.WriteLine((ConvertTo-Json $message -Depth 10 -Compress))
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
        if ($message.method -eq 'winshun/tool') { throw 'Test requested a real host tool' }
        if ("$($message.id)" -eq "$id") {
            if ($message.error) { throw 'Protocol error' }
            return $message.result
        }
    }
    throw 'Reply timed out'
}
function Barrier($id) {
    Send 'query' @{command='shot';text='';queryId="barrier-$id"} $id
    if ((Reply $id).items[0].id -ne 'hint-start') { throw 'Protocol barrier failed' }
}
$client = $null
try {
    Send 'initialize' @{
        settings=@{provider='openai';apiKey='test-only';endpoint="http://127.0.0.1:$($server.LocalEndpoint.Port)/chat/completions";shotHotkey='Alt+Shift+T';clipHotkey='Alt+Shift+C'}
        locale='en';capabilities=@{hostHotkeys=$true}
    } 1
    $null = Reply 1
    $clock = [Diagnostics.Stopwatch]::StartNew()
    $registration = $null
    while ($null -eq $registration -and $clock.ElapsedMilliseconds -lt 3000) {
        if ($null -eq $script:nextRead) { $script:nextRead = $process.StandardOutput.ReadLineAsync() }
        if (-not $script:nextRead.Wait(100)) { continue }
        $message = ConvertFrom-Json $script:nextRead.Result; $script:nextRead = $null
        if ($message.method -eq 'winshun/tool') {
            if ($message.params.name -ne 'register_hotkeys') { throw 'Unexpected host tool' }
            $registration = $message.id
        }
    }
    if ($null -eq $registration) { throw 'Fake hotkey registration request was missing' }
    foreach ($i in 1..20) { Send 'hotkey' @{key='Alt+Shift+T'} $null }
    Start-Sleep -Milliseconds 1500
    $process.StandardInput.WriteLine((ConvertTo-Json @{jsonrpc='2.0';id=$registration;result=@{}} -Compress))
    $process.StandardInput.Flush()
    Barrier 20
    Send 'query' @{command='clip';text='hello';queryId='blocking-query'} 2
    $clock = [Diagnostics.Stopwatch]::StartNew()
    while (-not $server.Pending() -and $clock.ElapsedMilliseconds -lt 5000) { Start-Sleep -Milliseconds 10 }
    if (-not $server.Pending()) { throw 'Background translation did not reach the local server' }
    $client = $server.AcceptTcpClient()
    $stream = $client.GetStream(); $stream.ReadTimeout = 3000
    $header = [Collections.Generic.List[byte]]::new()
    while ($true) {
        $b = $stream.ReadByte()
        if ($b -lt 0) { throw 'Unexpected HTTP EOF' }
        $header.Add([byte]$b)
        $n = $header.Count
        if ($n -ge 4 -and $header[$n-4] -eq 13 -and $header[$n-3] -eq 10 -and $header[$n-2] -eq 13 -and $header[$n-1] -eq 10) { break }
    }
    $headers = [Text.Encoding]::ASCII.GetString($header.ToArray())
    if ($headers -notmatch '(?im)^Content-Length: (\d+)') { throw 'Missing request length' }
    $remaining = [int]$Matches[1]
    $buffer = [byte[]]::new(4096)
    while ($remaining -gt 0) {
        $read = $stream.Read($buffer, 0, [Math]::Min($remaining, $buffer.Length))
        if ($read -eq 0) { throw 'Unexpected body EOF' }
        $remaining -= $read
    }
    # The --input-test executable never captures or reads the clipboard, even on a regression.
    foreach ($i in 1..100) {
        Send 'hotkey' @{key='Alt+Shift+T'} $null
        Send 'hotkey' @{key='Alt+Shift+C'} $null
    }
    Send 'action' @{action='start-region'} 3
    Send 'action' @{action='start-full'} 4
    Send 'action' @{action='retry-clip'} 5
    $body = [Text.Encoding]::UTF8.GetBytes('{"choices":[{"message":{"content":"translated"}}]}')
    $reply = [Text.Encoding]::ASCII.GetBytes("HTTP/1.1 200 OK`r`nContent-Type: application/json`r`nContent-Length: $($body.Length)`r`nConnection: close`r`n`r`n")
    $stream.Write($reply,0,$reply.Length); $stream.Write($body,0,$body.Length); $stream.Flush()
    $client.Dispose(); $client = $null
    if ((Reply 2).items[0].data.translated -ne 'translated') { throw 'Blocking query failed' }
    foreach ($id in 3..5) {
        if ((Reply $id).effects[0].type -ne 'keep') { throw 'Busy action was delayed and executed' }
    }
    Barrier 10
    Send 'hotkey' @{key='Alt+Shift+T'} $null
    Barrier 11
    Send 'action' @{action='start-region'} 12
    $null = Reply 12
    foreach ($i in 1..100) { Send 'hotkey' @{key='Alt+Shift+T'} $null }
    Barrier 13
    Send 'hotkey' @{key='Alt+Shift+T'} $null
    Barrier 14
    Send 'hotkey' @{key='obsolete-key'} $null
    Barrier 15
    Send 'shutdown' @{} 16
    $null = Reply 16
    $process.StandardInput.Close()
    if (-not $process.WaitForExit(3000) -or $process.ExitCode -ne 0) { throw 'Shutdown failed' }
    $log = $errors.Result
    if ([regex]::Matches($log, 'input-test: shot').Count -ne 3) { throw 'Queued or duplicate triggers executed after completion' }
    if ($log.Contains('input-test: text flow')) { throw 'Busy clipboard trigger executed later' }
    if ([regex]::Matches($log, 'kind=hotkey reason=busy').Count -lt 200) { throw 'Reader did not discard the busy hotkey burst' }
    if ([regex]::Matches($log, 'kind=hotkey reason=expired').Count -ne 20) { throw 'Old hotkeys survived a delayed host reply' }
    Write-Output 'PASS: 300 repeated hotkeys and 3 busy actions do not replay after HTTP or deferred capture'
    Write-Output 'PASS: 20 old hotkeys expire while waiting for a fake registration reply; host reply is preserved'
    Write-Output 'PASS: fresh hotkeys execute immediately after completion, obsolete bindings do not execute'
    Write-Output 'PASS: real protocol reader and local HTTP tested; no desktop capture, clipboard access, input simulation or real hotkey registration'
} finally {
    if ($null -ne $client) { $client.Dispose() }
    $server.Stop()
    if (-not $process.HasExited) { $process.Kill(); $process.WaitForExit() }
    $process.Dispose()
    [IO.Directory]::Delete($data)
}
