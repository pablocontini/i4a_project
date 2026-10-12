# Corrección de clasificación punto a punto durante búsqueda

## Evidencia en hardware

Referencia por Wi-Fi 3, IP 10.148.0.2/gateway 10.148.0.1: gateway 10/10;
los cinco destinos posteriores dieron 1/10. CSV:
`build/firmware/ipdiag-probes-20261012-074743.csv`.

Durante ese ensayo el central y el sur mantuvieron tabla idéntica
(default=4, hash=bfddf9b1), con RPi/internet por oeste. El central envió
ICMP por SPI con retorno del driver ESP_OK. El sur recibió las mismas
secuencias y su input lwIP las aceptó, pero eligió `via=sink` mientras
escaneaba como Station sin candidato. Ejemplos: id=1 seq=697 y seq=715.
Con seq=733 el sur pasó a `via=spi`; el central recibió la respuesta de
10.128.0.2 y la dirigió al hotspot. Que ambos drivers reporten éxito no
acredita cada entrega remota, pero aquí existe recepción correlacionada.

Capturas: `ipdiag-probe-center-20261012-074732.raw` y
`ipdiag-probe-south-20261012-074732.raw`, en build/firmware.

## Defecto reproducido

`device_is_point_to_point_message` comparaba `(dst & sta_mask)==sta_subnet`
sin validar que hubiera una red de transporte. La Station recién inicializada
tenía máscara=0/subred=0: la expresión daba true para cualquier destino.
El hook interceptaba paquetes que debían seguir por SPI y elegía una Wi-Fi
todavía no disponible, produciendo el sink. Al retornar a AP la máscara era
válida y el tráfico podía volver a pasar. Esto concuerda con la intermitencia
observada; aún debe medirse nuevamente el hardware después de instalar.

La regresión compiló la función anterior copiada sin modificaciones desde
device.c y el hook real: el primer caso falló porque un paquete de tránsito
no alcanzó la interfaz SPI. Copia usada: build/firmware/p2p-old-predicate.c.

## Cambio

La función pública se conserva, ahora en `components/wireless/device/point_to_point.c`.
Una máscara cero nunca identifica una subred de transporte. La Station debe
tener IP (`is_fully_connected`) y no estar deteniéndose. Un AP sin máscara o
deteniéndose tampoco reclama destinos. Se verifican punteros y modos ausentes.
No se exige admisión lógica para el tráfico punto a punto de una Station con
DHCP: el handshake inicial sigue pudiendo usar el gateway de transporte.

Los paquetes ajenos al transporte continúan por las reglas de ruteo existentes.
No se renumeran nodos ni cambian filtros de ofertas, RSSI, vecinos o gateway.
La corrección debe instalarse en todas las placas de los nodos participantes:
una radio libre de Root o Grillo también puede interrumpir un recorrido IP.

## Pruebas y builds

Cuatro casos nativos con la función y hook reales aprobaron: escaneo sin IP
no intercepta tránsito; máscara cero inválida; transporte válido conserva
su alcance; desconexión/detención y punteros/modes ausentes son seguros.
Runner: `tests/run_native_p2p_routing.py`. Las 37 pruebas Python aprobaron.
Los diagnósticos previos conservan sus 28 verificaciones aprobadas.

Bundles compilados con ESP-IDF 5.4.1, tiempos rápidos y perfil de pila 4096:

- `firmware_bundle_p2pfix_ramona_20261012`, versión dev-p2pfix-20261012-ramona.
  ELF: 5af517e56ee7c59d7f0f34ead8c4775cccd9a5c255f765d9260b09879425f208.
- `firmware_bundle_p2pfix_grillo_root_20261012`, versión dev-p2pfix-20261012-grillo.

La instalación se verifica por nodo después de OTA; no se considera terminada
por existir un bundle. Logs y reportes: p2pfix-<nodo>-ota.log e
p2pfix-<nodo>-installed.json. La prueba de corte continúa pendiente.
## Instalación verificada

Ramona (COM5), Grillo (COM8) y Root (COM9) completaron la actualización.
Las cinco placas de cada nodo reportaron el hash esperado, ready=1,
boot_paused=0, pending_verify=0 y recovery_pending=0.
Grillo y Root: ELF bbe9501d964d91bbbcfebec692f1164b9bf8a0630c26c62b4b1785726ba2d4af.
Los reportes están en build/firmware/p2pfix-<nodo>-installed.json.

Después de las actualizaciones, Wi-Fi 3 obtuvo 10.52.0.2 con gateway
10.52.0.1 y no conserva las rutas específicas de prueba. Deben reponerse
antes de medir la referencia. El corte con esta corrección sigue pendiente.
## Ensayo físico del 12 de octubre, 08:45 (UTC-3)

