# Windows failover ping monitor

Run from Windows PowerShell 5.1 or newer after the temporary test routes are in
place. The monitor uses the existing `Wi-Fi 3` adapter and never changes routes,
metrics, adapter state, or addresses. It needs no administrator privileges.

Check the actual Windows route to each target first:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\gateway\monitor_link_pings.ps1 -GatewayAddress 10.52.0.1 -ExpectedSourceAddress 10.52.0.3 -ValidateOnly
```

Capture the gateway, Raspberry Pi, and Internet probes concurrently:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\gateway\monitor_link_pings.ps1 -GatewayAddress 10.52.0.1 -ExpectedSourceAddress 10.52.0.3 -DurationSeconds 180 -IntervalMilliseconds 1000 -TimeoutMilliseconds 800 -CsvPath .\link-pings.csv
```

`-RaspberryPiAddress` defaults to `10.255.255.254`; `-InternetAddress` defaults
to `1.1.1.1`. Use a new CSV filename for each capture. The parent output
directory must already exist. `Ctrl+C` stops the capture; completed rounds are
flushed to disk.

Before and after every round, unrestricted `Find-NetRoute` must select `Wi-Fi 3`
for both the source address and the route. All targets must keep the same source
IPv4 for the whole capture; `-ExpectedSourceAddress` also checks that IP at
startup. A wrong route or changed IP terminates the monitor with an error.
Ordinary ICMP timeouts and unreachable responses are logged independently for
each destination and do not stop the capture.

The CSV includes ISO 8601 wall-clock send/completion times with UTC offsets,
monotonic elapsed milliseconds, target, ICMP status, successful round-trip time,
selected source IP, interface, route, and validation result. Source IP is the
address selected by Windows, not a packet-capture measurement: .NET `Ping`
cannot bind an interface/source address. Read results only with
`RouteVerified=True`; a route change during an in-flight round marks all its
rows false and stops further probes. A transient route change that occurs and
reverts entirely between the route checks cannot be detected by these checks.

The duration limits new sends. The final in-flight round may finish after that
duration by its ICMP timeout plus route-check overhead. Route checks also add
overhead to the requested interval; actual send timestamps record that timing.
