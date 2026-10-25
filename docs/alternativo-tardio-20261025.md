# Recuperación por un enlace alternativo tardío — 25/10/2026

Actualización de código posterior al cierre de esta captura:
[reserva de bloques por identidad](correccion-bloques-reservados-20261025.md).
La corrección ya está implementada y probada en C, fue
[cargada en las quince placas](despliegue-bloques-20261025.md) y pasó un
[corte con alternativo previamente asociado](prueba-corte-este-v3-20261025.md).
La incorporación tardía continúa pendiente en hardware. Los resultados que siguen corresponden
a las versiones del ensayo y se conservan como evidencia del defecto.

Estado: ensayo terminado y capturas cerradas. Se verificó recuperación por
un enlace directo que apareció después del corte, pero el encendido posterior
de Ramona produjo una colisión de direcciones y otra pérdida de conectividad.
Ese defecto de provisión quedó reproducido y fue corregido posteriormente. El
caso tardío de mayor distancia está cubierto por pruebas nativas, todavía
no por un ensayo válido en hardware. No considerar resuelto el proceso completo.

La corrección de búsqueda de gateway pasó 30 pruebas nativas y compilación;
OTA completada en las cinco placas de Grillo, todas listas al terminar.

## Defecto y criterio de recuperación

Una radio sin vecino puede conservar DTR 2 después de que la tabla compartida
pierda su ruta por defecto. Si más tarde se admite un vecino con DTR 2, el
camino candidato requiere DTR 3. El código anterior lo rechazaba aunque no
hubiera una salida disponible. La solicitud SPI inicial puede perderse, por
lo que recibirla no es una condición suficiente para asegurar recuperación.

La corrección conserva la selección de una salida ya configurada y el caso
de Root directo. Para recuperar un gateway ausente mediante un vecino que no
es Root, inicia el protocolo existente de solicitud de gateway, incluyendo
el bloque del solicitante en `hag_networks`. Espera su respuesta antes de
instalar la ruta. Un recorrido que vuelve al bloque de origen se rechaza
mediante las comprobaciones existentes del protocolo.

La tarea de ruteo también revisa una tabla sin default cuando conserva un
vecino conectado y distancia anterior. Esto inicia una búsqueda aunque se
haya perdido el mensaje SPI de caída. Se reutiliza el reintento de 30 segundos;
los handshakes repetidos no reinician el plazo ni aceptan un DTR sin confirmar.
Una vez elegido un gateway, los reintentos se cancelan.

No cambia el formato de los mensajes, el bloque de red del nodo, el hotspot
ni la provisión de clientes. Root y Ramona pueden participar con las versiones
que ya ejecutaban durante el ensayo anterior.

## Pruebas y paquete preparado

Se agregaron tres pruebas con los callbacks C reales:

- Radio tardía con distancia retenida, pérdida de la primera solicitud,
  retransmisión y recuperación completa Grillo → Ramona → Root, verificando
  rutas de ida, retorno y conservación del bloque y la ruta local del cliente.
- Tabla sin default después de perder una solicitud SPI, sin reconectar el
  vecino; búsqueda iniciada por tick y cancelación después del ganador.
- Rechazo de una solicitud cuyo recorrido vuelve al nodo que la originó.

Las tres fallaron antes del cambio. Después pasan las 30 pruebas de ruteo,
incluidas las anteriores de reconexión directa, distancia igual, pérdidas de
publicación y cola saturada. Las salidas quedan en `pruebas/routing-antes.log`
y `pruebas/routing-despues.log` de la carpeta del experimento.

Imagen compilada:
`app/build/link-capture/grillo-alternativo-tardio-20261025/main.bin`,
901984 bytes, ELF
`143a472aaec94c6178509cb48b408786e00dc0aea0052f64c0b4769a077efe89`.
El paquete conserva `sources.zip` y el manifiesto de hashes.

## Ensayo preparado

Carpeta de trabajo:
`app/build/capturas/alternativo-tardio-20261025-114614-288034`.
La ruta absoluta queda en `app/build/alternativo-tardio-activo.txt`.
COM5=Root central, COM7=Grillo central, COM8=Ramona central y COM9=Este de
Grillo. Durante la preparación COM9 cambió de Norte a Este: `uart-ota/`
contiene ambas identidades y los reinicios previstos de OTA. No usar esa
carpeta para evaluar ausencia de reinicios durante el corte.

Después de OTA, Grillo eligió Este hacia Root y recibió bloque
`10.116.0.0/14`. Wi-Fi 3 se reconectó al hotspot y obtuvo `10.116.0.2`,
gateway `10.116.0.1`. Antes del ensayo apareció un lateral Oeste de Grillo
hacia Ramona. La captura nueva `ensayo/` registra Root, Ramona, Este de
Grillo y pings vinculados a Wi-Fi 3; `control/` registra COM7. Estos cuatro
monitores quedan separados de la carga de firmware.

