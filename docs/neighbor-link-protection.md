# Un único enlace por par de nodos

Los ESP periféricos libres mantienen el ciclo AP/Station. Cada nodo registra a
sus vecinos por el UUID compartido por sus cinco ESP. Si A ya tiene un enlace
activo con B, las otras interfaces de A y B descartan ese vecino al escanear y
rechazan un segundo enlace durante la admisión TCP.

## Registro y admisión

`neighbor_manager` usa el ESP central como único escritor del registro y
distribuye las reservas a los cuatro ESP periféricos mediante
`RS_NEIGHBOR_MANAGER` en el anillo SPI. El registro conserva un vecino por
interfaz y un solo enlace por UUID remoto; las comprobaciones de admisión se
resuelven contra el registro central, aunque una interfaz aún tenga una copia
antigua al escanear.

Antes de entregar mensajes al ruteo, ambos extremos ejecutan:

1. Intercambio de identidad: UUID del nodo, rol AP/Station, orientación y
   token de sesión creado por la Station. La Station verifica que la identidad
   del AP coincida con el SSID seleccionado.
2. Reserva del vecino en sus respectivos ESP centrales y votación entre extremos.
3. Compromiso de las reservas y una segunda votación. Desde este punto ninguna
   otra reserva puede desplazar el enlace.
4. Activación y confirmación entre extremos. Solo entonces se entregan mensajes
   y eventos de conexión al protocolo de ruteo.

Las reservas pendientes se comparan por UUID de la Station, orientación de la
Station, orientación del AP y token de sesión. Ambos nodos aplican el mismo
orden. Un enlace comprometido o activo conserva prioridad, aunque otro intento
tenga una clave menor. Si dos intentos comprometen reservas distintas en los dos
nodos, ambos fallan la votación, liberan las reservas y vuelven a descubrir
vecinos; nunca se activan los dos enlaces.

Una asociación Wi-Fi provisional puede ocurrir antes de conocer el UUID remoto.
Al rechazar la admisión se libera esa asociación sin incorporar rutas. Los
enlaces hacia terceros nodos siguen permitidos.

## Desconexiones y tiempos

La liberación identifica el token y las orientaciones del enlace. Una operación
vieja no puede borrar la reserva de una sesión posterior. Las copias del registro
también llevan una revisión para descartar actualizaciones SPI fuera de orden.

| Estado u operación | Tiempo |
| --- | --- |
| Reserva o compromiso pendiente | 20 segundos |
| Enlace activo sin renovación | 30 segundos |
| Renovación de un enlace activo | Cada 5 segundos mientras se recibe o espera TCP |
| Espera de respuesta del coordinador SPI | 3 segundos |
| Identidad, votación o mensaje TCP incompleto | 5 segundos |
| Bloqueo temporal de un AP lleno | 30 segundos, por SSID |

La pérdida de SPI impide aceptar o renovar enlaces. Una renovación fallida
cierra la sesión; el coordinador retira las reservas vencidas aunque una
liberación no llegue. Los tiempos se distribuyen como duraciones para no depender
de que los ESP tengan el mismo tiempo de encendido. Ambos sockets tienen TCP
keepalive y el cliente limita el tiempo de conexión.

La información de vecinos sobrevive a los cambios AP/Station. El reparto de
canales sigue en `channel_manager`, separado de las reservas, y se anuncia tras
admitir el enlace. Los eventos de conexión, mensajes y desconexión se procesan
en una cola FIFO común. El primer handshake de ruteo del AP se procesa en la
Station antes de su respuesta, conservando el orden de aprovisionamiento.

## Convivencia con OTA

El centro procesa las solicitudes del gestor de vecinos en una tarea propia,
con una cola de 16 mensajes. El callback compartido del anillo copia y encola
la solicitud sin esperar un broadcast. La tarea aplica las operaciones en orden,
genera la copia del registro y la transmite fuera del callback. El ruteo también
encola sus pedidos de expiración; la tarea verifica vencimientos al quedar
inactiva, aunque no llegue un tick.

