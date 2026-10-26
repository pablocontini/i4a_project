# Análisis de la demora al recuperar por Ramona

Se revisó la segunda corrida controlada, carpeta
`app/build/guard-root-20261025-200718/revision2/ensayo-norte-20261025-215205`.
Todos los horarios siguientes son de Argentina (UTC−3). Se conservaron
sin modificar el ZIP sellado y los registros originales.

## Qué midieron realmente los 46 segundos

| Evento observado | Hora | Tiempo hasta primera respuesta de Internet |
|---|---|---:|
| Ramona Este informa un cliente Wi-Fi | 21:55:56.030 | 55,925 s |
| Grillo Este informa asociación a BSSID `841FE8271E99` | 21:55:56.478 | 55,477 s |
| Grillo Este incorpora bloque `10.96.0.0/11` por salida 3 | 21:56:04.718 | 47,237 s |
| Central Grillo incorpora ese bloque | 21:56:06.635 | 45,320 s |
| Ramona Este recupera default 1 y ruta a Root | 21:56:14.545 | 37,410 s |
| Primera respuesta de Internet y RPi | 21:56:51.955 | 0 s |
| Central Grillo muestra default 3 | 21:56:52.205 | Muestra posterior a la respuesta |

La referencia anterior a «46 segundos desde establecer el enlace» era
imprecisa: mide desde incorporar la ruta lateral, no desde la asociación
Wi-Fi. Desde la primera asociación observada fueron unos **55,5 segundos**.
Las rutas se muestrean aproximadamente cada dos segundos; las horas son
las de observación del host y no tiempos exactos de ejecución del evento.

## Qué sostiene la captura

Grillo tenía la ruta al bloque de Ramona pero carecía de gateway. El
problema restante era recuperar el camino a Root, no obtener dirección
del cliente: Wi-Fi 3 mantuvo `10.148.0.2`, y el gateway local respondió
493/493 sondas. El enlace lateral mantuvo BSSID y canal 8 en las muestras
entre 21:56:00 y 21:56:52, con RSSI aproximadamente −7 a −9 dBm.

Ramona tampoco mantuvo una salida estable durante toda la negociación:
pasó de default 1 a **0 a las 21:56:06.303**, después de incorporar Grillo,
y volvió a default 1 a las 21:56:14.545. Por tanto, no fueron 46 segundos
con ambos nodos ya preparados para transportar Internet.

En Grillo Este las secuencias de coordinación siguieron avanzando durante
la espera: 21538 a las 21:56:06 y 21725 a las 21:56:50; las muestras no
muestran cola persistente, publicación pendiente ni acumulación sostenida
de ticks. Esto contradice un bloqueo continuo de 46 segundos en ese ESP,
aunque no excluye pérdidas o esperas entre muestras o en otras placas.

En Ramona Este sí hubo una espera anterior: secuencia 113 sin avanzar
entre 21:55:49 y 21:55:56, seguida de secuencia 114, destino 2 y un mensaje
de peer en cola hasta 21:56:04. Se reanudó antes de las 21:56:06. Había
ticks acumulados de hasta 12 segundos. Esa espera puede retrasar el
handshake; las muestras no identifican qué placa o entrega la provocó.

Entre 21:56:00 y 21:56:52, Este Grillo y Este Ramona no incrementaron CRC,
tramas cortas ni pérdidas de cola. El central de Grillo incrementó CRC
interno de 143 a 144, sin aumentar `no_buffer` ni `queue_full`. No puede
identificarse ese paquete como una solicitud o respuesta de gateway.
No se monitorizaron los cinco ESP de Root ni Norte de Ramona.

El filtro Root seguía activo (unos 331 segundos restantes al recuperar),
y Norte Grillo permaneció cortado hasta 21:58:07.649. La recuperación no
fue provocada por la expiración del filtro o la restauración del principal.

## Mecanismo del código que puede explicar la espera

