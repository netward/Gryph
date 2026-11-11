$gui = @(Get-CimInstance Win32_Process -Filter "Name = 'Gryph.exe'")

if ($gui.Count -ne 1) {
    throw "Expected one Gryph.exe"
}

$guiPid = [uint32]$gui[0].ProcessId

$core = @(
    Get-CimInstance Win32_Process -Filter "Name = 'GryphCore.exe'" |
    Where-Object { $_.ParentProcessId -eq $guiPid }
)

if ($core.Count -ne 1) {
    throw "Expected one child GryphCore.exe"
}

Stop-Process -Id $core[0].ProcessId -Force