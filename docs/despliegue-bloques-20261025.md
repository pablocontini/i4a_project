# Carga del firmware en las quince placas — 25/10/2026

Estado: completado. Una consulta INFO independiente confirmó los quince
roles con el firmware nuevo, aplicaciones listas y sin pausa de arranque,
verificación ni recuperación pendientes. Después del despliegue se completó el
[ensayo de corte de Este](prueba-corte-este-v3-20261025.md); las secciones
siguientes conservan el orden y las condiciones de cada comprobación.

## Paquetes y resultado

Se usaron los paquetes finales `bloques-reservados-v3-20261025`, después de
comprobar SHA-256 de binario y fuentes y la correspondencia de las fuentes
archivadas con el workspace. No se recompilaron ni modificaron las imágenes.

| Nodo y central | Placas verificadas | ELF SHA-256 | Partición activa |
| --- | --- | --- | --- |
| Root, COM5 | N, S, E, W, C | `4aec92bb1bc1781973a70eb544af0455a840102fc2cffe80e6824843f1c9ae97` | `0x210000` |
| Grillo, COM7 | N, S, E, W, C | `4aec92bb1bc1781973a70eb544af0455a840102fc2cffe80e6824843f1c9ae97` | `0x20000` |
| Ramona, COM8 | N, S, E, W, C | `fe09b3f25e85e9bb98bcd46210722b6b0784dd41a079579452b3452c7e01ce03` | `0x210000` |

Cada imagen tiene 905040 bytes. En las quince respuestas finales:
`phase=0`, `ready=1`, `pending_verify=0`, `recovery_pending=0`,
`boot_paused=0`, `session=0`.

## Procedimiento y tiempos de Argentina

La comprobación inicial encontró cinco roles listos por nodo y los hashes
anteriores esperados: Root `3e6065c8…`, Grillo `143a472a…`, Ramona `a9e46318…`.
No había monitores externos ocupando los centrales.

Se utilizó el actualizador existente por OTA local: PC → USB del central →
anillo SPI → cinco placas. Root comenzó primero. Después se cargaron Grillo
y Ramona en paralelo, con puertos y anillos independientes. Cada proceso
conservó su propio registro serie y resultado; el resultado global se reunió
después de cerrar las tres transferencias.

| Nodo | Inicio de transferencia | Imagen verificada en cinco placas | Arranque confirmado |
| --- | --- | --- | --- |
| Root | 15:04:53 | 15:11:55 | 15:15:06 |
| Grillo | 15:06:55 | 15:13:24 | 15:16:07 |
| Ramona | 15:06:56 | 15:13:24 | 15:16:08 |

Root necesitó un reintento DATA y dos INFO durante el arranque. Grillo y
Ramona completaron los paquetes DATA en el primer intento. No hubo rechazo
de imagen, cancelación ni carga parcial seleccionada. Las cinco placas de
cada nodo verificaron la imagen antes de SELECT y REBOOT.

El primer arranque informó el hash nuevo con `recovery_pending=1` y
`boot_paused=1`. Se esperó la coordinación y el reinicio automático del
protocolo OTA. Se verificó nuevamente después, hasta obtener todas las
banderas normales. Esos reinicios forman parte de la actualización solicitada.

La consulta independiente posterior volvió a abrir COM5, COM7 y COM8 y
comprobó las quince respuestas, incluyendo hash y cambio de partición.
Se cerraron todos los monitores y puertos al terminar. No se borró NVS ni
se cambiaron selectores.

## Primeras asignaciones y tablas posteriores

Root registró las ofertas persistidas con `accepted=1 durable_ready=1`:

| Propietario | UUID | Bloque de nodo | Red local observada en su central |
| --- | --- | --- | --- |
| Ramona | `20E7C85AA245` | `10.96.0.0/11` | `10.116.0.0/14` |
| Grillo | `142B2FC05E7D` | `10.128.0.0/11` | `10.148.0.0/14` |

La captura posterior muestra las dos rutas de retorno en Root:
`0a600000/ffe00000:3` y `0a800000/ffe00000:4`. Ambos hogares tienen
`default=3`, con su ruta de clientes por salida 5. Las redes de clientes
son distintas; ya no aparece la duplicación observada en la corrida anterior.
Esto acredita la asignación inicial después de cargar el firmware, todavía
no la conservación de esas reservas durante un nuevo corte y reconexión.

