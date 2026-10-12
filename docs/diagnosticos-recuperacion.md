# Diagnósticos de recuperación

Imagen: `redundant-link-20261009-link-diag`. Instalar la misma imagen en
las cinco placas de Ramona antes de repetir el corte.

Los registros `I4ALINK` incluyen tiempo monotónico de la placa (`t_ms`) y
orientación (`role`: 0 norte, 1 sur, 2 este, 3 oeste, 4 central). Funcionan
con el nivel normal de logs desactivado. No registran contraseñas.
Se imprimen en el USB de la placa que genera el evento, no se retransmiten
al central. Las peticiones/denegaciones de turno se limitan a una traza
cada 30 segundos; cada concesión y liberación se registra.

Eventos: `cut_begin`, `cut_applied` (WiFi ya detenido y plazo de restauración),
`radio_restored` (AP reactivado; no implica asociación), `search_scheduled`,
`turn_request`, `turn_not_granted`, `turn_granted`, `turn_expired_or_revoked`,
`turn_release`, `scan_selected`, `scan_no_candidate`, `wifi_disconnected`,
`got_ip`, `static_transport_ip`, `offer_decision`, `handshake_rejected`,
`peer_admitted`, `peer_lost`, `telemetry_transport`.

Las ofertas y bloques lógicos aparecen en hexadecimal de orden host.
Los rechazos identifican oferta cero, prefijo/capacidad inválida, oferta
no canónica, bloque local inválido, prefijo no mayor o base no mayor.
La regla de admisión y sus excepciones se conservan.

## Dirección 192.168.0.10

`components/internal/config/config.c` fija la IP TX SPI en
`192.168.0.(orientación + 10)`, por lo que el norte usa `192.168.0.10`.
La telemetría UDP de `info_manager` no hace bind a una IP de origen.
La aparición de esa IP durante la caída es compatible con selección de la
interfaz SPI como origen al salir por el redundante. Es una inferencia del
código y de la captura; confirmar con las nuevas trazas y tcpdump.
No demuestra modificación de las subredes lógicas ni de la IP del teléfono.
`telemetry_transport` registra ambas IP configuradas, pero no promete medir
el origen de cada paquete: la captura de la RPi es la evidencia del origen real.

## Captura de la prueba

Conectar simultáneamente el central y el norte por USB. Identificar sus COM.
En una consola capturar el norte (ejemplo COM5):

```powershell
python gateway/capture_link.py --port COM5 --seconds 900 --output build/firmware/north-recovery-diag.log
```

En otra consola ordenar el corte por el central (ejemplo COM6):

```powershell
python gateway/cut_link.py --port COM6 --role north --seconds 120 --verbose
```

Mantener teléfono en Ramona, datos móviles apagados, anotar IP antes y
después, y hacer pings continuos a gateway, RPi e internet. No reconectar
manualmente. En la RPi capturar UDP 8000 e ICMP. Correlacionar hora del
host de capture_link con tcpdump; usar t_ms para duraciones de una misma
placa. Medir desde cut_applied hasta reanudación de respuestas y desde
radio_restored hasta peer_admitted. No confundir ACK de cola con corte real.

La captura no reinicia ni transmite órdenes. No usar el mismo COM para
capture_link y cut_link a la vez. Un nuevo intento de búsqueda puede esperar
600–700 segundos después de un turno fallido; los registros deben permitir
distinguir eso de ausencia de candidatos o rechazo del handshake.
