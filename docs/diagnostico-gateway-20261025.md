# Diagnóstico de solicitudes de gateway y repetición

Se instrumentó la etapa pendiente del [análisis de demora](analisis-demora-gateway-20261025.md).
No se cambian los mensajes del protocolo, la validación del camino ni los
plazos de 30 segundos de reintento y 10 segundos de respuesta en Root.

## Registro

Los callbacks depositan registros de tamaño fijo en una cola de 32 entradas
por ESP, sin imprimir ni reservar memoria dinámica. La tarea de captura
drena lotes de cuatro registros fuera de los locks de routing y transporte.
La cola usa el mutex de eventos durante la copia, nunca durante UART.
Si se llena, descarta el nuevo registro, mantiene el orden de los anteriores
y acumula `dropped`; no bloquea esperando espacio. La tarea imprime como
máximo 32 registros por vuelta. No deben considerarse completos los eventos
de un intervalo donde aumentó `dropped`.

`I4AGW` incluye `t_ms` del evento, orientación, secuencia local, evento,
motivo, valor, DTR local, estado, plazo pendiente, huella del camino y saltos.
La hora del host corresponde al drenaje posterior; para intervalos en el
mismo ESP se usa `t_ms`. La identidad periódica permite resolver el UUID.
Las secuencias son locales de observación: **no son IDs de transacción
extremo a extremo**. Las huellas permiten comparar caminos, con la limitación
de colisiones de un hash de 32 bits; tampoco distinguen dos reintentos del
mismo camino sin consultar los tiempos y eventos. No se cambió el wire format.

| Evento | Significado de `value` |
|---|---|
| 1 RX_PEER | ID del evento de peer procesado |
| 2 RX_SIBLING | ID del evento SPI procesado |
| 3 REQUEST_TX | Resultado booleano del envío local de solicitud |
| 4 RETRY_TX | Resultado booleano del reintento |
| 5 RESPONSE_TX | Resultado booleano del envío local de respuesta |
| 6 ROOT_WAIT | Plazo inicial de respuesta en ms |
| 7 ROOT_RESPONSE | Resultado de publicar la respuesta por SPI |
| 8 DECISION | DTR anunciado cuando corresponde; cero en decisiones de camino |
| 9 HANDSHAKE_TX | Resultado booleano del envío local de handshake |

En RX_PEER, los IDs 1–6 corresponden a HANDSHAKE, UPDATE_DTR,
NEW_GATEWAY_REQUEST, NEW_GATEWAY_RESPONSE, CONNECTED y LOST.
En RX_SIBLING, 1–5 corresponden a ON_START, UPDATE_DTR, PROVISION,
SEND_NEW_GATEWAY_REQUEST y NEW_GATEWAY_WINNER.

Los motivos 0–11 son: OK, NO_NETWORK, BAD_NETWORK, UNREACHABLE,
PENDING, LOOP, PATH_FULL, NOT_CONNECTED, NOT_BETTER, ADOPT, FORWARD,
ALREADY_PENDING.
Para eventos de recepción se registra el momento de procesamiento; una
demora previa de la cola se contrasta con `I4ACAP event=coord`, no se mide
directamente como latencia de recepción. El booleano de envío no es ACK
extremo a extremo. El estado/plazo del central solo es interpretable como
estado de Root cuando ese central cumple ese rol.

## Validación local

Pasaron **36 pruebas nativas de routing** (34 anteriores y dos nuevas).
Las nuevas comprueban saturación, descarte contado, orden, secuencia,
huella, drenaje y registro de solicitud/reintento/ganador conservando el
plazo de 30000 ms y el resultado de selección de gateway.
Los dos paquetes ESP-IDF se compilaron correctamente.
También pasaron las 38 pruebas de vecinos/transporte, 24 OTA, 7 wireless
y 5 hooks de routing: **110 pruebas nativas** en total.

## Paquetes y carga

| Paquete | ELF SHA256 |
|---|---|
| `grillo-gateway-diag-20261025` | `57bb3d29051e763cecb175eb700d6c92894ba8065e4b14f8bf1c01f7499817fc` |
| `ramona-gateway-diag-20261025` | `d033c9e5254dc0b5edfcd8bd762773f20a0820cda2641327f792f857d82c1e58` |

Ambos se guardan en `app/build/link-capture/`, con binario, ELF, configuración,
fuentes y manifiesto. Root utiliza el paquete de la variante Grillo;
la configuración persistida determina su identidad y modo Root. Se debe
verificar UUID Root después de la carga, además del hash en los cinco roles.

Trabajo y registro OTA:
`app/build/gateway-diag-20261025-233002`. El script de esa carpeta verifica
los cinco hashes y estados previos, el manifiesto del binario y los cinco
roles finales. No borra NVS. Las actualizaciones son secuenciales; los
reinicios y recuperación de arranque de OTA no forman parte del ensayo.

