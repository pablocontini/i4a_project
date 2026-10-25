param([ValidateSet('Inspect','Set','Restore')][string]$Action = 'Inspect')
$ErrorActionPreference = 'Stop'
$adapterName = 'Wi-Fi 2'
$testAddress = '10.116.0.250'
$testGateway = '10.116.0.1'
$backupPath = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../app/build/wifi2-link-test-backup.json'))
$adapter = Get-NetAdapter -Name $adapterName

if ($Action -eq 'Inspect') {
    Get-NetIPConfiguration -InterfaceAlias $adapterName
    return
}
$principal = [Security.Principal.WindowsPrincipal]::new([Security.Principal.WindowsIdentity]::GetCurrent())
if (-not $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) {
    throw 'Windows requiere PowerShell como Administrador para cambiar IP/rutas. No se modifico la configuracion.'
}

function Restore-Original {
    if (-not (Test-Path -LiteralPath $backupPath)) { throw 'No existe el respaldo del ensayo.' }
    $saved = Get-Content -LiteralPath $backupPath -Raw | ConvertFrom-Json
    if ([string]$adapter.InterfaceGuid -ne $saved.guid) { throw 'El adaptador no coincide con el respaldo.' }
    foreach ($prefix in $saved.routes) {
        Get-NetRoute -InterfaceIndex $adapter.ifIndex -DestinationPrefix $prefix -ErrorAction SilentlyContinue |
            Where-Object NextHop -eq $saved.gateway |
            Remove-NetRoute -Confirm:$false
    }
    Get-NetIPAddress -InterfaceIndex $adapter.ifIndex -IPAddress $saved.address -ErrorAction SilentlyContinue |
        Remove-NetIPAddress -Confirm:$false
    Set-NetIPInterface -InterfaceIndex $adapter.ifIndex -AddressFamily IPv4 -InterfaceMetric $saved.metric -PolicyStore ActiveStore
    Set-NetIPInterface -InterfaceIndex $adapter.ifIndex -AddressFamily IPv4 -AutomaticMetric $saved.automaticMetric -Dhcp $saved.dhcp -PolicyStore ActiveStore
    Remove-Item -LiteralPath $backupPath
    Write-Output 'Configuracion original de Wi-Fi 2 restaurada. DNS no fue modificado.'
}

if ($Action -eq 'Restore') { Restore-Original; return }
if (Test-Path -LiteralPath $backupPath) { throw 'Ya hay un respaldo: usar -Action Restore antes de repetir Set.' }
if ($adapter.Status -ne 'Up') { throw 'Conectar Wi-Fi 2 a Grillo antes de configurar.' }
$existing = @(Get-NetIPAddress -InterfaceIndex $adapter.ifIndex -AddressFamily IPv4 |
    Where-Object { $_.IPAddress -notlike '169.254.*' -and $_.IPAddress -ne '0.0.0.0' })
if ($existing.Count) { throw 'Wi-Fi 2 ya tiene IPv4; revisar la referencia sin sustituir su configuracion.' }
$prefixes = @('0.0.0.0/0','10.255.255.254/32','1.1.1.1/32')
foreach ($prefix in $prefixes) {
    if (Get-NetRoute -InterfaceIndex $adapter.ifIndex -DestinationPrefix $prefix -ErrorAction SilentlyContinue) {
        throw "Wi-Fi 2 ya tiene la ruta $prefix; no se sustituye automaticamente."
    }
}
$ipif = Get-NetIPInterface -InterfaceIndex $adapter.ifIndex -AddressFamily IPv4
$saved = @{guid=[string]$adapter.InterfaceGuid; address=$testAddress; gateway=$testGateway;
    dhcp=[string]$ipif.Dhcp; metric=$ipif.InterfaceMetric; automaticMetric=[string]$ipif.AutomaticMetric;
    routes=$prefixes; created_utc=[DateTime]::UtcNow.ToString('o')}
$saved | ConvertTo-Json | Set-Content -LiteralPath $backupPath -Encoding UTF8
try {
    Set-NetIPInterface -InterfaceIndex $adapter.ifIndex -AddressFamily IPv4 -Dhcp Disabled -AutomaticMetric Disabled -InterfaceMetric 5000 -PolicyStore ActiveStore
    New-NetIPAddress -InterfaceIndex $adapter.ifIndex -IPAddress $testAddress -PrefixLength 14 -AddressFamily IPv4 -PolicyStore ActiveStore | Out-Null
    foreach ($prefix in $prefixes) {
        New-NetRoute -InterfaceIndex $adapter.ifIndex -DestinationPrefix $prefix -NextHop $testGateway -RouteMetric 1 -PolicyStore ActiveStore | Out-Null
    }
    Write-Output 'Ensayo: Wi-Fi 2 = 10.116.0.250/14, gateway 10.116.0.1; RPi e ICMP Internet por esta interfaz.'
    Write-Output 'Al terminar ejecutar el mismo script con -Action Restore como Administrador.'
    Get-NetIPConfiguration -InterfaceAlias $adapterName
} catch {
    $setupError = $_
    Restore-Original
    throw $setupError
}
