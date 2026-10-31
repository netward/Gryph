param(
    [switch]$KnownDefects,
    [switch]$Race
)

$ErrorActionPreference = 'Stop'

$projectRoot = (
    Resolve-Path (Join-Path $PSScriptRoot '..')
).Path

$coreDir = Join-Path $projectRoot 'core\server'
$genDir = Join-Path $coreDir 'gen'

foreach ($tool in @(
    'go',
    'protoc',
    'protoc-gen-go',
    'protoc-gen-go-grpc'
)) {
    if (-not (Get-Command $tool -ErrorAction SilentlyContinue)) {
        throw "Missing $tool in PATH"
    }
}

Write-Host "[PR-0] Go: $(go version)"
Write-Host "[PR-0] Generating protobuf"

Push-Location $genDir

try {
    & protoc -I . --go_out=. --go-grpc_out=. libcore.proto

    if ($LASTEXITCODE -ne 0) {
        throw 'protoc failed'
    }
}
finally {
    Pop-Location
}

$tags = @(
    'with_clash_api'
    'with_gvisor'
    'with_quic'
    'with_wireguard'
    'with_utls'
    'with_dhcp'
    'with_tailscale'
    'badlinkname'
    'tfogo_checklinkname0'
    'with_purego'
    'http2legacy'
) -join ','

$oldValue = [Environment]::GetEnvironmentVariable(
    'GRYPH_KNOWN_DEFECTS',
    'Process'
)

Push-Location $coreDir

try {
    if ($KnownDefects) {
        $env:GRYPH_KNOWN_DEFECTS = '1'
    }
    else {
        Remove-Item Env:GRYPH_KNOWN_DEFECTS `
            -ErrorAction SilentlyContinue
    }

    $goArgs = @(
        'test'
        '-count=10'
        '-timeout=10m'
        '-tags'
        $tags
        '-ldflags=-checklinkname=0'
    )

    if ($Race) {
        $goArgs += '-race'
    }

    
    if ($KnownDefects) {
        $goArgs += '-run'
        $goArgs += '^TestKnownDefect'
        $goArgs += '.'
    }
    else {
        # Integration tests that require a dedicated
        # disposable Windows network adapter named winipcfg_test.
        # Some of these tests modify IP, MTU, routes and DNS.
    
        $skipWinipcfgTests = '^(TestIPChangeMetric|TestIPChangeMTU|TestGetIfRow|TestAddDeleteIPAddress|TestAddDeleteRoute|TestFlushDNS|TestSetDNS)$'
    
        $goArgs += '-skip'
        $goArgs += $skipWinipcfgTests
        $goArgs += './...'
    }


    & go @goArgs

    if ($LASTEXITCODE -ne 0) {
        throw "Go tests failed: $LASTEXITCODE"
    }
}
finally {
    [Environment]::SetEnvironmentVariable(
        'GRYPH_KNOWN_DEFECTS',
        $oldValue,
        'Process'
    )

    Pop-Location
}