## Capturas y comprobaciones pendientes

Evidencia completa:
`app/build/despliegues/bloques-reservados-20261025-150312-0191/`.
La ruta absoluta queda en `app/build/despliegue-bloques-activo.txt`.

Cada subdirectorio `root/control`, `grillo/control` y `ramona/control`
conserva bytes UART, TX, ACK, tiempos UTC y resultado propio. `monitor-este`
registra COM9 y el central de Root cuando este quedó disponible.
`post-carga` conserva 45 segundos de los tres centrales y Este de Grillo.
Los cuatro monitores corresponden a placas físicas distintas. Una reapertura
de puerto puede drenar datos anteriores del buffer; no interpretar por sí
solo su recepción como un nuevo reinicio.

`deployment.json` reúne el proceso y `verification.json` conserva la consulta
independiente de los quince roles. Se archivaron los hashes de la evidencia.

Wi-Fi 3 figuró desconectada al comprobar la PC. Se solicitó reconexión al
perfil guardado de Grillo; Windows no completó la asociación y la renovación
devolvió «medios desconectados». La IP anterior/APIPA no constituye una
conexión válida para pings. Queda reconectar el cliente y comprobar Internet
antes del próximo ensayo. No se modificaron IP estáticas ni rutas de Windows.

El próximo corte debe usar una captura nueva, los hashes anteriores y la
topología realmente formada después de estos arranques. El informe de
[reserva de bloques](correccion-bloques-reservados-20261025.md) describe la
corrección y sus pruebas C.

## Referencia posterior con Wi-Fi 3, 16:24–16:25

La reconexión manual a Grillo obtuvo `10.148.0.2`, gateway `10.148.0.1`.
Se capturó un minuto con pings de origen fijo por Wi-Fi 3 al gateway,
a la RPi `10.255.255.254` y a Internet `1.1.1.1`, junto con los centrales
de Root (COM5), Grillo (COM7), Ramona (COM8) y Este de Grillo (COM9).
La evidencia nueva está en
`app/build/capturas/referencia-v3-20261025-162451-8808/`; los resultados
por destino quedan en `pings-resultados.json`. Los tres destinos responden.
El gateway recibió 60/60 respuestas, la RPi 58/59 e Internet 54/57.
Hay pérdidas en los destinos remotos; no se interpreta esta referencia
como conectividad sin pérdidas.
Esta captura no modifica el archivo sellado del despliegue.

Grillo conserva clientes `10.148.0.0/14` y Ramona `10.116.0.0/14`.
Los snapshots de ambos centrales muestran únicamente Este admitido a Root,
con `default=3`; no muestran un enlace lateral admitido entre hogares.
No se provocó un corte: esta referencia confirma acceso desde el cliente,
pero no acredita conmutación por Ramona. Los cuatro monitores terminaron
normalmente y liberaron los puertos.

## Diagnóstico de búsqueda, 16:34

El usuario trasladó COM9 a Oeste de Grillo. La captura
`app/build/capturas/busqueda-oeste-20261025-163432-0710/` confirma `board=4`,
UUID `142B2FC05E7D` y hash `4aec92bb…ae97`. Oeste figura en modo estación
(`mode=1`), sin asociación y sin vecino por salida 4. Esto no demuestra que
la tarea de escaneo avance: los logs ESP de escaneo están deshabilitados
en esta compilación (`CONFIG_LOG_MAXIMUM_LEVEL=0`).

A las 16:35:18 se inició una captura pasiva de 22 minutos de los cuatro
monitores en `app/build/capturas/ciclo-oeste-20261025-163518-5260/`.
La ruta queda en `app/build/busqueda-oeste-activa.txt`. Su objetivo es observar
la alternancia de diez minutos AP/STA; resultado pendiente al iniciar.
No se ordenaron cortes ni reinicios. La sincronización entre radios de ambos
hogares es una hipótesis, no una causa demostrada.

