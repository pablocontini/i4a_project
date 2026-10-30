# Cambios de enlace al incorporar o reiniciar un cuarto nodo

## Síntoma informado y alcance

El 30 de octubre se informó que, con cuatro nodos, incorporar o reiniciar uno
modifica enlaces que ya estaban formados. El ejemplo es un enlace Norte hacia
la raíz con RSSI de aproximadamente −2 dBm que luego aparece por Sur con
aproximadamente −50 dBm. No se dispone de una captura de esa transición ni de
acceso físico a los equipos durante este análisis. Ese cambio no demuestra por
sí solo un reinicio ni una interrupción de Internet.

El análisis parte del código de la rama `feat/config-portal-stability`, sobre
el commit `19ed090b8d1047051fcc577e2726805abda761b5`. Los defectos descritos
abajo se reproducen ejecutando código C real con transporte y reloj simulados;
su participación en el incidente informado queda pendiente de captura.

## Cómo se selecciona un enlace

Hay dos decisiones distintas:

1. Una radio libre busca un AP permitido y elige el de mayor señal entre sus
   candidatos. Los turnos iniciales entre radios también consideran señal.
2. Con enlaces ya admitidos, routing elige la salida según distancia a la raíz,
   no según RSSI. Un empate conserva una salida válida.

Una estación Wi-Fi activa deja de escanear. El ciclo AP/estación respeta las
asociaciones y sesiones existentes. La admisión impide mantener dos enlaces
activos hacia la misma identidad de nodo: una reserva nueva no desplaza una
sesión confirmada. No existe una orden normal que obligue a abandonar un
enlace sano al encender otro nodo.

Si una sesión termina y libera su reserva, otra radio puede ocuparla antes de
que la primera vuelva a admitir su conexión. La radio que perdió esa carrera
no vuelve a desplazar a la ganadora solo por tener mejor señal. Esto explica
por qué un enlace más débil puede permanecer después del cambio, pero hace
falta identificar qué terminó la sesión original.

No se encontró un límite de tres nodos: el registro admite cuatro vecinos
distintos por nodo. El agotamiento de bloques rechaza nuevas admisiones sin
reutilizar bloques de propietarios vivos. No se modifican esos límites ni se
introduce una política de cambio continuo por RSSI en esta corrección.

## Defectos reproducidos y correcciones

### Una pérdida de renovación podía liberar un enlace todavía válido

`peer_session.c` renovaba cada cinco segundos la concesión de vecino mediante
el central y cerraba la sesión ante una sola respuesta fallida. El RPC SPI
puede fallar aunque la concesión de treinta segundos siga vigente.

Cerrar la sesión libera la reserva. El cliente espera cinco segundos antes
de reconectar TCP; en ese intervalo otra radio puede ganar el enlace hacia el
mismo nodo. Si la nueva admisión de la radio original es rechazada, entonces
se desconecta también su Wi-Fi y vuelve a buscar. Esta secuencia es compatible
con el cambio de Norte a Sur informado, sin demostrar que haya ocurrido allí.

La corrección conserva la sesión mientras `nm_owns()` confirme que sigue
siendo su propietaria. Reintenta una renovación fallida después de un segundo,
contado desde el final del RPC. El vencimiento, la pérdida de propiedad y el
cierre solicitado siguen deteniendo el transporte. No se prolonga ninguna
concesión usando un reloj local ni se permite ignorar indefinidamente al
central.

### Perder un lateral o recibir una solicitud ajena podía invalidar la distancia propia

Al perder un vecino por un enlace lateral, el código ponía la distancia
propia a cero aunque el principal por otra radio siguiera presente. Una
solicitud de gateway recibida de otro nodo también producía ese borrado.
Una respuesta tardía por una salida peor podía pasar entonces por mejor
candidata. El caso mínimo reproducido conserva inicialmente Norte y distancia
dos; tras una solicitud ajena acepta una respuesta de distancia tres por Este
y termina con distancia cuatro. Es un defecto de routing, distinto de la
asociación física Wi-Fi.

La corrección separa atender solicitudes de otros nodos de buscar una salida
propia. Una solicitud de tránsito debe actualizar las rutas de regreso y
obtener la confirmación para su solicitante sin borrar una salida propia
válida. La pérdida de un lateral también debe conservar la distancia si queda
un principal válido, manteniendo el anuncio de la ruta de regreso. Conservar
la distancia sin reenviar la confirmación dejaría al hijo esperando; ambas
partes deben corregirse juntas.

También se reproduce que una solicitud de gateway anterior al primer
handshake cambia el estado de un nodo aún sin bloque y bloquea su provisión
posterior. Esas solicitudes deben ignorarse hasta tener una red propia válida.

### Dos solicitudes simultáneas podían confirmar un retorno inexistente

Se reprodujo una topología de cuatro nodos: raíz, intermediario y dos vecinos
con bloques distintos que no están contenidos en el bloque del intermediario.
Mientras el intermediario esperaba una respuesta para el camino A, descartaba
temporalmente la solicitud del camino B. La confirmación podía responder a
ambos, aunque la raíz todavía no tuviera la ruta de regreso hacia B.

Ahora cada radio conserva el camino solicitado y la confirmación SPI lleva
el camino efectivamente reenviado. Solo contesta y consume una solicitud si
esta coincide con un prefijo exacto del camino confirmado. B permanece
pendiente; al reintentar, anuncia su camino hasta la raíz y recién entonces
recibe respuesta. Una mejora de gateway no solicitada no confirma caminos
ajenos. La raíz puede responder por el camino que ya instaló localmente.

