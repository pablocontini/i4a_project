# Anillo SPI: reproducción y corrección candidata

Fecha: 8 de octubre de 2026. Nodo no root: central COM5 y norte COM6.

Después de un arranque en frío, a los 64 segundos las cinco placas respondieron
INFO con el mismo hash ELF, `ready=1`, `boot_paused=0` y ninguna OTA pendiente.
En una consulta posterior, después de habilitarse el descubrimiento, dejaron
de responder los periféricos. El central continuó respondiendo a una consulta
exclusivamente local. El reinicio recupera temporalmente la comunicación, pero
no acredita estabilidad.

Los registros están en `build/firmware/cold-start-center.log`,
`cold-start-north.log` y `cold-start-after-discovery.log`. La consola norte
capturada tras un reinicio individual quedó dentro de `node_setup`, después
de inicializar SPI; ese reinicio no equivale a arrancar todo el anillo en frío.

## Bloqueo encontrado en el código

`broadcast_handler()` entregaba primero una trama a la cola de callbacks y
después la reenviaba. La entrega puede esperar si esa cola está llena. Si un
callback de esa cola espera a que su propio broadcast complete el recorrido,
se forma un ciclo de espera: el callback necesita el reenvío y el reenvío
espera espacio en la cola del callback.

La corrección reenvía primero la trama y después la entrega localmente. Las
confirmaciones del protocolo OTA siguen generándose después del procesamiento
de cada operación. No se cambian campos de protocolo, GPIO, reloj SPI,
potencias, umbrales ni particiones.

La prueba nativa utiliza el handler de producción y modela una cola que solo
puede drenarse al completar el broadcast del emisor. Falla con el handler
anterior y pasa con el corregido; también conserva la entrega local y el
tratamiento del retorno al origen. Esto demuestra el defecto de orden, pero
su relación causal con la caída observada requiere el ensayo en placas.

```powershell
python tests/run_native_broadcast.py --cc RUTA_TCC --run
```

## Imagen para probar

Versión: `redundant-link-20261008-fix1`. Compilada con ESP-IDF 5.4.1.
Aplicación: `build/firmware/main.bin`. Bundle completo:
`firmware_bundle_redundant_20261008_fix1`. El bundle original permanece intacto.

Actualizar las cinco placas del nodo de prueba con la misma imagen. Si el
anillo está detenido, realizar un arranque en frío y comenzar la actualización
después de que INFO confirme que las cinco placas están listas, antes de
reproducir la caída. No omitir roles para esquivar un timeout. Si no se consigue
comunicación estable para OTA, usar instalación USB individual.

Después de actualizar, verificar el nuevo hash y consultar INFO antes y después
de la habilitación del descubrimiento, además de observar la conexión al root.
La corrección no se considera validada en hardware hasta completar esa prueba.

## Resultado de fix1 y segunda corrección candidata

Se verificó el central con hash ELF
`415034f94a8c85e2d8b80f73296994b70812ee5dcc91d3eb713ae0cf4d7f6feb`.
El arranque estaba liberado y no había OTA pendiente, pero la consulta a los
periféricos volvió a fallar. Fix1 no resolvió el fallo observado.

Se encontró además carga innecesaria introducida por el timer de recuperación:
`rt_on_tick()` solicitaba un recorrido del token cada segundo por placa,
incluso con el nodo sin red y sin un pedido de gateway activo. Fix2 agrega un
predicado de trabajo pendiente: los timers solo solicitan el token cuando
están armados; conserva el reintento de una solicitud de token ya pendiente.
Este cambio no altera los mensajes de ruteo ni elimina el timer de failover.

La imagen `redundant-link-20261008-fix2` compila con ESP-IDF 5.4.1 y está en
`firmware_bundle_redundant_20261008_fix2/main.bin`. Se añadió un caso de prueba
de 120 ticks inactivos y activación de los timers de ambos roles. La suite
actual compila; su ejecución sigue pendiente por el bloqueo de DLL de Windows.
El cambio debe comprobarse en placas, primero con INFO después de varios
minutos y luego con la conexión al root. No se considera una solución
confirmada del fallo hasta completar esos ensayos.

### Primera observación en placas de fix2

