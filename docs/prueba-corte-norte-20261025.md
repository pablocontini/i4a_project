# Prueba de corte de Norte de Grillo — 25/10/2026

Para nuevas cargas y ensayos, usar la [corrección de reserva de bloques](correccion-bloques-reservados-20261025.md).
Este documento conserva los resultados y las versiones de la corrida anterior.

Ensayo posterior con el firmware de bloques reservados:
[corte de Este con retorno por Ramona](prueba-corte-este-v3-20261025.md).

Estado: ensayo completado y capturas cerradas. Conmutación por Ramona y
recuperación de Internet comprobadas, con retorno posterior a Norte.

## Montaje y comprobaciones

El usuario informó COM5=Root central, COM7=Grillo central, COM8=Ramona
central y COM9=Norte de Grillo. Las consultas a los tres centrales obtuvieron
los quince roles con `ready=1`, `boot_paused=0`, `pending_verify=0` y
`recovery_pending=0`. Los cinco roles de Grillo ejecutan el firmware corregido:
`6b3772b240074f33dc41fd3e41a1830ca93d6b471b194edacf4f8fb76807a3d8`.
No se reiniciaron ni se actualizaron los nodos en esta preparación.

Wi-Fi 3 recibió por DHCP `10.52.0.2/14`, gateway `10.52.0.1`, con
`WeakHostSend` deshabilitado. Los pings usan exclusivamente ese origen.
La tabla actual de Grillo confirma `default=1 rpi_out=1 internet_out=1`:
Norte es la salida principal. La ruta al bloque de Ramona es por Este.

La topología comunicada permite intentar el retorno por Ramona durante
un corte de Norte: Grillo N–Root N y Grillo E–Ramona E–Ramona N–Root S.
Se requiere comprobar los vecinos admitidos y la conectividad de ida y vuelta
durante el corte, además de las tablas.

## Registro e identificación pendiente

Carpeta local:
`app/build/capturas/corte-norte-20261025-105108-097097`.
`app/build/corte-norte-activo.txt` conserva su ruta.
`preparacion/` contiene los quince estados iniciales; `ensayo-grillo-1/`
registra los monitores y pings desde las 10:51:35 de Argentina.
Se agregó COM7 como cuarto monitor pasivo para comprobar el estado del central.
El controlador de corte todavía no se inició.

El archivo inicialmente etiquetado `grillo_north.log` contiene COM9, pero
su UART informa `board=3`, correspondiente a Este, y está asociada al BSSID
`841fe8271e99`, Este de Ramona, por canal 8. Por tanto, la etiqueta inicial
no acredita que se esté monitorizando Norte. El contenido crudo se conserva
con esa discrepancia documentada y una marca en la línea de tiempo.

Se solicitó conectar la placa que la telemetría identifica como Norte de
Grillo, UUID físico `142B2FC1DD91`, e informar su COM, manteniendo los nodos
encendidos. Si el usuario confirma que ya es esa placa, revisar la orientación
antes de continuar. No se cambió configuración ni se enviaron órdenes de corte.

## Monitor corregido y referencia iniciada

El usuario pidió reintentar la comprobación. COM9 volvió a conectarse a las
10:54:34 y ahora informa `board=1`, asociado al BSSID `841fe82720a9` de Root
Norte, canal 1. Se cerró ordenadamente la captura de preparación, que conserva
la transición entre las dos placas y no se utilizará para inferir reinicios.

La captura nueva `ensayo-grillo-2/` comenzó a las 10:55:45. Registra Root
central, Ramona central, Norte de Grillo y los pings. El controlador registra
por separado el cuarto UART, Grillo central, en `control-grillo-2/`.
Consultó los cinco roles y comenzó la referencia a las 10:55:51.370.
Si cumple la referencia, enviará un corte de Norte por 60 segundos y seguirá
registrando 240 segundos desde el ACK, con restauración automática.

El registro del central confirma dos vecinos admitidos: Root por Norte y
Ramona por Este. Se conserva la IPv4 DHCP `10.52.0.2`, gateway `10.52.0.1`.