Se compararon con `sources.zip` del paquete Grillo desplegado los archivos
`forwarder.c`, `forwarder.h`, `peer.c`, `sibling.c` y `root.c`: coinciden
con los actuales, normalizando finales de línea. Root y Ramona conservaron
otros paquetes; no se deduce su versión completa de esa comparación.

1. `forwarder/peer.c:on_handshake` agrega la ruta lateral y llama a
   `on_update_dtr`. Ante default ausente y DTR del vecino mayor que uno,
   solicita un gateway confirmado; no adopta sin comprobación un anuncio
   que podría depender del propio nodo. Esa protección evita bucles.
2. `forwarder/sibling.c:on_send_new_gateway_request` guarda el camino,
   pone DTR a cero y arranca `GATEWAY_REQUEST_RETRY_MS = 30000`.
   Un anuncio posterior no adelanta una solicitud pendiente.
3. `forwarder/forwarder.c:on_tick` repite esa solicitud al vencer el plazo.
   Los envíos inalámbricos devuelven un booleano que aquí no se utiliza
   para acortar el reintento cuando falla el envío local.
4. `forwarder/peer.c:on_new_gateway_request` puede poner también al nodo
   intermedio en estado de búsqueda con DTR cero. Esto ofrece una
   explicación coherente con la transición temporal de Ramona a default
   cero, pero faltan eventos de protocolo para demostrar esa causalidad.
5. `root.c` contiene una espera de **10 segundos** para responder solicitudes
   y reintenta una publicación SPI fallida tras 5 segundos. Las esperas
   se consumen al procesar ticks bajo coordinación; no son un cronómetro
   exacto independiente de la entrega del token.

La hipótesis principal es una primera confirmación fallida, perdida o
insuficiente mientras Ramona completaba su propia recuperación, seguida
por el reintento de 30 segundos y la confirmación desde Root. El orden
de magnitud es compatible con 30 + 10 segundos y procesamiento adicional.
**No se observó directamente ese reintento ni puede afirmarse que la
demora sea exactamente la suma de esos temporizadores.** La captura no
incluye IDs, emisión/recepción y motivos de rechazo de cada NGR/NGR response,
ni DTR y plazo pendiente de cada radio. No distingue pérdida de solicitud,
pérdida de respuesta, rechazo o retraso en Root.

## Cambios convenientes y siguiente verificación

Antes de reducir temporizadores, conviene instrumentar eventos de control
con UUID/orientación, identificador de solicitud, camino anunciado, DTR,
resultado del envío y motivo de aceptación o rechazo. Registrar también
inicio, vencimiento y cancelación del plazo, sin imprimir dentro de locks
de routing o del transporte. Una captura de Norte Ramona o Root central
complementaría los monitores Grillo central, Grillo Este y Ramona Este.

Con esos datos, repetir la misma alternativa tardía y comparar además con
Ramona ya lista. Así se separa la espera de arranque de la recuperación de
gateway. La instrumentación debe ser acotada para no introducir congestión
de UART ni alterar la coordinación.

Dos mejoras candidatas para evaluar mediante pruebas con pérdida son:

- Reintento más temprano y acotado cuando el envío devuelve fallo local,
  sin confundir entrega local con confirmación extremo a extremo.
- Adelantar una nueva comprobación cuando aparece una señal verificable
  de recuperación del vecino, conservando validación del camino, límite
  de frecuencia y protección frente a anuncios repetidos.

No conviene aceptar directamente un DTR mayor ni quitar la validación de
camino para ganar tiempo: se perdería la protección frente a bucles. Tampoco
se justifica reducir globalmente 30 o 10 segundos basándose en una corrida.

Este trabajo es análisis de evidencia y código: **no se modificó el firmware,
no se reflasheó y no se inició otro corte**. Queda identificada la etapa
responsable (confirmación/propagación del gateway), con hipótesis concreta
y límites de evidencia, pero no un paquete perdido o rechazo específico.