La captura finalizó automáticamente a las 16:57:19, con los cuatro monitores
sin desconexiones ni errores de captura. Oeste pasó de estación a AP a las
16:36:59, volvió a estación a las 16:46:57 y a AP a las 16:57:03
(horas de recepción del host). La alternancia funciona; no quedó detenida
en un modo. No se observó asociación en sus snapshots de radio y Grillo
conservó únicamente Este admitido a Root durante la captura. Esto no permite
confirmar escaneos ni rechazos particulares, ni demostrar sincronización
con Ramona: falta observar una radio libre de ese nodo simultáneamente.

A las 17:09:46 se inició `app/build/capturas/busqueda-comparada-20261025-170946-8299/`
por 22 minutos, con COM9 Oeste Grillo, COM20 Sur Ramona, COM21 Oeste Ramona
y COM22 Este Ramona. COM21 confirma `board=4`, estación sin asociación;
COM22 confirma `board=3`, estación asociada por canal 4 (BSSID
`20e7c85aa46d`). Al inicio COM9 no existe y COM20 devuelve error de
dispositivo Windows 31; ambos quedan en reintento automático. Se solicitó
reconectar esos USB sin apagar nodos. La captura incompleta todavía no
permite comparar los ciclos entre hogares. Ruta activa en
`app/build/busqueda-comparada-activa.txt`.

El usuario identificó Oeste Grillo en COM23. Se cerró ordenadamente la
captura incompleta anterior y a las 17:10:52 comenzó una nueva de 22 minutos:
`app/build/capturas/busqueda-comparada-20261025-171052-5804/`.
COM23, COM21 y COM22 abrieron correctamente; COM20 conserva el error USB 31.
La comparación de Oeste de ambos hogares ya dispone de ambos monitores;
Sur de Ramona permanece en reintento. Finalización prevista 17:32:53.

La sesión terminó a las 17:32:53, pero no aporta 22 minutos útiles de radio:
COM23 dejó de entregar líneas completas aproximadamente a las 17:11:16;
COM21 y COM22 también entregaron solamente el tramo inicial. Además COM23
mezcla mensajes `board=4` y `board=2`, con tablas de Grillo y Ramona.
No se puede tratar ese flujo como una única placa identificada. El
`summary.json` sin fallos de lectura no detecta este silencio ni garantiza
continuidad de datos. Esta corrida no permite evaluar sincronización de
búsqueda; hay que verificar el cableado/asignación USB y la continuidad
antes de repetir una espera larga. No atribuir el silencio a un reinicio
o bloqueo de firmware sin evidencia adicional.

## Cuatro cables, identidades verificadas a las 17:41

El usuario reasignó COM5=Oeste Grillo, COM7=Oeste Ramona,
COM8=Sur Grillo y COM9=Sur Ramona, desconectando los USB de los centrales.
La captura de validación `app/build/capturas/validacion-cuatro-radios-20261025-174059-7694/`
confirmó las cuatro combinaciones de UUID, orientación y hash esperado.
Excluyendo los primeros cinco segundos de drenaje, todos entregaron datos
hasta los segundos 58–59, con separación máxima entre líneas I4ACAP de
aproximadamente 2,02 segundos. Las cuatro radios estaban en AP sin clientes.
Esta validación superó el silencio observado con el cableado anterior.

Se inició luego otra captura de 22 minutos, cuya ruta queda en
`app/build/cuatro-radios-activa.txt`, para comparar cambios de modo y
asociaciones simultáneos. Resultado pendiente; no se ordenaron reinicios
ni cortes de radio.

### Resultado del ciclo de cuatro radios, 18:04

`app/build/capturas/ciclo-cuatro-radios-20261025-174220-7285/` terminó
a las 18:04:21. Los cuatro monitores entregaron datos hasta los segundos
1318–1319, sin reconexiones ni fallos de captura. Esta corrida sí tiene
continuidad para comparar las radios.

Tiempos de recepción locales de cambios observados:

| Radio | AP a estación | Estación a AP |
|---|---|---|
| Sur Ramona | 17:46:52 | 17:56:54 |
| Sur Grillo | 17:46:57 | 17:57:00 |
| Oeste Ramona | 17:47:09 | 17:57:16 |
| Oeste Grillo | 17:47:15 | 17:57:22 |

