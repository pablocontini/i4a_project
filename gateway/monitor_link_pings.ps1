#Requires -Version 5.1
<#
.SYNOPSIS
Monitor concurrent IPv4 ICMP probes through a verified Windows interface.
.DESCRIPTION
Checks the unrestricted Windows route and selected source address for all three
targets before and after each round. This script never changes network settings.
The .NET Ping API does not bind a source address; SourceAddress records the
Find-NetRoute selection, checked on both sides of each concurrent probe round.
.EXAMPLE
.\monitor_link_pings.ps1 -GatewayAddress 10.52.0.1 -ExpectedSourceAddress 10.52.0.3 -ValidateOnly
.EXAMPLE
.\monitor_link_pings.ps1 -GatewayAddress 10.52.0.1 -ExpectedSourceAddress 10.52.0.3 -DurationSeconds 180 -CsvPath .\link-pings.csv
#>
[CmdletBinding()]
param(
    [ValidateNotNullOrEmpty()]
    [string]$InterfaceAlias = 'Wi-Fi 3',
    [Parameter(Mandatory = $true)]
    [string]$GatewayAddress,
    [string]$RaspberryPiAddress = '10.255.255.254',
    [string]$InternetAddress = '1.1.1.1',
    [string]$ExpectedSourceAddress,
    [ValidateRange(1, 86400)]
    [int]$DurationSeconds = 120,
    [ValidateRange(100, 60000)]
    [int]$IntervalMilliseconds = 1000,
    [ValidateRange(1, 60000)]
    [int]$TimeoutMilliseconds = 800,
    [string]$CsvPath = (Join-Path $PWD ('link-pings-{0}.csv' -f (Get-Date -Format 'yyyyMMdd-HHmmss-fff'))),
    [switch]$ValidateOnly
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

function ConvertTo-Ipv4Literal {
    param([string]$Address)
    $parsed = $null
    if ($Address -notmatch '^\d{1,3}(\.\d{1,3}){3}$' -or
        -not [System.Net.IPAddress]::TryParse($Address, [ref]$parsed) -or
        $parsed.AddressFamily -ne [System.Net.Sockets.AddressFamily]::InterNetwork) {
        throw "An IPv4 literal is required; received '$Address'."
    }
    return $parsed.ToString()
}

function Get-VerifiedRoute {
    param(
        [string]$TargetAddress,
        [uint32]$InterfaceIndex,
        [string]$SourceAddress
    )
    # Do not constrain Find-NetRoute to an interface: check Windows' actual choice.
    $selection = @(Find-NetRoute -RemoteIPAddress $TargetAddress -ErrorAction Stop)
    $addresses = @($selection | Where-Object { $null -ne $_.PSObject.Properties['IPAddress'] })
    $routes = @($selection | Where-Object { $null -ne $_.PSObject.Properties['DestinationPrefix'] })
    if ($addresses.Count -ne 1 -or $routes.Count -ne 1) {
        throw "Cannot determine one source address and one selected route for $TargetAddress."
    }
    $address = $addresses[0]
    $route = $routes[0]
    if ([uint32]$address.InterfaceIndex -ne $InterfaceIndex -or
        [uint32]$route.InterfaceIndex -ne $InterfaceIndex) {
        throw ("Route to {0} uses source interface {1} and route interface {2}; expected '{3}' (index {4}). Prepare the test routes first." -f
            $TargetAddress, $address.InterfaceIndex, $route.InterfaceIndex, $InterfaceAlias, $InterfaceIndex)
    }
    $selectedSource = ConvertTo-Ipv4Literal $address.IPAddress
    if ($SourceAddress -and $selectedSource -ne $SourceAddress) {
        throw "Source IP changed for ${TargetAddress}: expected $SourceAddress, selected $selectedSource."
    }
    return [pscustomobject]@{
        TargetAddress = $TargetAddress
        SourceAddress = $selectedSource
        InterfaceAlias = $InterfaceAlias
        InterfaceIndex = $InterfaceIndex
        RoutePrefix = [string]$route.DestinationPrefix
        NextHop = [string]$route.NextHop
    }
}

function New-ProbeRow {
    param($Target, [int]$Sequence, [string]$Status, [string]$ErrorMessage = '')
    return [pscustomobject][ordered]@{
        SentAt = ''
        CompletedAt = ''
        ElapsedMilliseconds = ''
        Sequence = $Sequence
        Target = $Target.Name
        TargetAddress = $Target.Address
        Status = $Status
        RoundtripMilliseconds = ''
        SourceAddress = $stableSource
        InterfaceAlias = $InterfaceAlias
        InterfaceIndex = $interfaceIndex
        RoutePrefix = ''
        NextHop = ''
        RouteVerified = $false
        Error = $ErrorMessage
    }
}

function Write-ProbeRow {
    param($Row)
    $csvLines = @($Row | ConvertTo-Csv -NoTypeInformation)
    $writer.WriteLine($csvLines[1])
}

$adapter = @(Get-NetAdapter -Name $InterfaceAlias -ErrorAction Stop)
if ($adapter.Count -ne 1 -or $adapter[0].Name -ne $InterfaceAlias) {
    throw "Expected exactly one adapter named '$InterfaceAlias'."
}
$interfaceIndex = [uint32]$adapter[0].ifIndex
$targets = @(
    [pscustomobject]@{ Name = 'gateway'; Address = (ConvertTo-Ipv4Literal $GatewayAddress) },
    [pscustomobject]@{ Name = 'raspberry_pi'; Address = (ConvertTo-Ipv4Literal $RaspberryPiAddress) },
    [pscustomobject]@{ Name = 'internet'; Address = (ConvertTo-Ipv4Literal $InternetAddress) }
)
if (@($targets | ForEach-Object { $_.Address } | Select-Object -Unique).Count -ne 3) {
    throw 'The gateway, Raspberry Pi, and Internet target addresses must be distinct.'
}
$stableSource = ''
if ($ExpectedSourceAddress) {
    $stableSource = ConvertTo-Ipv4Literal $ExpectedSourceAddress
}
$initialRoutes = foreach ($target in $targets) {
    $selectedRoute = Get-VerifiedRoute $target.Address $interfaceIndex $stableSource
    if (-not $stableSource) {
        $stableSource = $selectedRoute.SourceAddress
    }
    $selectedRoute
}
if ($ValidateOnly) {
    $initialRoutes
    return
}

$outputPath = $ExecutionContext.SessionState.Path.GetUnresolvedProviderPathFromPSPath($CsvPath)
$parentDirectory = [System.IO.Path]::GetDirectoryName($outputPath)
if (-not [System.IO.Directory]::Exists($parentDirectory)) {
    throw "CSV output directory does not exist: $parentDirectory"
}
$writer = $null
$stream = $null
$clients = @()
$clock = [System.Diagnostics.Stopwatch]::new()
$sequence = 0
$statistics = @{}
try {
    # CreateNew preserves existing captures; share Read permits live CSV viewing.
    $stream = [System.IO.File]::Open($outputPath, [System.IO.FileMode]::CreateNew,
        [System.IO.FileAccess]::Write, [System.IO.FileShare]::Read)
    $writer = [System.IO.StreamWriter]::new($stream, [System.Text.UTF8Encoding]::new($false))
    $writer.AutoFlush = $true
    $header = @((New-ProbeRow $targets[0] 0 '') | ConvertTo-Csv -NoTypeInformation)[0]
    $writer.WriteLine($header)
    foreach ($target in $targets) {
        $clients += New-Object -TypeName System.Net.NetworkInformation.Ping
        $statistics[$target.Name] = @{ Total = 0; Success = 0 }
    }
    Write-Host "Monitoring '$InterfaceAlias' (index $interfaceIndex), source $stableSource. CSV: $outputPath"
    $clock.Start()
    $nextRoundMilliseconds = 0.0
    while ($clock.Elapsed.TotalSeconds -lt $DurationSeconds) {
        $waitMilliseconds = [int][Math]::Ceiling($nextRoundMilliseconds - $clock.Elapsed.TotalMilliseconds)
        if ($waitMilliseconds -gt 0) {
            $remainingMilliseconds = [int][Math]::Ceiling(($DurationSeconds * 1000) - $clock.Elapsed.TotalMilliseconds)
            Start-Sleep -Milliseconds ([Math]::Min($waitMilliseconds, [Math]::Max(1, $remainingMilliseconds)))
        }
        if ($clock.Elapsed.TotalSeconds -ge $DurationSeconds) { break }
        $sequence++
        $routesBefore = @()
        foreach ($target in $targets) {
            try {
                $routesBefore += Get-VerifiedRoute $target.Address $interfaceIndex $stableSource
            }
            catch {
                $row = New-ProbeRow $target $sequence 'RouteValidationFailed' $_.Exception.Message
                $row.CompletedAt = [DateTimeOffset]::Now.ToString('o')
                $row.ElapsedMilliseconds = [Math]::Round($clock.Elapsed.TotalMilliseconds, 3)
                Write-ProbeRow $row
                throw
            }
        }
        if ($clock.Elapsed.TotalSeconds -ge $DurationSeconds) { break }
        $roundStarted = $clock.Elapsed.TotalMilliseconds
        $probes = @()
        # Launch all three requests before waiting for any of them.
        for ($index = 0; $index -lt $targets.Count; $index++) {
            $row = New-ProbeRow $targets[$index] $sequence 'Pending'
            $row.SentAt = [DateTimeOffset]::Now.ToString('o')
            $row.ElapsedMilliseconds = [Math]::Round($clock.Elapsed.TotalMilliseconds, 3)
            $row.RoutePrefix = $routesBefore[$index].RoutePrefix
            $row.NextHop = $routesBefore[$index].NextHop
            $task = $null
            try {
                $task = $clients[$index].SendPingAsync([System.Net.IPAddress]::Parse($targets[$index].Address), $TimeoutMilliseconds)
            }
            catch {
                $row.Status = 'SendError'
                $row.Error = $_.Exception.Message
                $row.CompletedAt = [DateTimeOffset]::Now.ToString('o')
            }
            $probes += [pscustomobject]@{ Row = $row; Task = $task; Complete = ($null -eq $task) }
        }
        do {
            foreach ($probe in $probes) {
                if ($probe.Complete -or -not $probe.Task.IsCompleted) { continue }
                $probe.Row.CompletedAt = [DateTimeOffset]::Now.ToString('o')
                try {
                    $reply = $probe.Task.GetAwaiter().GetResult()
                    $probe.Row.Status = [string]$reply.Status
                    if ($reply.Status -eq [System.Net.NetworkInformation.IPStatus]::Success) {
                        $probe.Row.RoundtripMilliseconds = $reply.RoundtripTime
                    }
                }
                catch {
                    $probe.Row.Status = 'SendError'
                    $probe.Row.Error = $_.Exception.Message
                }
                $probe.Complete = $true
            }
            $pending = @($probes | Where-Object { -not $_.Complete }).Count
            if ($pending -gt 0) { Start-Sleep -Milliseconds 10 }
        } while ($pending -gt 0)

        $routeError = $null
        foreach ($target in $targets) {
            try {
                $null = Get-VerifiedRoute $target.Address $interfaceIndex $stableSource
            }
            catch {
                $routeError = $_
                break
            }
        }
        foreach ($probe in $probes) {
            $probe.Row.RouteVerified = ($null -eq $routeError)
            if ($null -ne $routeError) {
                $probe.Row.Error = ($probe.Row.Error + ' ' + $routeError.Exception.Message).Trim()
            }
            Write-ProbeRow $probe.Row
            $statistics[$probe.Row.Target].Total++
            if ($probe.Row.Status -eq 'Success' -and $probe.Row.RouteVerified) {
                $statistics[$probe.Row.Target].Success++
            }
        }
        if ($null -ne $routeError) { throw $routeError }
        # Slow rounds delay the next round; requests never overlap on one client.
        $nextRoundMilliseconds = $roundStarted + $IntervalMilliseconds
    }
}
finally {
    $clock.Stop()
    foreach ($client in $clients) { $client.Dispose() }
    if ($null -ne $writer) { $writer.Dispose() }
    elseif ($null -ne $stream) { $stream.Dispose() }
}
foreach ($target in $targets) {
    $stat = $statistics[$target.Name]
    Write-Host ("{0} ({1}): {2}/{3} successful verified probes" -f $target.Name, $target.Address, $stat.Success, $stat.Total)
}
Write-Host ("Finished after {0:N1}s. CSV: {1}" -f $clock.Elapsed.TotalSeconds, $outputPath)