El procedimiento `controlar-alternativo-tardio.py` de esa carpeta registra
COM7 mientras el capturador conserva los otros tres UART y pings. Exige el
hash nuevo y cinco placas listas, 120 segundos de referencia con pings
saludables y las rutas principal y lateral presentes.

Retira el lateral durante 60 segundos y espera hasta 15 segundos a observar
una tabla nueva con solo el principal y la ruta local. Si aparece otra salida
antes de cumplirlo, restaura el lateral y termina sin cortar el principal.
Si se cumple, detiene el principal durante 180 segundos: el lateral puede
volver después y participar en una recuperación que no existía al caer el
principal. Observa 300 segundos desde el ACK del corte principal.

Para esta topología se seleccionan `--primary east --alternate west`.
El modo `--no-alternate-initial` permite omitir la retirada previa si la
tabla y un snapshot de los cuatro vecinos confirman que solo existe el
principal. Si el lateral no acepta el comando de corte, no se envía el
corte principal. Un ACK por sí solo no acredita la caída del enlace.

En caso de error intenta restaurar las radios intervenidas; los plazos
automáticos permanecen limitados aunque la conexión UART falle. Se conserva
la distinción entre ACK de encolado y evidencia de corte real en la radio
monitorizada. La retirada del lateral se comprueba por la tabla del central.

La clasificación final requiere comprobar cuándo aparece el nuevo vecino,
la ruta de ida y retorno, las respuestas a RPi e Internet antes de restaurar
el principal, la misma IPv4 y ausencia de reinicios en los monitores. Si otra
radio conecta directamente a Root, registrarlo como un escenario diferente;
no atribuirlo a recuperación por el camino más largo a través de Ramona.

## Preparación registrada, 12:10–12:13 de Argentina

La primera referencia (`ensayo/` y `control/`) terminó sin cortar el enlace
principal. Los pings desde `10.116.0.2` recibieron 165/165 respuestas a cada
destino: gateway, RPi e Internet. La consulta de estado confirmó las cinco
placas de Grillo con ELF `143a472a…`, listas y sin recuperación pendiente.

A las 12:12:36, Oeste rechazó el comando de retirada del lateral con
`ESP_ERR_INVALID_STATE` (`0x103`). Ese extremo funciona como AP, mientras
el comando exige una estación admitida. El controlador canceló el ensayo
y envió restauración preventiva; no envió el corte de Este. No se observan
eventos `I4ACUT` en la captura. Este rechazo no es un fallo de recuperación
de rutas: la condición inicial del ensayo no pudo establecerse.

El paquete instalado en Ramona, ELF `a9e46318…`, tampoco incluye el comando
de corte: se comprobó su `sources.zip`, que carece de `node_radio_cut` y
`NODE_OTA_RADIO_CUT`. Se solicitó apagar sus cinco placas para repetir con
`--primary east --no-alternate-initial --trial -2`; se pedirá encenderlas
durante el corte del principal. Esto evita cambiar su firmware para la prueba.
Las carpetas iniciales se conservan cerradas y cualquier repetición usa
directorios nuevos. Hasta completar esa secuencia, el caso tardío sigue
pendiente de validación en hardware.

## Segunda corrida: sin vecino lateral al cortar

El usuario confirmó Ramona apagada. Se iniciaron `ensayo-2/` y `control-2/`,
con los mismos cuatro puertos y pings ligados a `10.116.0.2`. El controlador
comprobó las cinco placas listas, 120 segundos de referencia saludable,
tabla con solo ruta local y default Este, y los cuatro vecinos: únicamente
Este estaba admitido, hacia Root. No se retiró ningún lateral por comando.

Este confirmó `cut_applied` a las **12:17:20.244** de Argentina
(`t_ms=1015016`), con temporizador de 180 segundos. A continuación se solicitó
encender Ramona. Antes de su incorporación, Grillo admitió un enlace nuevo
directo a Root por Norte y recuperó la ruta por defecto. La primera respuesta
de Internet llegó a las **12:17:36.473**, 16.229 segundos después del corte
aplicado; RPi respondió a las **12:17:36.961**. Cada destino externo perdió
ocho consultas en ese intervalo; el gateway local siguió respondiendo.

Ramona reconectó COM8 a las **12:18:23.689**, mostrando unos 18 segundos de
uptime: el USB no capturó sus primeros bytes de arranque. El usuario confirmó
que sus cinco placas estaban encendidas y se agregó el marcador correspondiente.
No confundir el instante de confirmación con el instante físico de encendido.