Las cinco placas del nodo no root en COM5 respondieron con hash ELF
`4e0420345d08215e62a0f0ac98a83cbdfade0afc907f5e753c42254d3c153a41`,
`ready=1`, `boot_paused=0` y ninguna OTA pendiente. Se obtuvieron respuestas
completas en la consulta inicial a los 136 segundos y en cuatro muestras
posteriores, hasta los 258 segundos de arranque. Los ACK periféricos de la
última muestra demoraron aproximadamente 15 ms cada uno.

Registros: `build/firmware/fix2-status-initial.log` y
`build/firmware/fix2-stability-samples.{json,log}`. Esta observación supera el
momento de caída de las versiones anteriores, pero no es una prueba de larga
duración. El usuario confirmó que el root todavía tiene la versión anterior;
queda pendiente actualizarlo y comprobar conexión, tráfico y failover.

### Actualización del root a fix2

Se actualizó el root por COM5 con `gateway/flash_node.py`, incluyendo sus
cinco roles. El cargador terminó con código 0 después de la validación de
la imagen, selección de partición, reinicio y recuperación automática.
Una consulta posterior confirmó en las cinco placas el hash fix2 anterior,
partición activa `0x20000`, `ready=1`, `boot_paused=0`,
`pending_verify=0` y `recovery_pending=0`.

Registros: `build/firmware/root-fix2-update.log` y
`build/firmware/root-fix2-status-final.log`. El escaneo de Windows mostró
`I4A_N_000000000000` e `Internet4All_Root`. La presencia de SSID no confirma
por sí sola el enlace lógico con el nodo no root ni la entrega de tráfico;
esas comprobaciones siguen pendientes.

### Observación posterior: fix2 no acredita estabilidad sostenida

Después de actualizar el root, se volvió a consultar el nodo no root,
ahora en COM6. A los aproximadamente 23 minutos de arranque, sus periféricos
no respondieron; el central seguía ejecutando fix2, listo y sin bloqueo de
arranque ni OTA pendiente. El root, consultado por COM5, respondió con sus
cinco placas fix2 listas. No se conoce el instante exacto en que dejó de
responder el anillo no root: no se lo consultó durante toda la carga del root.

Registros: `build/firmware/nonroot-fix2-current.log`,
`nonroot-fix2-center-late.log` y `root-fix2-current.log`. Las muestras anteriores
hasta cuatro minutos no son evidencia de estabilidad a largo plazo. Se debe
separar un ensayo de arranque inicial con root fix2 estable de la recuperación
tras el reinicio del root antes de atribuir la causa a una etapa concreta.

### Arranque inicial con root fix2 ya estable

Se reinició solo el nodo no root, manteniendo el root fix2 encendido. A los
28 segundos respondieron las cinco placas: el central estaba listo y los
periféricos todavía no, con la barrera activa. Después, las cinco placas
pasaron a `ready=1`, `boot_paused=0` y respondieron en cinco muestras, hasta
los 168 segundos. Una consulta posterior, a los 574 segundos (9 min 34 s),
volvió a obtener los cinco ACK con el hash fix2 y sin OTA pendiente.

Registros: `build/firmware/nonroot-restart-root-fixed.log`,
`nonroot-root-fixed-samples.{json,log}` y `nonroot-root-fixed-later.log`.
Un escaneo nuevo mostró `ComNetAR - Grillo` y AP N/E con UUID
`142B2FC05E7D`, además de redes del root. Eso es evidencia de provisión del
nodo no root, no una comprobación de tráfico ni de failover. El PC seguía
conectado a su red de administración, por lo que no se ejecutó tráfico desde
un cliente del hotspot del nodo.

### Hotspot Grillo ausente

Al conectar su central a COM5, no respondió ni al INFO exclusivamente local
ni al de las cinco placas. Una captura pasiva de 20 segundos no recibió bytes.
Se reinició la placa mediante RTS, sin flashearla: arrancó en rol 4, alcanzó
`app_main.ready`, obtuvo `ready_mask=0x1f` y liberó la barrera. Tres consultas
posteriores recuperaron los cinco ACK fix2, hasta los 161 segundos.

Un escaneo nuevo mostró `ComNetAR - Grillo`, `ComNetAR - Ramona` y periféricos
con UUID `142B2FC05E7D` y `20E7C85AA245`, respectivamente. Los registros de
Grillo están en `build/firmware/grillo-missing-hotspot-{status,passive,boot}.log`
y `grillo-after-center-reset.log`. El reinicio recuperó el funcionamiento;
no identifica ni corrige la causa por la que el central dejó de responder.
