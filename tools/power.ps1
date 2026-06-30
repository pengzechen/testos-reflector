# OrangePi 5 Plus power control via USB relay on COM15
# Usage: .\power.ps1 restart | off | on
#
# "off" keeps the process alive holding BREAK — Ctrl+C or close the window to power back on.
# "on" is just a convenience to open+close the port (BREAK=false), in case the port got stuck.

param(
    [Parameter(Mandatory=$true)]
    [ValidateSet("restart","off","on")]
    [string]$Action
)

$port = New-Object System.IO.Ports.SerialPort "COM15", 9600
$port.Open()

switch ($Action) {
    "restart" {
        $port.BreakState = $true
        Start-Sleep -Seconds 1
        $port.BreakState = $false
        $port.Close()
        Write-Host "Power: restarted"
    }
    "off" {
        $port.BreakState = $true
        Write-Host "Power OFF - holding relay. Press Ctrl+C to power back on."
        try { while ($true) { Start-Sleep -Seconds 3600 } }
        finally { $port.BreakState = $false; $port.Close() }
    }
    "on" {
        $port.BreakState = $false
        $port.Close()
        Write-Host "Power: on"
    }
}