Esta corrida sí reproduce la recuperación con un enlace **no admitido al
caer el principal**, de distancia igual por otro radio hacia Root. No demuestra
la recuperación por el camino más largo a través de Ramona: Norte fue admitido
antes. La validación de ese caso requiere evitar los otros enlaces directos
a Root y comprobar que Ramona dispone de una salida independiente de Grillo.
Su incorporación posterior por sí sola no establece esas condiciones.

## Falla posterior al incorporar Ramona: bloque duplicado

La primera recuperación fue real pero **no persistió**. A las 12:19:01.054,
Root cambió su ruta de retorno `10.96.0.0/11` de Sur (hacia Norte de Grillo)
a Este (hacia Ramona). Las consultas a RPi e Internet volvieron a fallar
desde aproximadamente las 12:19:02 hasta cerrar la captura a las 12:22:50.
La restauración de Este de Grillo no corrigió esa segunda falla.

Las tablas de los centrales de Grillo y Ramona muestran la misma red local
`10.116.0.0/14` por salida 5. El código explica el resultado:

1. El bloque delegado depende del sector proveedor: Este de Root ofrece
   `10.96.0.0/11` en `on_peer_connected` mediante `device_network`.
2. Grillo conserva ese bloque al recuperar un gateway por otro radio. Ese
   comportamiento conserva la IP del cliente y es el esperado para movilidad.
3. Al quedar libre Este de Root, una Ramona recién iniciada recibe otra vez
   el mismo bloque: la provisión no consulta su ocupación por otro nodo.
4. El handshake de Ramona anuncia ese prefijo y `routing_table_add` reemplaza
   la salida de la entrada existente. Root deja de devolver los paquetes a
   Grillo. Ambos centrales además derivan el mismo sector de clientes /14.

`repro-colision.py`, guardado con la evidencia, compila un diagnóstico con
los callbacks C reales de la versión actual y los mocks existentes. Reproduce
la movilidad de Grillo conservando su bloque, la nueva oferta de Root Este,
la duplicación del bloque y el reemplazo de la ruta de retorno. El resultado
`repro-colision-resultado.json` confirma la reproducción sin UART ni cambios
en los dispositivos. Es un diagnóstico que confirma un defecto existente,
no una prueba de corrección ni una regresión añadida a la suite normal.

### Resultado final medido

| Destino | Respuestas/consultas | Resultado |
| --- | --- | --- |
| Gateway local | 467/467 | Sin interrupción |
| RPi | 223/346 | 8 pérdidas al cortar y 115 en la segunda falla |
| Internet | 223/346 | 8 pérdidas al cortar y 115 en la segunda falla |

La radio Este estuvo apagada **180.096 segundos**, medidos por `t_ms`; confirmó
`radio_restored` a las **12:20:20.449**. Se conservó la IP `10.116.0.2`.
No se observaron retrocesos de uptime en Root central, Grillo central ni
Este de Grillo, ni reinicios adicionales de Ramona central desde su conexión
USB después del encendido solicitado. Esto no mide las otras once placas.

Los incrementos CRC en los cuatro monitores fueron Root=4, Grillo central=7,
Este de Grillo=0 y Ramona central=5. Ramona central acumuló 29 eventos
`no_buffer`; los otros tres no aumentaron ese contador. No aumentaron las
tramas cortas, colas llenas ni errores TX. Esas pérdidas deben conservarse
como evidencia de transporte; no explican que dos nodos reciban el mismo
bloque, que también se reprodujo sin transporte en el diagnóstico nativo.

Al finalizar, las quince placas respondieron a INFO y quedaron listas, sin
verificación/recuperación pendientes ni pausa de arranque. Root conserva
ELF `3e6065c8…`, Ramona `a9e46318…` y Grillo `143a472a…`.
La segunda falla seguía presente al cerrar los pings: la lectura de estado
normal de firmware no acredita conectividad a Internet.

## Corrección pendiente y condiciones para repetir

La provisión debe conservar la propiedad de un bloque entre enlaces y
evitar delegarlo a otro UUID mientras su propietario lo utiliza. Una radio
libre no implica que el bloque que ofrecía esté libre. La solución debe
resolver también el rechazo o la reasignación de una provisión conflictiva,
permitiendo que el solicitante busque otra salida; ocultar el anuncio o
conservar arbitrariamente la primera ruta no evita dos clientes con la misma IP.

Antes de otra carga o corte se necesita cubrir la reserva compartida entre
radios, movilidad del propietario sin renumeración, incorporación de un nodo
nuevo al puerto original y solicitudes simultáneas/pérdidas SPI. Para probar
el camino tardío más largo también se debe impedir el atajo directo a Root
y verificar que Ramona tiene un uplink independiente de Grillo.

No se aplicó una modificación de asignación de direcciones ni se cargó nuevo
firmware en Root o Ramona durante esta corrida. El problema queda documentado
con sus capturas completas para resolverlo sin atribuir un éxito total a la
primera recuperación ni confundir el reinicio solicitado de Ramona con una falla.
