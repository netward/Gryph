# Найти запущенный GUI.
$gui = @(Get-CimInstance Win32_Process -Filter "Name = 'Gryph.exe'")

if ($gui.Count -ne 1) {
    throw "Expected exactly one Gryph.exe. Found: $($gui.Count)"
}

$guiPid = [uint32]$gui[0].ProcessId

# Ищем только GryphCore, запущенный этим экземпляром GUI.
function Get-GryphCoreChild {
    @(
        Get-CimInstance Win32_Process -Filter "Name = 'GryphCore.exe'" |
        Where-Object { $_.ParentProcessId -eq $guiPid }
    )
}

$first = @(Get-GryphCoreChild)

if ($first.Count -ne 1) {
    throw "Expected exactly one child GryphCore. Found: $($first.Count)"
}

Write-Host "First core PID: $($first[0].ProcessId)"

# Первое аварийное завершение.
$firstCrashTime = Get-Date
Stop-Process -Id $first[0].ProcessId -Force -ErrorAction Stop

# Дождаться нового процесса, не более 6 секунд.
do {
    Start-Sleep -Milliseconds 100

    $second = @(
        Get-GryphCoreChild |
        Where-Object { $_.ProcessId -ne $first[0].ProcessId }
    )

} while (
    $second.Count -ne 1 -and
    ((Get-Date) - $firstCrashTime).TotalSeconds -lt 6
)

if ($second.Count -ne 1) {
    throw "Core was not restarted within 6 seconds"
}

Write-Host "Second core PID: $($second[0].ProcessId)"

Start-Sleep -Seconds 1

if (((Get-Date) - $firstCrashTime).TotalSeconds -ge 10) {
    throw "10-second crash-loop window elapsed. Repeat the test."
}

# Второе аварийное завершение.
Stop-Process -Id $second[0].ProcessId -Force -ErrorAction Stop

Start-Sleep -Seconds 3

$remaining = @(Get-GryphCoreChild)

Write-Host "Remaining GryphCore processes: $($remaining.Count)"
