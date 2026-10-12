# Coordinación v2 y diagnósticos de ruteo

Implementación preparada el 10/10/2026 y revisada el 11/10/2026.
Ramona se actualizó por OTA el 11/10/2026: las cinco placas confirmaron
la imagen 05f036c4..., ready=1, boot_paused=0 y sin recuperación pendiente.
Grillo también se actualizó el 11/10/2026: las cinco placas confirmaron
aa89562c..., ready=1, boot_paused=0 y sin recuperación pendiente.
El root también se actualizó el 11/10/2026: sus cinco placas confirmaron
aa89562c..., ready=1, boot_paused=0 y sin recuperación pendiente.
Los tres nodos quedaron actualizados. Registros locales:
build/firmware/coord-v2-ramona-ota.log, coord-v2-grillo-ota-retry.log
y coord-v2-root-ota.log.
El primer intento de Grillo se canceló antes de SELECT por un error de
codificación de la consola; todas las placas confirmaron ABORT. El reintento
con salida UTF-8 terminó correctamente. El CLI ahora configura esa salida.

## Cambios

- Las colas de ruteo usan un mutex y lotes separados para ejecutar eventos.
  Los eventos que llegan durante un turno quedan pendientes para el siguiente.
  Una cola pendiente vuelve a solicitar turno cada tres segundos, incluso si
  todavía no se procesó la desconexión y no hay un timer de recuperación activo.
- El token identifica sesión de arranque y secuencia. El central reintenta la
  misma circulación cada cinco segundos. Los periféricos que ya completaron
  ese turno reenvían el token sin repetir su callback; rechazan secuencias
  anteriores y sesiones ajenas. Las solicitudes concurrentes se agrupan.
- Una tabla cuya publicación falla conserva el turno pendiente. El callback
  de ruteo vuelve a publicar antes de consumir más eventos. Las tablas llevan
  versión, sesión, secuencia y propietario para descartar actualizaciones
  antiguas. Los cambios de un mismo propietario/turno siguen el orden FIFO SPI.
- El servidor TCP conserva su tarea de supervisión y reintenta cada dos
  segundos ante fallos de red/socket/bind/listen/select. Un cierre solicitado
  detiene los reintentos y termina la tarea.
- La tabla continúa teniendo un único gateway activo: no se agregaron
  prioridades ni una lista de gateways de respaldo. La selección alternativa
  conserva el mecanismo de búsqueda de gateway existente.

La sesión del token se establece durante el arranque del nodo completo. El
reinicio coordinado existente reinicia central y periféricos; no se admite
cambiar la sesión mediante un grant ajeno. Un callback que no termina no se
revoca para fabricar otro token: las trazas permiten diagnosticar ese bloqueo
sin crear dos secciones críticas simultáneas.

## Diagnósticos

`I4AROUTE` incluye tiempo monotónico de la placa y etiquetas sync, routing,
gateway, shared_state, server y session. Se encola sin espera y un worker
separado escribe UART; una cola llena informa descartes. No se bloquea el
callback SPI esperando la impresión de estos mensajes.

Eventos relevantes: peer_queued, peer_dispatch, loss_processed, grant_send,
grant_forward, circulation_done, completion_deferred, table_publish,
table_received, route_add, route_remove_output, table_state, request_send,
request_received, response_received, root_response_scheduled,
root_response_publish, listener_ready, listener_retry, listener_stopped,
session_decision, admitted, renewed, renew_failed y released.

`table_state` informa el gateway y las salidas hacia RPi, 1.1.1.1 y el cliente
de esta prueba 10.52.0.3. Las orientaciones de ruteo son N=1, S=2, E=3, W=4,
C=5; los roles GPIO de I4ALINK/I4AOTA son 0–4.

La telemetría agrega peer_admitted y sample_age_ms. peer_admitted describe
una sesión inter-nodos aceptada con reserva activa; no las conexiones de
usuarios al hotspot central. Las filas se actualizan cada 30 segundos y el
emisor UDP conserva su intervalo de un minuto. sample_age_ms es la edad de
recepción local de cada muestra; -1 significa que aún no existe una muestra.

## Pruebas realizadas

- 37 pruebas Python aprobadas.
- Simulación con el código real de sync/shared_state: pérdida en cada uno
  de los cinco saltos, entrega parcial, grants duplicados/antiguos, tabla
  fallida que retiene el turno, agrupación de solicitudes y mantenimiento OTA.
- Prueba del servidor real: primer bind falla, el siguiente listener arranca
  y una solicitud de cierre termina la tarea correctamente.
- Suite nativa de OTA/protocolo aprobada.
- Las suites de recuperación y hooks compilaron, pero Windows bloqueó su
  carga con WinError 4551 (Control de aplicaciones). Falta ejecutarlas en
  Linux antes de instalar. Se agregó un caso de pérdida de solicitud con
  PEER_LOST pendiente sin timer activo.

El 11/10/2026 el usuario ejecutó el paquete corregido en la RPi: aprobaron
la simulación de coordinación, el reintento del servidor, las 14 pruebas
de recuperación y la prueba del hook de ruta sin salida. La validación
física de conmutación y retorno todavía está pendiente.

## Paquetes compilados

| Nodo | Carpeta | Versión | Hash ELF |
| --- | --- | --- | --- |
| Ramona | firmware_bundle_coord_v2_ramona_20261010 | coord-v2-20261010-ramona | 05f036c4372ae9f4b11513f63b59693819f4e8df9ef1917c1f8d6aa36da5ca60 |
| Grillo y root | firmware_bundle_coord_v2_grillo_root_20261010 | coord-v2-20261010-grillo | aa89562c87ddaaf594ab2541ca979c5faa56159f2301e713040ecc1d369a703d |

El root determina su función y SSID por el modo de hardware y puede usar
la imagen de Grillo. Las imágenes mantienen las configuraciones persistidas.
Actualizar las cinco placas de cada nodo juntas: SYNC y SHARED_STATE internos
usan versión 2 y no se deben mezclar imágenes viejas/nuevas dentro de un nodo.
Para validar la recuperación completa, actualizar Ramona, Grillo y root.

Las imágenes caben en las particiones existentes y no requieren borrar NVS.
La compilación usa I4A_HOUSE_SSID y PROJECT_VER explícitos. build/firmware
contiene la última variante compilada (Grillo/root); para Ramona usar su
paquete exportado, no ese main.bin genérico.

## Validación en la RPi

Paquete en Downloads: i4a_validacion_coord_v2_20261011.zip.
Desde PowerShell, conservar el signo ':' que hace remoto el destino:

```powershell
& "$env:WINDIR\System32\OpenSSH\scp.exe" "$env:USERPROFILE\Downloads\i4a_validacion_coord_v2_20261011.zip" 'rpi@192.168.0.114:/home/rpi/'
```

En la consola de la RPi, son dos comandos separados:

```bash
unzip -o ~/i4a_validacion_coord_v2_20261011.zip -d ~
sh ~/i4a_coord_validation/run_validation.sh
```

Si falta el compilador cc, resolver esa dependencia antes de ejecutar la suite.
No se pide desactivar Control de aplicaciones de Windows.
