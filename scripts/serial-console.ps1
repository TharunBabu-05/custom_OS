[CmdletBinding()]
param(
    [ValidatePattern('^COM[1-9][0-9]*$')][string]$Port='COM3',
    [ValidateRange(1200,1000000)][int]$Baud=115200,
    [ValidateRange(1,100)][int]$CharacterDelayMs=10,
    [string]$Log
)

$ErrorActionPreference='Stop'
if ([Console]::IsInputRedirected) { throw 'Open this script in an interactive PowerShell or Windows Terminal tab.' }
$serial=New-Object System.IO.Ports.SerialPort($Port,$Baud,'None',8,'One')
$serial.Handshake=[IO.Ports.Handshake]::None
$serial.WriteTimeout=1000
$control=[Console]::TreatControlCAsInput
$capture=$null
$byte=New-Object byte[] 1
try {
    $serial.Open()
    if ($Log) {
        $capture=[IO.File]::AppendText($ExecutionContext.SessionState.Path.GetUnresolvedProviderPathFromPSPath($Log))
        $capture.AutoFlush=$true
    }
    [Console]::TreatControlCAsInput=$true
    Write-Host "$Port at $Baud baud. Paste one line at a time. Ctrl+] closes."
    while ($true) {
        $received=$serial.ReadExisting()
        if ($received) {
            [Console]::Write($received)
            if ($capture) { $capture.Write($received) }
        }
        if (![Console]::KeyAvailable) {
            [Threading.Thread]::Sleep(5)
            continue
        }
        $key=[Console]::ReadKey($true)
        $character=[int]$key.KeyChar
        if ($character -eq 29) { break }
        if ($key.Key -eq [ConsoleKey]::Enter) { $character=13 }
        if ($character -in 3,4,8,9,13,21,127 -or ($character -ge 32 -and $character -le 126)) {
            $byte[0]=[byte]$character
            $serial.Write($byte,0,1)
            [Threading.Thread]::Sleep($CharacterDelayMs)
        }
    }
} finally {
    [Console]::TreatControlCAsInput=$control
    if ($capture) { $capture.Dispose() }
    $serial.Dispose()
}
