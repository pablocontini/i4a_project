[CmdletBinding()]
param(
    [string]$InterfaceAlias = 'Wi-Fi 3',
    [switch]$Remove
)
$ErrorActionPreference = 'Stop'
$cfg = Get-NetIPConfiguration -InterfaceAlias $InterfaceAlias
$address = @($cfg.IPv4Address | Where-Object { $_.IPAddress -like '10.*' })
$gateway = $cfg.IPv4DefaultGateway.NextHop
if ($address.Count -ne 1 -or -not $gateway -or $gateway -notlike '10.*') {
    throw 'El adaptador debe estar conectado a Ramona y tener una IP y gateway 10.x.'
}
$index = $cfg.InterfaceIndex
foreach ($target in @('10.255.255.254/32', '1.1.1.1/32')) {
    $routes = @(Get-NetRoute -AddressFamily IPv4 -DestinationPrefix $target -PolicyStore ActiveStore -ErrorAction SilentlyContinue)
    if ($Remove) {
        $routes | Where-Object { $_.InterfaceIndex -eq $index -and $_.NextHop -eq $gateway -and $_.RouteMetric -eq 1 } |
            Remove-NetRoute -Confirm:$false
        Write-Host "Ruta de prueba retirada: $target"
        continue
    }
    if ($routes.Count -gt 0) {
        if (@($routes | Where-Object { $_.InterfaceIndex -ne $index -or $_.NextHop -ne $gateway -or $_.RouteMetric -ne 1 }).Count -gt 0) {
            throw "Ya existe una ruta distinta para $target; no se modifico."
        }
    } else {
        New-NetRoute -DestinationPrefix $target -InterfaceIndex $index -NextHop $gateway -RouteMetric 1 -PolicyStore ActiveStore | Out-Null
    }
    Write-Host "$target via $gateway por $InterfaceAlias (IP $($address[0].IPAddress))"
}
Write-Host 'La ruta por defecto no se modifica. Estas rutas duran hasta retirarlas o reiniciar Windows.'