Esto evita que una respuesta de vecinos esperando el bloqueo de broadcast
detenga la tarea que entrega los ACK OTA. Las reservas y renovaciones siguen
activas durante la carga. Si la cola se llena, la solicitud se descarta sin
bloquear el anillo; el emisor conserva su mecanismo de timeout y no admite ni
renueva un enlace sin confirmación. Las revisiones y las duraciones se generan
cuando la tarea procesa la solicitud, para evitar enviar una copia envejecida
por una espera en cola.

La UART del centro imprime `I4ANM` cuando un broadcast de vecinos falla, demora
al menos 250 ms o se detectan descartes de la cola. En `snapshot_failed` y
`snapshot_slow`, `value` es el tiempo del broadcast en milisegundos; en
`queue_full`, es el contador acumulado de solicitudes descartadas. Estas líneas
se conservan con `gateway/flash_node.py --verbose` y usan el mismo bloqueo de
salida que los ACK OTA.

Las pruebas nativas verifican que el callback no transmita ni espere, incluso
con la cola llena, y que la tarea conserve la admisión, renovación, liberación
y expiración. La carga OTA con vecinos conectados requiere una nueva prueba
física; las pruebas simuladas no garantizan la continuidad SPI durante flash.

## Compatibilidad y actualización

Al cerrar una interfaz se detienen sus tareas TCP antes de destruir la interfaz
de red. Los handlers ignoran asociaciones/IP tardíos durante ese cierre para
evitar que vuelvan a iniciar un cliente o servidor sobre una interfaz retirada.

**Actualizar los cinco ESP de cada nodo y todos los nodos que deban enlazarse.**
La identidad TCP comienza con `I4AN`, versión 1, y los mensajes posteriores tienen
un prefijo de longitud de dos bytes en orden de red, con un máximo de 512 bytes.
Esto permite reconstruir mensajes fragmentados o agrupados por TCP. Los campos
del protocolo de ruteo y el protocolo OTA conservan su formato anterior.

Un vecino con el transporte anterior falla la admisión. Un ESP central con
firmware anterior no responde a las reservas y sus periféricos tampoco aceptan
enlaces. Usar una ventana de actualización coordinada y reiniciar los nodos
completos para evitar una combinación de versiones.

## Verificación

```bash
python -m unittest discover -s tests -v
python tests/run_native_ota.py --cc cc
python tests/run_native_neighbors.py --cc cc
idf.py -C app build
```

En Windows, pasar la ruta de TinyCC a `--cc`. Las 21 pruebas ejecutan el
código C real del registro, coordinador SPI, transporte, filtro de escaneo y cola
de eventos con proveedores simulados. Cubren enlaces recíprocos, terceros
vecinos, intercalaciones de intentos simultáneos, vencimiento y renovación,
mensajes SPI viejos o perdidos, identidades incompatibles, fragmentación TCP y
orden de los eventos.

La compilación y las simulaciones no sustituyen las pruebas en placas. Verificar
A→B seguido de B→A en otras orientaciones, dos intentos simultáneos, pérdida y
recuperación de SPI/Wi-Fi, reconexión tras reiniciar un nodo y enlaces válidos
A→C/B→C. Comprobar que el cambio AP/Station libera sockets y reservas y que una
asociación rechazada no incorpora rutas.


Durante OTA, las reservas usan el reloj local congelado de ring_share: mantienen
el tiempo restante hasta el aborto, vencimiento de la sesion o reinicio. Las
renovaciones de conexiones activas se resuelven localmente y las nuevas admisiones
se rechazan. La pausa tambien bloquea trafico IP en el transporte inferior. Los
pedidos duplicados del mismo propietario se coalescen antes de ocupar la cola;
las retransmisiones identicas no generan nuevas revisiones ni mas snapshots.
Ver `docs/actualizacion_firmware.md` para instalacion y diagnosticos de la pausa.


La pausa de arranque bloquea vecinos/IP desde antes de inicializar SPI. Se mantiene
hasta que el centro comprueba las cinco aplicaciones listas, con la misma imagen,
confirmadas y fuera de recuperacion. RESET y aprovisionamiento siguen habilitados.
La suite incluye la barrera y una simulacion de ocho recepciones DMA acumuladas
mientras la tarea de polling no corre; tambien comprueba el descarte de tramas
truncadas y que no se reutilicen buffers aun en poder del driver.
