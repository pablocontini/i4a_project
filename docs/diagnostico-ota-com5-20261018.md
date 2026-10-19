# Fallo OTA en COM5: diagnóstico y recuperación

Fecha: 18 de octubre de 2026. Evidencia: salida del cargador y consultas de estado
proporcionadas por el usuario durante esta sesión. No se operó el hardware.

## Estado comprobado

- Carga de 804816 bytes detenida en DATA seq=686, offset=305984, tras confirmar
  North. No hubo ACK de South, East, West ni Center. ABORT seq=687 tampoco pudo
  confirmarse. **SELECT no se envió:** este intento no seleccionó la imagen nueva.
- Tras esperar, la consulta completa muestra `request_broadcast_failed`,
  `broadcast_ok=0` y timeout del primer periférico. RX y TX del central siguen
  avanzando; sus contadores CRC y colas de despacho permanecen en cero errores.
- El central mantiene telemetría. Heap libre 69828, mínimo 57904; el mínimo de
  pila del callback compartido es 396 bytes. Estos datos no prueban desbordamiento
  ni excluyen un bloqueo de callbacks o un fallo en otro salto.
- La nueva consulta `--local-status` respondió por UART con `phase=0`,
  `session=0`, `pending_verify=0`, `recovery_pending=0`, `ready=1`,
  `boot_paused=0`, partición activa `0x210000`.
- Hash ELF activo:
  `1eb86333cf9f23f7571af8a6a425d6106223195b1c4036109e4676c6e2427d74`.
  Coincide con la imagen **SOAK Grillo 17/10 con pila TCP/IP 4096** del worktree
  `three-node-soak-20261015`. El nombre de la variante no determina el rol físico
  Root/Home ni identifica por sí solo el nombre del nodo conectado.

## Interpretación

El coordinador instalado pregunta por North, South, East, West y finalmente
procesa Center. Al primer ACK ausente abandona el intercambio. Por eso la lista
de cuatro roles faltantes del DATA no demuestra cuatro fallas independientes:
South es el primer pendiente; East, West y Center pueden no haberse intentado.
Ese mismo comportamiento en INFO y ABORT oculta diagnósticos posteriores e
impide limpiar algunas sesiones, incluido el receptor local del central.

La sesión local ya está cerrada, pero el broadcast completo sigue sin
confirmarse. El log no permite localizar la primera causa física. CRC y colas
sin errores en el central no verifican los demás ESP ni la cola compartida de
callbacks. La espera de unos 5,6 segundos incluye los reintentos y la contención
del broadcast; no demuestra por sí sola un problema de cable o alimentación.

El firmware SOAK contiene un mecanismo de ACK físico que no correlaciona el ID
de retorno y usa un handle de tarea compartido sin protección. Es un defecto
comprobable en sus fuentes, pero este log no demuestra que haya iniciado este
fallo concreto. SOAK ya incluye reenvío antes del callback y recepción DMA
encolada: no corresponde atribuir el fallo a su ausencia.

## Recuperación operativa

Conservar el registro y reiniciar eléctricamente las cinco placas del nodo
juntas. Una vez terminado el arranque, consultar todas:

```powershell
python gateway/flash_node.py --port COM5 --roles center,north,south,east,west --status --verbose
```

Si falla, distinguir la respuesta local mediante:

```powershell
python gateway/flash_node.py --port COM5 --local-status --verbose
```

El segundo comando consulta solamente el central y no acredita recuperación del
anillo. No reintentar una carga mientras falten respuestas del inventario real.
La expiración de 120 segundos necesita que las tareas continúen ejecutándose;
no es un reinicio automático que garantice reparar el transporte.

## Compatibilidad de la imagen

La imagen fallida pertenece al worktree `comnetar-stability-tools`, base
`a0ccdf3-dirty`; su hash ELF es
`eca40101a74f86ee7e90b5ad18a8e5ef3fe7539ff23be607a8a63a15380bbe1c`.
Su SHA-256 de archivo es
`7b58dac9a883a74b5ec1971b667cf2916581b3567f04b7a9006aa7b5e311981d`.
Se conservó una copia con su ELF y sdkconfig en
`app/build/ota-timeout-20261018/before/`.

**Esta imagen no preserva la implementación SOAK instalada.** No contiene
`discovery_manager`, `node_control` ni otros componentes de esa variante, y los
formatos SYNC/SHARED_STATE llamados v2 difieren en campos y orden. Compartir el
layout OTA no hace intercambiables las implementaciones. Las correcciones deben
portarse y validarse sobre las fuentes SOAK correspondientes antes de generar
una imagen que conserve esas funciones.

## Correcciones locales posteriores al fallo

- El cargador agrega `--local-status`, que solo emite INFO dirigido al central;
  no requiere instalar firmware nuevo. La consulta se verificó en hardware por
  la respuesta que compartió el usuario.
- El coordinador C de este worktree continúa INFO y ABORT después de un
  periférico sin ACK y procesa el central, conservando el resultado de error
  si falta alguno. Las operaciones de escritura mantienen su corte al primer
  timeout. Esta modificación aún no está instalada y no se declara solución de
  la causa física del primer timeout.

El presupuesto de espera de ACK se reparte entre periféricos. No limita por sí
solo el tiempo de una llamada `rs_broadcast` bloqueada en transporte o mutex.