## Procedimiento aplicado

Se conservaron los enlaces existentes y se registraron los tres centrales y
Norte de Grillo simultáneamente. COM7 perteneció al controlador, que registró
su UART sin competir con el capturador. Se comprobaron estados y vecinos,
se registraron 120 segundos de referencia y se envió un único corte de
60 segundos, con restauración automática y 240 segundos de observación
desde el ACK. La captura inicial de preparación quedó separada del ensayo.

## Ejecución y recuperación observada

El controlador envió una sola orden de corte a las 10:57:51.483. Norte registró
`cut_begin` a las 10:57:51.514 y `cut_applied` a las 10:57:51.610. El ACK de
Norte y central llegó sin reintentos. Los vecinos confirmados antes del corte
eran Root por Norte y Ramona por Este, ambos en fase activa.

La RPi volvió a responder a las 10:58:07.985: 16,375 segundos después de
`cut_applied`. Internet volvió a las 10:58:08.500: 16,890 segundos después.
En ambos casos la primera respuesta inicia una serie de cinco respuestas.
Contando desde la finalización del primer intento fallido posterior al corte,
los intervalos son 14,139 segundos y 14,154 segundos respectivamente.

La tabla de Grillo pasó de `default=1` a `default=0` y luego a `default=3`,
con `rpi_out=3 internet_out=3`. La tabla de Root cambió el retorno del bloque
de Grillo `10.32.0.0/11` de Norte a Sur, donde está Ramona. Ramona conservó
su ruta de salida por Norte a Root y su ruta al bloque de Grillo por Este.
Las respuestas con Norte detenido, más estos cambios de ida y retorno,
acreditan el camino Grillo E → Ramona E → Ramona N → Root S.

`restore_begin` apareció a las 10:58:51.703 y `radio_restored` a las
10:58:51.812. El reloj del firmware mide 60,096 segundos desde el corte
aplicado hasta el comienzo de la restauración. Internet ya había vuelto
43,312 segundos antes de `radio_restored`.

Tras la reactivación, Norte volvió a asociarse a Root y el central registró
otra vez `default=1 rpi_out=1 internet_out=1` a las 10:59:28.786, unos
36,974 segundos después de arrancar la radio. La IPv4 del cliente sigue
siendo `10.52.0.2`, con gateway `10.52.0.1`.

La referencia tuvo dos timeouts aislados de Internet antes del corte, a las
10:56:40.849 y 10:56:45.850. El controlador exigió y obtuvo las últimas cinco
respuestas satisfactorias por destino antes de enviar la orden. El análisis
de interrupción parte del primer fallo posterior a `cut_begin`, para no
confundir esas pérdidas de referencia con la caída provocada.

## Resultado final y pérdidas

El controlador completó la observación a las 11:01:51.679 y consultó los
cinco roles de Grillo antes de cerrar. Los otros tres monitores y los pings
cerraron a las 11:02:00.957, agregando unos nueve segundos de observación.
Los dos resúmenes de captura terminaron sin errores y con una única conexión
por monitor durante el ensayo limpio.

| Destino | Antes del corte | Radio detenida | Restauración y tramo posterior |
| --- | ---: | ---: | ---: |
| Gateway `10.52.0.1` | 126/126 | 60/60 | 188/188 |
| RPi `10.255.255.254` | 126/126 | 44/52 | 188/188 |
| Internet `1.1.1.1` | 122/124 | 43/51 | 188/188 |

Valores: respuestas/intentos, clasificados por finalización en el PC.
La columna final agrupa una respuesta de Internet recibida durante el
arranque de la radio y las 187 posteriores a `radio_restored`. La fase
previa incluye unos seis segundos anteriores al inicio de la referencia.
Los intentos fallidos tardan más y reducen el número de muestras por fase.

Hubo ocho timeouts de RPi y ocho de Internet durante la interrupción inicial.
No hubo pérdidas de ping en la restauración ni en el tramo posterior, incluido
el cambio de regreso a Norte. El gateway local respondió durante todo el
ensayo y la PC conservó `10.52.0.2/14`, gateway `10.52.0.1`.

