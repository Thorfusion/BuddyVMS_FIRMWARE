param(
    [Parameter(Mandatory = $true)]
    [string]$Port,
    [string]$Command = '$STATUS#',
    [ValidateRange(100, 60000)]
    [int]$ListenMs = 1000,
    [switch]$Monitor
)

$ErrorActionPreference = "Stop"
$serial = [IO.Ports.SerialPort]::new($Port, 19200,
    [IO.Ports.Parity]::None, 8, [IO.Ports.StopBits]::One)
$serial.Handshake = [IO.Ports.Handshake]::None
$serial.DtrEnable = $false
$serial.RtsEnable = $false
$serial.ReadTimeout = 100
$serial.WriteTimeout = 1000

try {
    $serial.Open()
    $serial.DiscardInBuffer()
    if ($Command.Length -ne 0) {
        $serial.Write($Command)
    }

    if ($Monitor) {
        Write-Host "Monitoring $Port at 19200 8N1; press Ctrl+C to stop."
        while ($true) {
            $text = $serial.ReadExisting()
            if ($text.Length -ne 0) {
                Write-Host -NoNewline $text
            }
            Start-Sleep -Milliseconds 20
        }
    } else {
        $timer = [Diagnostics.Stopwatch]::StartNew()
        while ($timer.ElapsedMilliseconds -lt $ListenMs) {
            $text = $serial.ReadExisting()
            if ($text.Length -ne 0) {
                Write-Host -NoNewline $text
            }
            Start-Sleep -Milliseconds 20
        }
    }
} finally {
    if ($serial.IsOpen) {
        $serial.Close()
    }
    $serial.Dispose()
}