El mensaje Wi-Fi existente no contiene un identificador de solicitud. Una
respuesta antigua que llegue después de comenzar otra petición no puede
correlacionarse completamente con ese formato. El alcance SPI resuelve el
caso simultáneo reproducido; no añade garantía de frescura al protocolo
Wi-Fi. Esta limitación heredada requeriría otro cambio de protocolo si se
decide abordarla.

### Una desconexión ajena podía cerrar el servidor AP existente

El evento de desconexión AP cerraba el servidor sin comprobar qué estación
había salido. Además, rechazar una nueva asociación en un AP bloqueado usaba
la desconexión de todas las estaciones.

Ahora el servidor identifica a su estación por MAC y AID; una desconexión
ajena no termina la sesión admitida. El rechazo se dirige solo al nuevo AID y
un error al rechazarlo no aborta el firmware. Los AP laterales actuales
admiten una sola estación, por lo que este defecto reproducido tampoco prueba
por sí solo la causa del incidente con cuatro nodos.

## Validación y despliegue

Las pruebas ejecutan los callbacks y sesiones C reales. Se simulan reloj,
entrega SPI, sockets y eventos del driver; no son una prueba de radiofrecuencia.

| Comprobación | Resultado |
|---|---:|
| Routing: principal sano, reinicio de lateral, provisión, recuperación y cuatro nodos simultáneos | 51/51 |
| Renovación de sesiones y reserva Norte/Sur hacia la misma raíz | 9/9 |
| Aislamiento de asociaciones AP | 7/7 |
| Vecinos, concesiones y bloques existentes | 38/38 |
| Ciclo de descubrimiento y DHCP existentes | 7/7 |
| Hooks de rutas IP existentes | 5/5 |
| OTA y comandos de control existentes | 24/24 |
| Herramientas Python, incluida compilación con SSID propio | 80/80 |

Con el código original fallan cinco de las nueve pruebas de renovación,
incluida la carrera Norte/Sur; la corrección supera las nueve. La prueba AP
contra el código original falla al desconectar una estación ajena. Las
regresiones de routing también se contrastaron con las fuentes originales:
fallan catorce de los quince casos nuevos y sigue pasando el control positivo
de una ruta realmente mejor. La versión corregida supera los 51 casos.
Resultados: [validación](evidencias/20261030/validacion.json),
[renovación original](evidencias/20261030/renovacion-original.json),
[renovación corregida](evidencias/20261030/renovacion-corregida.json) y
[routing original](evidencias/20261030/routing-original.json).

Comandos de las pruebas centrales, desde la raíz del proyecto:

```powershell
python tests/run_native_routing.py --cc app/build/tools/tcc/tcc.exe
python tests/run_native_peer_renew.py --cc app/build/tools/tcc/tcc.exe
python tests/run_native_access_point.py --cc app/build/tools/tcc/tcc.exe
```

La compilación ESP32 con ESP-IDF 5.4.1 terminó correctamente. Se generó el
paquete diagnóstico local `app/build/cuarto-nodo-20261030/grillo`, con
`main.bin` de 908 016 bytes, dentro de la partición OTA de 2 031 616 bytes.
Su identidad ELF es
`5547aef3d8bc9265723f2d146309e765c7907a9f8198086815130a15f7feb350`.
Se verificaron el hash binario, la identidad ELF y que el archivo de fuentes
del paquete coincide byte a byte con las fuentes de producción del checkout.
El [manifiesto de compilación](evidencias/20261030/compilacion.json) conserva
los hashes. Se validó la variante Grillo; no se generaron paquetes específicos
para los demás nodos en esta sesión.

El mensaje de routing SPI pasa de 132 a 144 bytes y agrega una confirmación
interna. **Hay que actualizar juntas las cinco placas de cada nodo** mediante
el procedimiento coordinado: los mensajes antiguos de routing SPI son
incompatibles dentro de un mismo nodo. Se mantienen los mensajes Wi-Fi entre
nodos de 132 bytes y HELLO versión 2. No se ha cargado firmware ni realizado
una prueba física en esta sesión. La corrección se prepara para compartir en
la rama `fix/stable-links-neighbor-restart`, junto con la
[guía de actualización y validación](ota-validacion-cuarto-nodo-20261030.md).

## Comprobación física pendiente

Cuando el responsable de los equipos pueda reproducirlo, conviene capturar
antes de reiniciar o incorporar el cuarto nodo. Con cuatro USB, priorizar:

- El central del nodo cuyo enlace cambia.
- Su radio que tiene el enlace principal sano antes del cambio.
- El central de la raíz.
- El central del nodo que se incorpora o reinicia.

Registrar qué nodo y radio corresponde a cada COM, los estados y hashes de
las cinco placas de cada nodo, y una marca temporal al reiniciar el vecino.
Mantener un cliente conectado al nodo afectado y pings por ese adaptador hacia
su gateway, `10.255.255.254` y `1.1.1.1`. Conservar la carpeta completa de
`auxiliar/capture_links.py`, no solo las últimas líneas.

La captura debe permitir distinguir asociación Wi-Fi, sesión admitida y
gateway elegido. Para considerar resuelto el incidente hay que verificar que
la incorporación o el reinicio de un vecino no retire una sesión principal
sana, no cambie su salida por una ruta peor y no corte Internet. Si falla el
principal realmente, la recuperación por el alternativo debe seguir
funcionando; las direcciones ya asignadas deben conservarse durante esa
conmutación.
