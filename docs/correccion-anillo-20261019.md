# Corrección del atasco del anillo y siguiente ensayo

**Esta es la primera corrección y no resolvió el ensayo de hardware.** La
continuación está en [correccion-spi-dma-20261019.md](correccion-spi-dma-20261019.md),
con una segunda corrección reproducida y contadores para el transporte.

**Resultado de hardware: la corrección no resolvió la validación en esta
corrida.** En `root-corregido-20261019-084626-994` siguen los timeouts. Después
de cerrar el capturador, la consulta local pudo confirmar que COM8 ejecuta
`7e0a5b81e06180d19a906533b7a367391f16c09377c5612d0e5a063125079d8a`, con
`running=0x20000 ready=1 boot_paused=1 pending_verify=0 recovery_pending=0`.
Esto identifica la imagen del central, no demuestra la identidad de las otras
cuatro placas de Root. No presentarla como solución validada del fallo físico.

La consulta original de tres intentos separados por 30 segundos no obtuvo ACK.
El coordinador de arranque ocupa `exchange_lock` durante sus verificaciones y
`node_ota_exchange` rechaza solicitudes concurrentes sin emitir respuesta;
por eso un timeout local no acredita que no haya comunicación UART.
La lectura posterior tuvo éxito con:

```powershell
python gateway/flash_node.py --port COM8 --local-status --timeout 1 --retries 45
```

Esta consulta es local de estado: no carga ni selecciona firmware. Los intentos
breves permiten aprovechar los intervalos entre verificaciones. Todavía falta
verificar las identidades de las periféricas y medir qué causa las pérdidas del
transporte; no repetir el flasheo sin obtener evidencia que lo justifique.

19/10/2026. La captura de Root aislado mostró recuperación tardía, sin reinicio:
validación de las cinco placas a las 06:48:24 y reanudación a las 06:48:29,
aproximadamente nueve minutos y medio después del arranque. Eso descarta
atribuir el síntoma únicamente a la incorporación de Grillo o Ramona.

## Defecto reproducido y cambio

Antes, `ring_link_process_task` encolaba todas las tramas internas para
`ring_link_internal_process_task`. Esa segunda tarea reenviaba por SPI,
confirmaba retornos y entregaba a `node_on_sibling_message`. Esta entrega puede
esperar indefinidamente si la cola de aplicación está llena. Sus consumidores
pueden estar esperando a su vez la confirmación de una difusión. Un atasco de
aplicación frenaba así también las siguientes tramas y confirmaciones físicas.

La corrección separa esos pasos:

1. Verificar CRC en recepción.
2. Reenviar las tramas internas en tránsito o confirmar un retorno directamente
   en la tarea de recepción, antes de la admisión a la cola local.
3. Encolar solamente las tramas que requieren entrega a la aplicación.
4. La tarea interna entrega el mensaje local una sola vez, sin reenviarlo otra
   vez; el propietario libera el buffer exactamente una vez.

Se conserva el formato de tramas, el control TTL existente, las comprobaciones
de ID del ACK y el tratamiento de las colas llenas. No se agregó una espera
infinita ni se aumentaron tamaños de cola. Tampoco se retiró la barrera de
arranque ni su comprobación de hashes.

La cola local sigue siendo finita y puede perder una entrega si se llena,
como antes. Ahora eso no impide que esa trama circule ni que se reconozca su
retorno. La tarea receptora todavía puede esperar hasta 100 ms al intentar
encolar localmente y debe compartir el mutex de transmisión SPI; la corrección
no constituye una garantía de latencia o entrega sin pérdidas.

La prueba nativa sobre el código C real falló antes del cambio y pasó después
con la cola local bloqueada/llena. Se comprueban cinco casos: tránsito aun con
cola llena, retorno de ACK sin pasar por la cola, entrega única sin reenvío
duplicado, destino/CRC y liberación de buffers, y entrega local pese a un fallo
de reenvío. No reproduce todas las condiciones físicas de los ESP y no prueba
que este sea el único defecto presente en la captura.

Validación:

```powershell
python tests/run_native_ingress.py --cc app/build/tools/tcc/tcc.exe
python tests/run_native_broadcast.py --cc app/build/tools/tcc/tcc.exe
python tests/run_native_coordination.py --cc app/build/tools/tcc/tcc.exe
python tests/run_native_neighbors.py --cc app/build/tools/tcc/tcc.exe
python tests/run_native_routing.py --cc app/build/tools/tcc/tcc.exe
python tests/run_native_ota.py --cc app/build/tools/tcc/tcc.exe
```