Las cuatro comenzaron en AP sin clientes, pasaron a estación casi juntas
y regresaron a AP. Las parejas equivalentes difieren solamente unos
5–6 segundos; durante casi toda la ventana de búsqueda ambas están en
estación y durante la espera ambas en AP. No se observó asociación de
estas radios en los snapshots. La captura demuestra ciclos casi
sincronizados y respalda que el calendario determinista dificulta el
encuentro; no identifica por sí sola cada intento de escaneo o rechazo.
Conviene romper la sincronización con esperas variables acotadas en las
radios libres, preservando las comprobaciones que protegen enlaces
ocupados. Esa modificación es una posibilidad no implementada; el enlace por
Norte encontrado después impide concluir que sea necesaria para esta topología.

Hay un dato adicional decisivo: los snapshots de vecinos de los cuatro
monitores ya muestran salida 1 en fase 3 hacia el otro hogar (Grillo ve
`20E7C85AA245` y Ramona ve `142B2FC05E7D`), además de salida 3 hacia Root.
Por tanto, la ausencia de asociación en las radios Sur/Oeste no equivale
a ausencia de enlace redundante: el registro indica uno por Norte.
Debe comprobarse ese enlace y su tráfico antes de modificar el calendario;
la sincronización observada no demuestra una falla actual de redundancia.

## Verificación de Norte y principal, 18:13–18:14

El usuario conectó COM5=central Grillo, COM7=Este Grillo, COM8=Norte Grillo
y COM9=Norte Ramona. Se verificaron los cuatro UUID/orientaciones y hashes
en `app/build/capturas/verificacion-norte-20261025-181353-1004/`, durante
60 segundos, sin ordenar cortes. Las cuatro UART mantuvieron datos hasta
el final. Los datos iniciales acumulados se drenaron antes de evaluar
la continuidad.

Grillo conserva `default=3`, con Este asociado a Root por canal 10.
Norte Grillo está asociado a Norte Ramona por canal 5, BSSID `841fe82713bd`,
con RSSI observado aproximadamente −86 a −88 dBm al inicio; Norte Ramona
figura como AP con un cliente. Existen rutas recíprocas de los hogares:
Grillo `0a600000/ffe00000:1`, Ramona `0a800000/ffe00000:1`.
Esto confirma la asociación lateral y sus rutas, aunque el tráfico hacia
Internet durante esta referencia todavía utiliza el principal por Este.
Los pings se fijaron a Wi-Fi 3, origen `10.148.0.2`; los conteos quedan en
`pings-resultados.json`. Esta referencia no valida aún la conmutación.

## Cambio lateral y arranque no solicitado de Ramona, 19:22

El usuario informa que no reinició Ramona. Se conserva por tanto el arranque
como incidente no solicitado; no se ejecutó un corte de prueba.
`app/build/capturas/verificacion-oeste-20261025-192205-8468/` registra
60 segundos en COM5 central Grillo, COM7 Este Grillo, COM8 Oeste Grillo y
COM9 Este Ramona. Los cuatro informan las identidades/hashes esperados.
Este Ramona informa `reset=1`, con unos 3272 segundos de uptime al inicio:
esta captura comenzó después del incidente y no contiene el motivo previo
al arranque. No se puede atribuir a una falla de software con estos datos.

El principal continúa por Este Grillo a Root; el lateral ahora es Oeste
Grillo (AP con un cliente) a Este Ramona (estación, canal 2, RSSI cercano
a −57 dBm). Grillo tiene el bloque Ramona por salida 4 y Ramona el de
Grillo por salida 3; Ramona sale a Root por Norte (`default=1`). Los bloques
de clientes se conservaron: `10.148.0.0/14` y `10.116.0.0/14`.
Los tres destinos de ping recibieron 60/60 respuestas desde Wi-Fi 3,
origen `10.148.0.2`. Esto verifica la referencia actual, no descarta el
incidente anterior ni valida conmutación. Falta capturar un eventual nuevo
arranque de Ramona desde antes del evento.

Actualización: con autorización del usuario se completó un
[corte de 60 segundos de Este de Grillo](prueba-corte-este-v3-20261025.md).
Internet se recuperó por Ramona en unos 18,6 segundos, antes de restaurarse
el principal. Se conservaron los bloques y no hubo regresiones de uptime
en los cuatro monitores durante ese ensayo. La incorporación tardía de
un alternativo y los cortes repetidos siguen pendientes.
