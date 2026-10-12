# Recuperación desde PC: 10 de octubre de 2026

PC conectada a ComNetAR - Ramona por Wi-Fi 3, índice 25, IP 10.52.0.3/14,
gateway 10.52.0.1. El Wi-Fi habitual sigue conectado por índice 4.
Se agregaron únicamente rutas /32 para 10.255.255.254 y 1.1.1.1 vía
10.52.0.1 en ActiveStore. No se cambió la ruta por defecto.

Preflight: gateway, RPi e internet respondieron 12/12 pings cada uno.
Monitor: tres solicitudes ICMP concurrentes, timeout 2000 ms; Windows
seleccionó Wi-Fi 3 y 10.52.0.3 antes/después de cada ronda. El CSV no es
una captura de paquetes: la IP proviene de Find-NetRoute.

Central de Ramona COM5; norte COM7. Imagen de las cinco placas:
0d2e3358034a239330fce2c52939047afbcc54f7b4939c03ceb1b243cfa3416c.
Topología previamente observada: root N–Ramona N; Ramona S–Grillo S;
Grillo E–root E. Los SSID/RSSI no bastan para confirmar la salud de la
sesión de ruteo durante todo el ensayo.

## Hechos observados

- Corte aplicado: 21:14:00.704 -03:00; `peer_lost` y `wifi_disconnected`
  registrados, WiFi detenido por 120 s; sin reinicio.
- Durante el corte: gateway Success 60/60; RPi TimedOut 60/60;
  internet TimedOut 60/60. No se observó recuperación por el redundante.
- Reactivación automática: 21:16:00.741 -03:00, inicialmente AP.
- Readmisión del principal: 21:16:11.802, unos 11.1 s tras reactivar;
  oferta original 10.32.0.0/11 admitida, transporte 10.32.0.2/30,
  gateway de transporte 10.32.0.1, SPI 192.168.0.10.
- Primer envío de ping exitoso después del corte: 21:16:12.977 para
  RPi e internet, 132.27 s después de cut_applied. No es el instante
  exacto de recepción del primer paquete en la red.
- La IP de la PC se mantuvo en 10.52.0.3 y las verificaciones de ruta
  fueron correctas en todas las rondas guardadas. Tras la readmisión
  ambos destinos continuaron respondiendo.

No atribuir estos timeouts únicamente a NAT, DNS o latencia: ICMP a la
RPi tampoco recuperó durante el corte. La captura se detuvo después de
observar la recuperación; se conservó el estado de los nodos.

## Archivos de evidencia (locales)

En build/firmware: pc-ping-baseline-20261010.csv,
pc-failover-20261010.csv, pc-test-north-20261010.log,
pc-test-cut-20261010.log. La captura de COM7 fue legible en este ensayo.

## Próximo diagnóstico

Comprobar salud/admisión real del enlace lateral, estado del token de
RS_ROUTING y el siguiente salto de Ramona, Grillo y la ruta de retorno
del root hacia 10.32.0.0/11. Distinguir la falta de transmisión desde
Ramona de una ruta de retorno que siga apuntando al norte del root.
Las trazas de turnos actuales pertenecen a discovery_manager, no al
token de ruteo. No modificar el algoritmo sin identificar ese estado.

Las rutas de Windows siguen temporales para continuar la investigación.
Ver [revisión del lateral y las rutas](revision-sesion-lateral-rutas-20261010.md)
para los defectos de entrega y las limitaciones de telemetría identificados.
Al terminar, retirarlas desde PowerShell como administrador con
gateway/set_test_routes.ps1 -Remove mientras Wi-Fi 3 siga conectado.
