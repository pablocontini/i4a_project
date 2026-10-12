# Revisión del lateral y la conmutación de rutas

Se revisó el código y la prueba de PC del 10/10/2026. No se modificó ni
reinició el firmware de los nodos durante esta revisión.

## Resultado físico conocido

Con IP de PC 10.52.0.3 constante y rutas verificadas por Wi-Fi 3, el
gateway respondió durante el corte. La RPi e internet no respondieron
hasta recuperar el norte original. Esto no valida la conmutación lateral.
Los logs `peer_lost` actuales prueban pérdida del transporte; no prueban
que la cola de ruteo haya procesado el evento y retirado el siguiente salto.

## Reglas de admisión

El sur de Ramona puede asociarse al sur de Grillo. La oferta Grillo sur
10.104.0.0/14 es mayor y más específica que Ramona 10.32.0.0/11.
La protección por UUID permite vecinos distintos (root y Grillo).
No se encontró una regla de máscara que prohíba este lateral.

El ruteo previsto es:

| Nodo | Salida de tráfico hacia RPi/internet tras el corte | Retorno hacia Ramona |
| --- | --- | --- |
| Ramona | Sur (orientación de ruteo 2) | Central/hotspot |
| Grillo | Este (3) | Sur (2), bloque 10.32.0.0/11 |
| Root | Central (5), hacia RPi | Este (3), bloque 10.32.0.0/11 |

Las orientaciones de ruteo son 1–5; los roles GPIO/UART son 0–4.
No confundir ambos valores.

## Problemas confirmados en el código

1. El líder marca el token de sincronización como entregado antes de
   transmitirlo e ignora el resultado de `rs_broadcast`. Si no vuelve el
   token, las nuevas solicitudes solo acumulan un contador y no generan
   otro turno. No hay expiración de ese token. Se reprodujo con el
   `leader.c` real: ante envío fallido o token no entregado, 15 nuevas
   solicitudes no produjeron otro grant. El control con devolución del
   token sí produjo el segundo grant.
   Ver [leader.c](../components/routing/sync/src/leader.c).

   También se reprodujo la pérdida de la primera solicitud de token de
   un PEER_LOST: 120 llamadas reales a rt_on_tick(1000) no vuelven a pedir
   el turno mientras el estado sigue WITH_NETWORK y no tiene timer activo.
   El evento queda en cola y la salida norte, DTR 2 y estado CONNECTED
   permanecen sin retirar. Al entregar el grant en el control, el evento
   se procesa, se retira el norte y comienza la recuperación. Ver
   [routing.c](../components/routing/routing/src/routing.c).

2. La publicación de la tabla compartida ignora el fallo de transmisión.
   Una radio puede cambiar su tabla y el resto conservar la anterior.
   Esto es un camino confirmado por inspección, no un fallo demostrado
   en la captura física. Ver
   [shared_state.c](../components/routing/shared_state/src/shared_state.c).

3. El AP conserva un flag de servidor levantado distinto del flag interno
   del servidor TCP. Si falla la creación/socket/bind/listen, solo se limpia
   el flag interno: el AP puede conservar asociación WiFi sin listener ni
   reintento de arranque. Su controlador también evita buscar mientras ese
   flag del AP esté activo. No hay evidencia de que ocurriera en Grillo
   durante este ensayo. Ver
   [access_point.c](../components/wireless/access_point/access_point.c) y
   [server.c](../components/wireless/server/server.c).

No regenerar un token ante cualquier timeout sin identificar su sesión y
secuencia: un envío que informa fallo podría haber sido recibido por parte
del anillo. El diagnóstico debe distinguir entrega fallida, devolución
perdida y callback que sigue ejecutándose.

## Límite de la telemetría actual

Cada placa actualiza su fila cada cinco minutos; el emisor UDP reenvía la
copia completa cada minuto. No hay edad por fila. RSSI/SSID y contadores
pueden describir un estado anterior al corte. Un SSID propio de AP tampoco
prueba una sesión TCP admitida. La renovación de la reserva UUID y el
handshake de ruteo son comprobaciones adicionales.

Los logs guardados de este ensayo muestran el norte de Ramona, no la
admisión/renovación de ambos extremos sur del lateral.

## Diagnóstico necesario antes de atribuir la causa

- Capturar en la RPi ICMP de 10.52.0.3 durante un corte breve. Solicitudes
  presentes sin retorno al cliente apuntan a la ruta de respuesta; solicitudes
  ausentes apuntan a salida de Ramona/enlace intermedio. No concluir solo
  a partir de timeouts en Windows.
- Confirmar en ambos sur sesión admitida, reserva ACTIVE y renovación.
- Registrar encolado y ejecución de PEER_LOST, grants/devoluciones de
  RS_ROUTING, salida por defecto, envío/respuesta de búsqueda de gateway y
  propagación de tabla. Los `turn_*` actuales son turnos de descubrimiento,
  no el token RS_ROUTING.
- Verificar en el root que la entrada 10.32.0.0/11 cambia de norte a este.

La prueba nativa local está en build/firmware/routing-token-failure-proof.c;
la prueba de solicitud perdida está en build/firmware/routing_lost_request_proof.py.
las capturas del ensayo están descritas en
[prueba-recuperacion-pc-20261010.md](prueba-recuperacion-pc-20261010.md).

## Estado posterior durante la revisión

Se intentó preparar un corte breve con captura ICMP de la RPi, pero no se
aplicó porque la referencia previa ya fallaba: gateway 4/4, RPi 0/4,
internet 0/4, con IP de PC 10.52.0.3 y rutas verificadas. Ramona respondió
a INFO en las cinco placas, ready=1 y boot_paused=0. Esto confirma vida
de las aplicaciones y SPI, no salud del enlace con la RPi ni tablas de ruteo.
Se solicitó comprobar la interfaz de la RPi y su ping al root, conservando
el estado sin reiniciar.

La captura recibida a continuación (21:23:57–21:24:03) muestra cuatro
solicitudes a la RPi y cuatro a 1.1.1.1 desde 10.52.0.3, junto con sus
respuestas saliendo por wlan0. Eso prueba ida y respuesta generada en
la RPi durante esa referencia, no recepción efectiva en Windows ni
camino de retorno por todos los nodos. No hubo corte nuevo en ese intervalo.

Más tarde, sin reiniciar ni cambiar las rutas, ping.exe respondió 4/4
y luego 8/8 para ambos destinos; en la comparación simultánea el monitor
.NET también respondió 8/8 para cada uno de los tres destinos. No se
demostró un error de la herramienta de ping; se conserva la hipótesis de
falla intermitente de retorno hasta contar con captura de recepción en
la PC y estados de tablas. Evidencia local:
build/firmware/pc-route-review-baseline-20261010.csv y
build/firmware/pc-monitor-crosscheck-20261010.csv.
