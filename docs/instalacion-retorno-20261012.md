# Instalación de la corrección de retorno — 12 de octubre de 2026

Se conserva el criterio de aceptación de enlaces y el formato del protocolo.
Ante la pérdida de un enlace en una radio provisionada con DTR mayor que 1,
se inicia la convergencia HAG aunque esa radio ya no sea el gateway activo.
Root (DTR=1) mantiene su salida por el central. La convergencia puede causar
una interrupción breve también al perder un enlace no predeterminado.

Validación: 15 pruebas C de recuperación y 37 pruebas Python aprobadas;
ambas variantes compiladas con ESP-IDF 5.4.1. Tiempos rápidos activados.
La prueba física de consistencia con estas imágenes todavía está pendiente.

| Nodo | Puerto central | Imagen | Hash ELF |
| --- | --- | --- | --- |
| Ramona | COM5 | dev-return-20261012-ramona | 05d746fdc40e93453238ff6d4fd9adfba499703ececf7ccf4bf466ffd9c17aff |
| Grillo | COM8 | dev-return-20261012-grillo | 02d5dd03ee1dd866b83ee8f7563e828a5773fbc710ab41b381454610c713fde2 |
| Root | COM9 | dev-return-20261012-grillo | 02d5dd03ee1dd866b83ee8f7563e828a5773fbc710ab41b381454610c713fde2 |

Root usa la variante común de Grillo; su rol conserva Internet4All_Root.
Paquetes: firmware_bundle_return_ramona_20261012 y
firmware_bundle_return_grillo_root_20261012.
Logs: build/firmware/return-<nodo>-ota.log.
Reportes finales: build/firmware/return-<nodo>-installed.json.
El estado final de cada placa debe ser ready=1, boot_paused=0,
pending_verify=0 y recovery_pending=0. Un ACK de transferencia no basta.
Instalación final: las cinco placas de Ramona, Grillo y Root reportaron
los hashes esperados, listas y sin pausa ni recuperación pendiente.
Tras los reinicios, Wi-Fi 3 sigue conectado a ComNetAR - Ramona pero
obtuvo 10.116.0.2. Las rutas específicas todavía apuntan al gateway
anterior 10.52.0.1; la referencia y los cortes deben esperar su reposición.
No interpretar el ping aislado a internet como validación del recorrido.
## Pruebas físicas con la corrección instalada

Topología del usuario, 09:30: Root Este–Ramona Este; Root Oeste–Grillo Oeste;
Ramona Oeste–Grillo Este. Todos admitidos. PC 10.116.0.2/14, gateway
10.116.0.1; rutas /32 verificadas por Wi-Fi 3. Referencia gateway/RPi/internet
5/5 cada uno. No hubo nueva captura RPi simultánea en estos dos ensayos:
se usaron pings PC y consolas centrales de los tres nodos.

| Ensayo | Corte | Gateway | RPi | Internet | Intervalo observado hasta volver las respuestas |
| --- | --- | --- | --- | --- | --- |
| 1 | Este Ramona, 60 s | 143/143 | 134/143 | 134/143 | ~11,3 s desde primer timeout |
| 2 | Este Ramona, 60 s | 137/137 | 130/137 | 130/137 | ~9,9 s desde primer timeout |

En ambos, las respuestas remotas volvieron antes del vencimiento del corte.
Ramona seleccionó Oeste (output=4) y Root reconstruyó 10.96.0.0/11 por
Oeste hacia Grillo (route_entry network=0a600000 mask=ffe00000 output=4).
En el segundo ensayo el default ya era Oeste y la ruta RPi era Este antes
de la pérdida: cubre el caso que fallaba antes de la corrección.
La PC conservó 10.116.0.2 y el hotspot no dejó de responder.
Los tiempos son intervalos de las muestras ICMP, no tiempos exactos del
instante físico del corte. La restauración de Este tras el primer ensayo
se observó mediante rpi_out=3 con default=4. Tras el segundo, la última
muestra aún usa Oeste; falta confirmar la nueva admisión de Este por
telemetría. No se afirma retorno automático del default al enlace inicial.

Evidencia: build/firmware/return-cut{1,2}-pings.csv,
return-cut{1,2}-command.log, return-cut{1,2}-{center,grillo,root}.raw.
Resultado: dos recuperaciones consecutivas de ida y retorno aprobadas en
esta topología. Quedan prueba inversa cortando el uplink de Grillo,
repeticiones adicionales y restaurar tiempos normales al finalizar desarrollo.
## Confirmación de reconexión y prueba inversa

Telemetría compartida 09:39–09:40 confirma Ramona Este y Root Este con
peer_admitted=1; permanecen admitidos los otros dos enlaces del triángulo.
Subredes conservadas: Ramona 10.116.0.0/14, Grillo 10.148.0.0/14.

Wi-Fi 3 cambió automáticamente a Ramona durante la preparación: se abortó
la referencia, sin aplicar corte. Se configuró I4A-Ramona-Prueba en modo
manual y se reconectó a Grillo. El usuario repuso rutas /32 por 10.148.0.1.
Perfil de Ramona permanece manual durante las pruebas; PC queda en Grillo.
Referencia válida: gateway, RPi e internet 5/5, IP 10.148.0.2.

Corte inverso de Oeste de Grillo por 60 segundos desde COM8:
- Gateway 148/148; RPi 139/148; internet 139/148.
- Primer timeout 09:45:57.678; respuestas RPi/internet volvieron
  09:46:08.893/08.983: intervalo observado de aproximadamente 11,2–11,3 s.
- Recuperación antes del vencimiento del corte, Grillo default=3 (Este)
  y Root ruta 10.128.0.0/11 por output=3 (Este, hacia Ramona).
- PC conservó 10.148.0.2. No nueva captura RPi simultánea en este ensayo.
- La última tabla de Grillo sigue por Este; no confirma reconexión de Oeste.

Evidencia: build/firmware/return-inverse-pings.csv, return-inverse-command.log,
return-inverse-{ramona,grillo,root}.raw. Tres ensayos recientes aprobaron
recuperación por el alternativo (dos Ramona, uno Grillo), con pérdidas de
transición. No equivalen a validación exhaustiva ni prueba de velocidad.
## Actualización final, repetición inversa 09:52–09:54

La telemetría del usuario 09:51:14–09:51:31 confirma Oeste de Grillo y
Oeste de Root admitidos en ambos extremos. Las subredes siguen iguales.
Se repitió Oeste de Grillo por 60 segundos con default ya por Este:
gateway 133/133, RPi 126/133, internet 126/133. Siete timeouts por destino
remoto; intervalo primer timeout–respuesta posterior ~9,8 s. Respuestas
recuperadas antes del vencimiento del corte. IP PC conservada 10.148.0.2.
Evidencia return-inverse2-{pings.csv,command.log,ramona.raw,grillo.raw,root.raw}
en build/firmware. Root muestra hash b0990bec, ruta de Grillo por Este.

Estado actualizado: cuatro ensayos exitosos con la corrección de retorno,
dos en cada sentido, incluyendo repetición con default alternativo activo.
Restauración de los enlaces originales confirmada por telemetría tras los
ensayos previos; falta telemetría posterior al cuarto si se necesita cerrar
su reconexión exacta. No se valida pérdida simultánea de dos enlaces.
Este apartado reemplaza las cifras y pendientes históricos anteriores.