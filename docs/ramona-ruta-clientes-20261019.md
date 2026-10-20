# Ramona: falta la ruta de retorno a sus clientes, 19/10/2026

Los centrales conectados durante este diagnóstico son Root COM5, Grillo COM7
y Ramona COM8. Se consultaron las quince placas: todas respondieron `ready=1`
y `boot_paused=0`. Root y Ramona ejecutan `3e6065c8…`; Grillo `1ff9eff3…`.
No hay versiones mezcladas ni barrera de arranque pendiente en esta prueba.

## Evidencia

La captura `tres-centrales-20261019-121903-160` muestra estas tablas:

| Nodo | Default | Rutas específicas |
| --- | --- | --- |
| Root | 5, central | Ramona `10.64.0.0/11 → Sur`, Grillo `10.96.0.0/11 → Este` |
| Grillo | 3, Este | Clientes `10.116.0.0/14 → central`, Ramona `10.64.0.0/11 → Oeste` |
| Ramona | 2, Sur | Grillo `10.96.0.0/11 → Este`, `10.0.0.0/8 → Sur` |

Al conectar Wi-Fi 2 a `ComNetAR - Ramona`, SSID/BSSID confirmado por Windows,
la PC recibió `10.84.0.3`, gateway `10.84.0.1`. Cinco pings al gateway
respondieron; cinco a RPi y cinco a Internet fallaron, con origen explícito
en esa interfaz. La captura posterior de 30 segundos,
`ramona-retorno-20261019-122302-923`, repitió el resultado:

| Destino | Recibidos / intentos, espera 2000 ms |
| --- | --- |
| Gateway Ramona | 30 / 30 |
| RPi | 0 / 16 |
| Internet | 0 / 16 |

Ambas capturas terminaron con `summary.json`, `failures=[]`. La segunda contiene
`analisis-retorno.json` y los tres UART. La falta de la ruta local se mantuvo
durante esa medición; no es únicamente una observación durante el arranque.

Falta `10.84.0.0/14 → central`. En la tabla observada, una respuesta destinada
a `10.84.0.3` coincide con `10.0.0.0/8 → Sur`, en lugar de dirigirse a la placa
que atiende a los clientes. Es un defecto de retorno distinto de los descartes
aislados por CRC. Las tablas son compartidas entre las placas del nodo; no se
monitoreó directamente el periférico Sur durante esta prueba.

No se identificó aún qué publicación o secuencia hizo desaparecer esa ruta.
No se atribuye esa desaparición a un paquete SPI específico a partir de estos
datos, ni se declara probado que sea la única falla posible de extremo a extremo.

## Corrección y pruebas

`home.c` instalaba la ruta solo al recibir la primera provisión y luego ignoraba
provisiones repetidas. No comprobaba si seguía presente tras recibir otra tabla
compartida. Ahora conserva la subred asignada fuera de esa tabla y verifica,
en sus turnos periódicos de routing, la entrada exacta de clientes. Si falta
o señala una salida incorrecta, la repone y solicita publicar la tabla.

No renumera clientes, no reinicia el AP, no sustituye la salida WAN ni borra
rutas laterales. Cuando la tabla ya es correcta, no modifica ni publica nada.
Antes de provisionarse, no inventa ninguna red local. Se usan los mutex
existentes y el mecanismo existente de publicación pendiente y reintento.

La regresión nativa reproduce la tabla observada, verifica que inicialmente
dirige al cliente hacia Sur y comprueba su reparación, la conservación de WAN
y lateral, ausencia de entradas duplicadas, y que el AP se configura una sola
vez. También cubre una entrada local con salida incorrecta y un central aún
sin provisión. Los dos casos nuevos fallaron antes de la corrección. Pasaron
los 24 casos de routing y los 5 de hooks de selección de interfaz.

El paquete `app/build/link-capture/ramona-retorno-20261019/` compiló con ESP-IDF
5.4.1. Aplicación: 808480 bytes; ELF SHA-256:

`a38093781f032ec226f51861988131c62040d0f9bfd3238bb4cbe37a6a5aa1f1`.

Se verificaron los cuatro segmentos exportados y la coincidencia del archivo
de fuentes con `home.c` y su estado actualizado. Al preparar el paquete aún
no se había instalado; la actualización posterior y su resultado se registran
a continuación. La reparación periódica añade pedidos de turno del central;
no se declara eliminada toda pérdida de paquetes por esta modificación.

## Actualización OTA y resultado en hardware

El 19/10 se actualizó Ramona por COM8 con `ramona-retorno-20261019/main.bin`,
seleccionando las cinco placas. La transferencia, selección y verificación
posterior terminaron correctamente. Todas reportaron el hash `a3809378…`,
partición `0x210000`, `ready=1`, `boot_paused=0`, `pending_verify=0` y
`recovery_pending=0`, tras los reinicios previstos por el procedimiento OTA.
Root y Grillo conservaron sus versiones de control.

La captura `ramona-post-ota-20261019-123650-359` confirmó la ruta
`10.84.0.0/14 → 5` y el retorno de conectividad desde Wi-Fi 2, IP `10.84.0.2`:

| Destino | Recibidos / intentos, espera 2000 ms |
| --- | --- |
| Gateway Ramona | 23 / 27 |
| RPi | 27 / 29 |
| Internet | 23 / 27 |

Se conservaron `resultado-ota.json`, los UART y `summary.json` con
`failures=[]`. El log de actualización local es
`app/build/ota-ramona-retorno-20261019-122647-827.log`.
Se recuperó el acceso que antes fallaba en todos los intentos a RPi/Internet;
esta medición corta todavía muestra pérdidas y no demuestra ausencia de fallos
prolongados.

El **20/10/2026**, el usuario informó que la prueba llevaba **12 horas sin
reinicios**. Es un resultado de operación reportado por el usuario, sin una
captura continua de esas doce horas analizada en esta revisión. No equivale
a doce horas sin pérdida de paquetes ni garantiza estabilidad indefinida.

## Particularidad de Root

En `node_set_as_ap`, el central de Root configura `Internet4All_Root` como
enlace /30 con un único cliente para la RPi. Ese AP no equivale al hotspot de
clientes `ComNetAR - Grillo` o `ComNetAR - Ramona`. No cambiar esa configuración
ni desconectar la RPi para intentar reproducir con un cliente de PC la misma
prueba. Este diagnóstico confirmó la falla desde Ramona; no midió otra conexión
de PC a Root ni permite atribuirle la misma causa.