Con el triángulo admitido, se solicitó desde COM5 un corte de Norte durante
90 segundos. Registro: build/firmware/p2pfix-cut-command.log. El central
pasó por default=255 y luego default=2 (Sur), conservando el hotspot.
Los pings al Root y RPi volvieron aproximadamente a las 08:45:39; internet
volvió también antes del vencimiento del corte. El gateway local respondió
sin interrupción y la PC conservó 10.52.0.2. Hubo pérdidas aisladas hacia
internet incluso antes del corte: no atribuir todas las pérdidas al cambio.
La reconexión de Norte aún requiere confirmación mediante telemetría;
la última tabla capturada mantuvo Sur como gateway activo.
Evidencia: p2pfix-cut-pings.csv, p2pfix-cut-center.raw y p2pfix-cut-south.raw
bajo build/firmware. Este ensayo demuestra recuperación IP por el alternativo,
no ausencia de pérdidas ni validación de todos los escenarios.
### Confirmación posterior, 08:49 (UTC-3)

La captura compartida por el usuario (dbb14b79-4c64-43db-a80a-45b86b9d8874)
confirma Norte de Ramona y Norte de Root admitidos en ambos extremos,
RSSI aproximadamente -32/-27 a -29. También siguen admitidos Sur de
Ramona–Sur de Grillo y Oeste de Grillo–Este de Root. Las subredes del
hotspot permanecen 10.52.0.0/14 (Ramona) y 10.116.0.0/14 (Grillo).
Los uptime avanzaron sin evidencia de reinicio completo de los nodos.
Esto confirma restauración del enlace original, pero la telemetría no
identifica el gateway activo ni el instante exacto de reconexión.
## Repetición del corte, 08:52–08:53 (UTC-3)

Antes del corte: default=2 (Sur), ruta específica RPi por Norte (rpi_out=1).
Se solicitó Norte por 60 segundos. Durante la pérdida, la tabla seleccionó
Sur para ambos destinos (default=2, rpi_out=2, internet_out=2), pero los pings
remotos fallaron desde 08:52:15 hasta aproximadamente 08:53:39, después del
plazo del corte. Después reapareció la ruta RPi por Norte. No se demuestra
recuperación por el alternativo en esta repetición; no está validada la
consistencia. Gateway 78/78; RPi 20/78; internet 19/78. IP conservada 10.52.0.2.
Evidencia: build/firmware/repeat-cut-pings.csv, repeat-cut-command.log y
repeat-cut-center.raw. Necesita capturas simultáneas de Grillo, Root y RPi
para distinguir ida, retorno y sesión lateral. No atribuir causa solo al
snapshot local ni afirmar que la vuelta de Norte garantiza el recorrido.
## Captura simultánea contrastada con RPi

La salida de tcpdump compartida muestra solicitudes y respuestas durante
los timeouts de Windows. Ejemplo: RPi 08:58:46.867298 recibe echo request
seq=1938 de 10.52.0.2 y responde a 08:58:46.867431; Windows registra timeout
08:58:47.191712. Las relojes de PC y RPi presentan un pequeño desfase:
correlacionar por id/seq cuando el registro lo permite, no asumir reloj idéntico.
Internet seq=2023 tiene respuesta en RPi 08:59:29.136068 y el central Root
registra spi_rx/input_ok de esa misma respuesta (hora PC 08:59:28.829012).
La ida alcanza la RPi e internet; hay falla de retorno dentro de la malla.
Root mantiene solamente 10.96.0.0/11 por Este durante este tramo; falta
10.32.0.0/11 por el vecino alternativo. Esto identifica un defecto de
reconstrucción/propagación de la ruta de retorno como hipótesis principal,
pero todavía no demuestra cuál callback lo causa ni confirma un bucle.
Capturas locales: simultaneous-{center,south,grillo,root}.raw,
simultaneous-cut.log y simultaneous-pings.csv en build/firmware.
Resultado total: gateway 87/87; RPi 11/87; internet 7/87. Pendiente corregir
y probar el anuncio de rutas hacia Root cuando se pierde el enlace directo.
## Corrección del inicio de recuperación tras una pérdida no predeterminada

peer.c iniciaba HAG solamente cuando is_local_root era verdadero. Después
de la primera conmutación, Norte podía seguir aportando una ruta directa
hacia Root sin ser el gateway elegido. Su pérdida retiraba la ruta de
retorno en Root sin disparar una nueva propagación de Ramona por Grillo.
Se amplió el inicio de recuperación a radios provisionadas con DTR>1;
las radios de Root (DTR=1) conservan su salida por el central. Se reutiliza
el protocolo HAG existente, sin agregar prioridades ni formatos de mensajes.
Esto también puede iniciar convergencia al perder otros enlaces no activos:
es una decisión conservadora con posible interrupción breve adicional.

La regresión test_lost_nondefault_link_rebuilds_return_route falló en la
línea 192 antes del cambio y pasó después. Los 15 casos de recuperación
aprobaron con el código C real mediante tests/run_native_recovery_objects.py.
Aún falta instalar esta corrección y validar físicamente varios cortes;
los nodos siguen ejecutando la imagen anterior.