Estado inicial: COM5 central Grillo; COM7 Este Grillo; COM8 central Root
(UUID `000000000000` confirmado); COM9 Este Ramona, pendiente de pasar
temporalmente al central para OTA. Wi-Fi 3 mantiene `10.148.0.2`.

Las tres actualizaciones terminaron verificadas. El ensayo queda pendiente
de preparar Ramona apagada y verificar topología y referencia de conectividad.
No se ha enviado un nuevo corte en esta etapa.

### Avance de carga

- **23:39:46:** las cinco placas de Grillo quedaron verificadas con hash
  `57bb3d29…99817fc`, slot `0x210000`, listas, sin pausa ni recuperación.
  El segundo arranque coordinado es parte de OTA y está registrado.
- **23:39:47:** comenzó Root en COM8 después de la verificación anterior.
- COM9 se movió de Este a central Ramona por instrucción del usuario.
  La captura `monitores-actualizacion` contiene esa transición marcada;
  no interpretar su etiqueta anterior como identidad física posterior.
- Grillo se provisionó por Ramona tras reiniciar; Wi-Fi 3 cambió de
  `10.148.0.2` a `10.110.128.2`. Las sondas de `monitores-ota` estaban
  fijadas al origen antiguo, por lo que se marcó su invalidez para evaluar
  el tráfico posterior. La futura captura debe resolver nuevamente el
  origen y gateway antes de comprobar la referencia.
- **23:49:14:** las cinco placas de Root quedaron verificadas, hash
  `57bb3d29…99817fc`, slot `0x20000`, listas y sin recuperación ni pausa.
  El central conservó UUID `000000000000`.
- **23:59:23:** las cinco placas de Ramona quedaron verificadas, hash
  `d033c9e5…d82c1e58`, slot `0x20000`, listas y sin recuperación ni pausa.
  `despliegue-verificado.json` conserva los quince estados finales.
- Se cerraron las capturas de actualización y se solicitó apagar todas las
  placas de Ramona, incluido USB, manteniendo Root y Grillo. Falta la
  confirmación del usuario y una referencia nueva antes de aplicar el corte.
  El ensayo pendiente se hará el 26 de octubre; los nombres de paquetes
  conservan la fecha de preparación del 25.

## Repetición del 26 de octubre: en curso

Carpeta: `app/build/gateway-diag-20261025-233002/ensayo-20261026-001906`.
El usuario confirmó Ramona completamente apagada y COM9 desapareció.
La referencia previa seguía activa y confirmó gateway, RPi e Internet
desde Wi-Fi 3 `10.110.128.2`. Se abrió una captura nueva, con esa dirección
como origen fijo, y monitores Este Grillo COM7, central Root COM8 y futuro
Este Ramona COM9. El central de Grillo COM5 lo registra el controlador.

Grillo tenía salida 1, Este libre en modo estación y Root reconocía a Grillo
como vecino. Los cinco roles de Grillo confirmaron estado normal y el filtro
de 600 segundos. Tras 35 segundos de referencia, el corte de Norte por 300
segundos recibió ACK a las **00:20:06.022**. Grillo pasó a default cero.
Se solicitó encender Ramona y volver a conectar **Este**, puesto que para
OTA se había usado su central. La restauración está prevista alrededor de
las **00:25:06**, seguida de cancelación del filtro por el controlador.

Norte no tiene USB en este montaje de cuatro monitores: no se dispone de
su evento físico `cut_applied`. El ACK confirma encolado; la pérdida de
gateway en Grillo confirma el efecto en routing. Los tiempos de esta
corrida se referencian al ACK, con esa limitación, y no se equiparan al
reloj de aplicación física usado en la corrida anterior.

### Recuperación observada con diagnóstico

COM9 confirmó Este Ramona (`board=3`). El usuario confirmó encendido a las
00:21:50; el registro ya mostraba su arranque. Ramona pasó transitoriamente
por default cero a las 00:22:07.925 y recuperó default 1 a las 00:22:20.296,
antes de asociarse lateralmente con Grillo. No se atribuye esa transición
al enlace lateral, que todavía no estaba admitido.

Este Grillo incorporó el bloque de Ramona a las **00:22:37.500**, y el
central lo mostró a las 00:22:38.344. Grillo recuperó por Este y las primeras
respuestas fueron **00:22:52.506 RPi** y **00:22:52.507 Internet**: respectivamente
166,484 y 166,485 segundos desde el ACK del corte. El total incluye la
espera del usuario, arranque y descubrimiento. La IP cliente se mantuvo en
`10.110.128.2`; el filtro seguía activo y Norte todavía estaba cortado.

