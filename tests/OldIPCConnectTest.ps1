$pipeName = "gryphIPC-701c23d3-234a-486f-9185-af2824e4e5cc"

$pipe = [System.IO.Pipes.NamedPipeClientStream]::new(
    ".",
    $pipeName,
    [System.IO.Pipes.PipeDirection]::InOut
)

try {
    $pipe.Connect(2000)
    Write-Host "Test client connected"
}
catch {
    Write-Host "Connection rejected or unavailable: $($_.Exception.Message)"
}
finally {
    $pipe.Dispose()
}