No se detectaron retrocesos de uptime ni reinicios en los cuatro ESP
monitorizados. Las consultas independientes posteriores obtuvieron los
quince roles con los hashes esperados, `ready=1`, `boot_paused=0`,
`pending_verify=0` y `recovery_pending=0`. Los once ESP sin monitor continuo
no tienen una verificación directa de uptime durante toda la ventana.

| Monitor | Aumento RX SPI | Aumento CRC en copia | Tramas cortas / sin buffer / colas llenas / TX fallido |
| --- | ---: | ---: | --- |
| Root central | 12831 | 3 | Sin aumentos |
| Grillo central | 12557 | 26 | Sin aumentos |
| Ramona central | 10327 | 19 | Sin aumentos |
| Grillo Norte | 13148 | 0 | Sin aumentos |

Las pérdidas SPI existentes no impidieron recuperar el tráfico en esta
corrida. No se atribuye un timeout de ping concreto a un CRC particular.

## Alcance y evidencia

La corrida valida la conmutación del firmware corregido mediante un enlace
con Ramona ya admitido antes del corte. El ensayo fallido del 22/10 tenía
otra condición: solo un enlace inicial y posterior admisión de Root por otra
radio. No es una comparación A/B de la misma topología ni aísla por sí sola
el efecto de la corrección de distancia igual; ese caso tiene reproducción
y pruebas nativas en el [informe anterior](prueba-corte-enlace-20261022.md).
Este resultado corresponde a un corte de 60 segundos, sin reiniciar nodos
ni desconectar el anillo SPI.

La carpeta del experimento conserva los UART crudos, líneas de tiempo UTC,
pings con origen fijo, marcas de control, estados antes/después y snapshots
de IP y rutas. `analizar-ensayo.py` produce `resultado-ensayo-grillo-2.json`.
El estado final queda en `experimento.json`, con `state=completed` y
`failover_verified=true`. Se guarda un paquete local de evidencia con su
manifiesto SHA-256 junto a la carpeta de capturas.

## Pendientes después de esta corrida

Actualización posterior: [ensayo de alternativa tardía](alternativo-tardio-20261025.md).
La colisión encontrada tiene una [corrección de provisión implementada](correccion-bloques-reservados-20261025.md),
ya [cargada y verificada en las quince placas](despliegue-bloques-20261025.md).
El [ensayo posterior de Este](prueba-corte-este-v3-20261025.md) validó el corte
y la reconexión con esa versión y un alternativo previamente asociado.
Se implementó la búsqueda con confirmación de recorrido y pasó las 30 pruebas
nativas. En hardware recuperó Internet por un nuevo enlace directo a Root,
pero al incorporar Ramona se detectó una colisión de bloques que volvió a
cortar la conectividad. La corrección de provisión y el caso tardío de mayor
distancia siguen pendientes; esta corrida anterior no los valida.

Falta validar en hardware la recuperación cuando no había alternativo
admitido al perder el principal y otro enlace aparece después. El empate
de distancia observado el 22/10 tiene corrección y pruebas nativas, pero la
corrida de hoy no reproduce esa condición.

Hay además una limitación de código por resolver en ese escenario: si una
radio conserva una distancia anterior no nula y el nuevo camino requiere
mayor distancia, `should_adopt_gateway` rechaza el candidato antes de revisar
si falta la ruta por defecto. El camino por Ramona funcionó hoy porque ya
estaba admitido y participó en la búsqueda de gateway. Esto no cubre su
admisión tardía. Se necesita reproducir ese caso y resolver la recuperación
manteniendo la prevención de ciclos de ruteo.

Como comprobación adicional de robustez quedan los cortes repetidos; una
única corrida no descarta fallas acumulativas de tareas, memoria o sesiones.
La corrección, las herramientas y los informes se incluyen en la publicación
de la rama `feat/config-portal-stability`; el [resumen de cierre](cierre-estabilidad-20261025.md)
reúne resultados actuales y limitaciones.