Los eventos `I4AGW` permiten medir el intercambio sin depender de la hora
de drenaje de UART:

| ESP | Evento | `t_ms` | Resultado |
|---|---|---:|---|
| Grillo Este | REQUEST_TX, secuencia 30 | 2599445 | Envío local aceptado, plazo 30000, camino `e2ee97cd`, 1 salto |
| Ramona Este | RX_PEER request, secuencia 8 | 94550 | Camino recibido `e2ee97cd`, 1 salto |
| Root central | RX_SIBLING request, secuencia 7 | 2046063 | Camino `95cdf515`, 2 saltos |
| Root central | ROOT_WAIT, secuencia 8 | 2046063 | Plazo inicial 10000 |
| Root central | ROOT_RESPONSE, secuencia 9 | 2055138 | Publicación SPI aceptada |
| Grillo Este | RX_PEER response y ADOPT, secuencias 31–32 | 2614309 | DTR vecino 2, DTR adoptado 3, plazo cancelado |

La duración exacta dentro del reloj de Grillo Este es **14,864 segundos**
entre envío y aceptación. En Root transcurrieron **9,075 segundos** entre
procesar la solicitud y registrar la publicación. La constante sigue siendo
10000 ms: el plazo consume ticks acumulados de routing, incluso el lote
procesado junto con la solicitud; no equivale a esperar exactamente diez
segundos desde la recepción en tiempo real. Esta medición identifica ese
efecto de temporización, sin haber cambiado el código de decisión.

Los restantes **5,789 segundos** de la duración medida corresponden al
conjunto de transporte y procesamiento fuera de esa espera de Root; no se
pueden repartir entre los enlaces con estos relojes no sincronizados.
El booleano de publicación/enviado no acredita por sí solo entrega remota,
pero la recepción posterior y recuperación de tráfico sí completan el caso.

Hasta las 00:24:10 no se registraron reintentos REQUEST_RETRY_TX ni desbordes
del registro en los cuatro ESP capturados. El handshake de Grillo se procesó
antes de CONNECTED: aparece un primer rechazo NOT_CONNECTED, seguido de la
solicitud emitida 142 ms después. No produjo una espera de 30 segundos.

En esta corrida **no se reproduce la demora anterior de 45–47 segundos**
desde incorporar la ruta lateral: fueron unos 15 segundos. La hipótesis de
un reintento perdido en la corrida vieja sigue sin demostrarse. Se confirma
que la espera configurada en Root contribuye sustancialmente a la latencia
normal del intercambio, y que el reintento largo no se activó en este caso.
No corresponde reducir temporizadores ni quitar validación de caminos
basándose en la supuesta reproducción de un fallo que aquí no ocurrió.

### Cierre del ensayo

El controlador terminó a las **00:27:06.542**, sin fallos, con
`root_guard_controlled=true` y `root_guard_cleared=true`. Los cinco roles
de Grillo confirmaron estado normal al final. El filtro quedó cancelado
por los cinco ACK a las **00:25:21.270**, y sus muestras posteriores indican
cero. Se cerraron los monitores y se recalculó el análisis final.

| Destino | Sondas | Respuestas | Pérdidas |
|---|---:|---:|---:|
| Gateway Grillo | 496 | 496 | 0 |
| RPi | 407 | 317 | 90 |
| Internet | 410 | 324 | 86 |

Las últimas cinco sondas de cada destino respondieron. Hubo pérdidas
aisladas además del corte principal; no se informa ausencia total de pérdidas.
No hubo regresiones de uptime en los cuatro ESP monitorizados, después
del encendido solicitado de Ramona. Esto no prueba individualmente la
ausencia de reinicios en las quince placas.

Grillo terminó con default 3 por Ramona, y Root con rutas a ambos bloques
por su salida 2. Los clientes conservaron las redes de referencia de esta
corrida: Grillo `10.110.128.0/17` y Ramona `10.116.0.0/14`. La provisión
inicial de Grillo por Ramona ocurrió antes de esta referencia, durante OTA;
no se debe compararla como un cambio de direcciones durante el corte.

No se dispone del evento físico de restauración de Norte al no tener su
UART. Se envió además una orden explícita de restauración al cerrar el
ensayo, como cancelación preventiva del corte temporal; se registra aparte
en `restauracion-norte`, sin mezclarla con la ventana de medición.

Queda cumplida la repetición con diagnóstico y camino alternativo tardío.
La espera larga original continúa como hipótesis de una solicitud o respuesta
fallida: esta corrida no acredita su causa. El siguiente cambio de rendimiento
debe evaluarse por separado, conservando la validación de camino y probando
pérdidas deliberadas, no simplemente reduciendo constantes tras este caso.