Los 75 casos nativos pasaron, además de la comprobación C/Python del protocolo
OTA. Las imágenes diagnósticas se compilan con ESP-IDF 5.4.1; incluyen ELF,
mapa, manifiesto y ZIP de fuentes para conservar la identidad instalada.
Ambas aplicaciones compiladas miden 807264 bytes. Se verificaron los hashes de
los cuatro segmentos de cada paquete, la identidad ELF y la coincidencia entre
las fuentes modificadas y su ZIP. No se instaló firmware automáticamente.

## Ensayo siguiente: cambiar primero solo Root

Cerrar la captura y todos los monitores antes de flashear. Conservar las carpetas
anteriores. Dejar Grillo y Ramona apagados. Instalar **el mismo paquete nuevo en
las cinco placas de Root**; no evaluar el nodo con hashes mezclados.

Para cada placa de Root, conectada individualmente por USB:

```powershell
$Puerto = Read-Host "COM de la placa individual de Root"
python auxiliar/esp_rpi_flasher.py flash --port $Puerto --flash-args app/build/link-capture/ramona-ingress-20261019/flash_args --baud 460800
```

Repetir para las cinco placas. El nombre `ramona` identifica la variante de
compilación; Root conserva su rol mediante su configuración, como en el paquete
diagnóstico anterior. El ensayo usa la variante correspondiente al firmware ya
instalado en Root. No borrar NVS ni cambiar los selectores de orientación/Root.

Hash ELF del nuevo paquete Root:
`7e0a5b81e06180d19a906533b7a367391f16c09377c5612d0e5a063125079d8a`.

Después apagar Root por completo, incluidos sus USB de alimentación, y preparar
una captura nueva desde la raíz del proyecto:

```powershell
$Captura = Join-Path (Get-Location) ("app\build\capturas\root-corregido-" + (Get-Date -Format "yyyyMMdd-HHmmss-fff"))
$Captura | Set-Content app/build/captura-activa.txt
python auxiliar/capture_links.py start --port root_center=COM8 --port root_south=COM5 --port root_west=COM9 --port grillo_center=COM7 --output "$Captura" --notes "Correccion de reenvio y ACK antes de cola local; Root solo al inicio"
```

Confirmar que los COM siguen asignados a las mismas placas. Los puertos ausentes
se reintentan; COM7 seguirá ausente mientras Grillo esté apagado.

En un segundo PowerShell:

```powershell
$Captura = (Get-Content app/build/captura-activa.txt -Raw).Trim()
python auxiliar/capture_links.py mark --output "$Captura" "ROOT_ON: cinco placas con paquete corregido"
```

Encender las cinco placas de Root y conectar sus monitores. Observar durante
unos tres minutos y revisar el archivo sin abrir nuevamente el COM:

```powershell
Get-Content "$Captura\root_center.log" -Tail 100
```

- Si aparece `ready_mask=0x1f error=0x0` y después `boot_traffic_resumed`, guardar
  la marca `ROOT_VALIDADO` y conservar al menos 60 segundos de referencia.
  Luego marcar `GRILLO_ON`, encender Grillo con su firmware actual y conservar
  tres minutos más. El formato del protocolo no cambió; la barrera compara
  hashes dentro de cada nodo, no entre Root y Grillo. No encender Ramona todavía.
- Si continúan los timeouts, no prolongar la espera para reproducir los nueve
  minutos y medio: guardar `ROOT_SIGUE_FALLANDO`, terminar con Ctrl+C y conservar
  la evidencia. Eso indicaría que hay otra causa además del defecto corregido.
- Una validación de Root no prueba Internet. Para comprobarlo, conectar Wi-Fi 2
  al hotspot de Grillo y usar los pings con origen explícito de la guía principal,
  guardando su salida. Esta corrida inicial es solo UART y no inicia pings.

Las marcas se agregan con `python auxiliar/capture_links.py mark --output
"$Captura" "TEXTO"`. Terminar siempre con Ctrl+C y conservar la carpeta completa.

La variante corregida para una actualización posterior de Grillo se prepara en
`app/build/link-capture/grillo-ingress-20261019/`; primero se compara Root para
cambiar una sola condición del ensayo.
Su identidad ELF es
`bb3d3a834ea594e7da6b0b7d376a74ce3d20ec273ecdb6f612290a70b07f9419`.
