# Segundo defecto reproducido: recepción SPI dependiente de la aplicación

**Evaluación más reciente:** Root y Grillo ya ejecutan la corrección DMA.
Las pérdidas intermitentes continúan; los cambios nuevos, su validación y
los paquetes aún no instalados están en
[evaluacion-pruebas-20261019.md](evaluacion-pruebas-20261019.md).
Las indicaciones de actualización que siguen son antecedentes ya completados.

**Paso histórico:** las pérdidas continuaron durante tres minutos después de apagar
Ramona. Se confirmó que las cinco placas de Grillo aún tenían el diagnóstico
original, sin la corrección DMA. El paquete y la comparación siguiente están en
[prueba-grillo-dma-20261019.md](prueba-grillo-dma-20261019.md). Root conserva su
imagen actual; Ramona permanece apagada.

**Resultado histórico, 09:28 del 19/10:** el estado de las cinco placas confirmó
que Norte todavía ejecuta la imagen diagnóstica anterior `48221379…`; Sur,
Este, Oeste y central ejecutan `a965f3c7…`. Esto explica exactamente la máscara
`0x1e`: todas responden, pero Norte no coincide con el hash del central.
El próximo paso es actualizar **solo Norte de Root** con el paquete
`ramona-dma-20261019`, conservando las otras cuatro imágenes.

**Actualización posterior:** después de actualizar Norte, se confirmó el hash
`a965f3c7…` en las cinco placas. La consulta inicial todavía mostró
`boot_paused=1`; una nueva consulta completa, hecha después, confirmó
`boot_paused=0 ready=1` en las cinco. Root completó la barrera en ese arranque.
Esto valida la reanudación actual de Root, no garantiza todavía Internet desde
Grillo ni estabilidad al incorporar Ramona. Mantener Root encendido para la
prueba siguiente y no volver a flashearlo para repetir esta comprobación.

El usuario confirmó que actualizó las cinco placas de Root con la primera
corrección. El central COM8 también confirmó el hash de esa imagen por estado
local. El ensayo `root-corregido-20261019-084626-994` continuó fallando; no se
considera resuelto el fallo físico por el cambio de reenvío anterior.

## Defecto y corrección

El driver SPI y la aplicación compartían ocho buffers. Al terminar una recepción,
la tarea SPI entregaba ese buffer y esperaba otro libre con `portMAX_DELAY`
para rearmar la transacción. Si la aplicación retenía los buffers, la tarea
dejaba de rearmar; las transacciones restantes podían agotarse mientras llegaban
más tramas. Corregir el despacho del anillo no eliminaba esa espera del driver.

La nueva prueba sobre `spi.c` conserva los buffers recibidos en lugar de
devolverlos inmediatamente. Falló con el código anterior porque el rearme
quedaba bloqueado. La prueba previa suponía que el consumidor devolvía cada
buffer enseguida y no ejercitaba este caso.

Ahora hay ocho buffers exclusivos de DMA y ocho para entregar a la aplicación:

1. Copiar una trama completa a un buffer de entrega, si hay alguno libre.
2. Rearmar inmediatamente el mismo buffer DMA, antes de encolar la copia.
3. Si falta buffer de entrega o la cola está llena, descartar esa entrega local
   y contabilizarla sin bloquear el rearme. Una trama truncada no se entrega.

La aplicación nunca modifica un buffer que el driver vuelva a usar para DMA.
Se conserva la corrección anterior de reenvío y ACK antes de la cola local.
Con el frame de 1600 bytes configurado, el pool adicional consume 12800 bytes
de RAM estática por placa. No cambia la frecuencia SPI ni el formato de tramas.

Esto corrige una condición reproducible de software. No demuestra que explique
por sí sola las pérdidas observadas en Root. La recepción todavía depende de
que el driver y la tarea puedan atender las interrupciones y las ráfagas; no
hay una garantía de entrega sin pérdidas si la aplicación está saturada.

## Contadores e identidad

Cada dos segundos, además del diagnóstico anterior, se imprime:

- `event=spi`: `rx`, `short`, `no_buffer`, `queue_full`, `tx`, `tx_fail`.
- `event=transport`: `crc`, `unknown`, `local_full`, `forward_fail`.

Son acumulados desde el arranque. Sus diferencias permiten distinguir tramas
truncadas, agotamiento de buffers, CRC inválido, saturación de colas y fallos de
reenvío. `tx_fail=0` no prueba que el siguiente ESP recibiera la trama: solo
indica que la API local de transmisión no devolvió error.

`event=identity` se repite aproximadamente cada 30 segundos; su repetición no
indica un reinicio. Así se comprueba el hash aunque Windows abra el monitor
después del boot. Los contadores se copian bajo una sección crítica breve;
las consultas de Wi-Fi y la impresión UART ocurren fuera de ella.

## Validación

- 77 pruebas nativas aprobadas: 23 vecinos/SPI, 18 ruteo, 20 OTA, 5 coordinación,
  6 difusión y 5 despacho de recepción. Además, comprobación C/Python del
  protocolo OTA.
- La nueva prueba SPI retiene los ocho buffers, completa recepciones adicionales
  y comprueba que el driver sigue rearmado, sin espera por buffers de entrega.
- Verifica que los datos de aplicación no se alteran cuando DMA reutiliza sus
  buffers, que no se entregan tramas truncadas y que una cola llena no pierde
  buffers ni retrasa el rearme.
- La instrumentación de transporte comprueba sus contadores de CRC, cola llena
  y fallo de reenvío mediante los tests de recepción existentes.
- Paquete Root compilado con ESP-IDF 5.4.1. No se instaló automáticamente.

## Paquete nuevo para probar primero solo Root

Ruta: `app/build/link-capture/ramona-dma-20261019/`.
Aplicación: 807968 bytes.
Hash ELF:
`a965f3c7089a3193e7b78bc08b5efe95f3912ffc9e3825e650de2f909513a3b8`.

Cerrar monitores. Con Grillo y Ramona apagados, instalar el mismo paquete en
las cinco placas de Root, individualmente por USB:

```powershell
$Puerto = Read-Host "COM de la placa individual de Root"
python auxiliar/esp_rpi_flasher.py flash --port $Puerto --flash-args app/build/link-capture/ramona-dma-20261019/flash_args --baud 460800
```

Conservar NVS y los selectores de Root/orientación. No probar Root con hashes
mezclados. El nombre de variante `ramona` no determina el rol de Root.

Después de completar las cinco placas, apagarlas por completo, incluidos USB,
y abrir una captura nueva antes de encenderlas:

```powershell
$Captura = Join-Path (Get-Location) ("app\build\capturas\root-dma-" + (Get-Date -Format "yyyyMMdd-HHmmss-fff"))
$Captura | Set-Content app/build/captura-activa.txt
python auxiliar/capture_links.py start --port root_center=COM8 --port root_south=COM5 --port root_west=COM9 --output "$Captura" --notes "Root con pools DMA separados; Grillo y Ramona apagados"
```

Los puertos ausentes se reintentan. Encender las cinco placas de Root y conectar
sus monitores. Observar tres minutos desde que Root esté completamente
alimentado y terminar con Ctrl+C. No encender Grillo ni Ramona en esta corrida.

En otro PowerShell se pueden revisar los datos sin volver a abrir los COM:

```powershell
$Captura = (Get-Content app/build/captura-activa.txt -Raw).Trim()
Select-String -Path "$Captura\timeline.log" -Pattern "event=identity|boot_ring_probe|boot_traffic_resumed|event=spi|event=transport" | Select-Object -Last 45
```

Si sigue fallando, estos contadores permiten dirigir el análisis siguiente;
no prolongar la espera para intentar obtener una recuperación de nueve minutos.
Guardar la carpeta completa, no solamente la selección de 45 líneas.

## Resultado recibido: Root DMA, 09:21–09:24 de Argentina

En `root-dma-20261019-092054-434`, los monitores central, Sur y Oeste confirman
el nuevo hash `a965f3c7089a3193e7b78bc08b5efe95f3912ffc9e3825e650de2f909513a3b8`.
La captura comenzó con unos 278 segundos de uptime; no mide el tiempo completo
de arranque desde cero.

El central registra repetidamente `ready_mask=0x1e error=0x0`, y la coordinación
avanza normalmente (aproximadamente de secuencia 1036 a 1687 en el intervalo
revisado). El intercambio ya no presenta el timeout general de la corrida
anterior. Falta el bit 0, correspondiente a Norte. Esta máscara significa que
Norte no pasó alguno de los filtros de `collect_boot_status`: longitud de
respuesta, fase, estado de verificación/recuperación, aplicación lista o hash.
No demuestra que esté apagado: el intercambio terminó sin error.

Oeste mantiene en cero los contadores de error. En cambio, durante unos
144 segundos Sur aumentó de 13 a 22 tramas cortas y de 12 a 14 errores CRC;
el central aumentó de 21 a 29 CRC. Esas pérdidas siguen existiendo, aunque no
impidieron completar las consultas durante el intervalo. No atribuirles aún
la exclusión persistente de Norte.

La selección anterior de tres archivos y `Select-Object -Last 45` mostraba
solo el final del último archivo (Oeste). Se cambió el comando por
`timeline.log`, que conserva los tres monitores en la línea de tiempo conjunta.

Siguiente comprobación, después de terminar la captura con Ctrl+C:

```powershell
python gateway/flash_node.py --port COM8 --roles center,north,south,east,west --status --timeout 1 --retries 20
```

Conservar las cinco líneas de estado, especialmente Norte. Es una lectura;
no cargar firmware ni cambiar selectores antes de identificar qué filtro falla.

## Estado completo confirmado y siguiente paso

Se identificó el capturador todavía activo como PID 12160, en otro PowerShell.
Se envió Ctrl+C a su consola y terminó limpiamente a las 09:28:34, con
`failures=[]` en `summary.json`; no se finalizaron procesos a la fuerza.
COM8 quedó libre. La consulta completa se guardó como
`estado-cinco-placas.txt` dentro de esta carpeta de captura.

Todas las placas informan `phase=0 pending_verify=0 recovery_pending=0 ready=1
boot_paused=1`, ejecutando la partición `0x20000`. La diferencia es:

| Placas | Hash ELF instalado |
| --- | --- |
| Norte | `48221379f6eb0679c640b9da7a4edfe1a4f04c2bd6573b1db099a24731c91061` |
| Sur, Este, Oeste y central | `a965f3c7089a3193e7b78bc08b5efe95f3912ffc9e3825e650de2f909513a3b8` |

El hash de Norte corresponde al primer paquete diagnóstico. Esto identifica
la imagen realmente ejecutada, sin atribuir a qué operación de flasheo se debe
la diferencia. No hace falta volver a flashear las otras cuatro placas.

Con Grillo y Ramona apagados, conectar por USB la placa Norte de Root y usar
su COM real, distinto del central COM8:

```powershell
$PuertoNorte = Read-Host "COM de la placa Norte de Root conectada individualmente"
python auxiliar/esp_rpi_flasher.py flash --port $PuertoNorte --flash-args app/build/link-capture/ramona-dma-20261019/flash_args --baud 460800
```

Una vez completado el flasheo, encender el nodo Root completo con sus cinco
placas. Después consultar nuevamente por COM8 (con los monitores cerrados):

```powershell
python gateway/flash_node.py --port COM8 --roles center,north,south,east,west --status --timeout 1 --retries 20
```

Comprobar que las cinco líneas tengan el hash `a965f3c7…` completo y que, tras
completar la barrera, indiquen `boot_paused=0`. Si coincide el hash pero sigue
pausado, guardar el estado antes de otra intervención. La coincidencia de
versiones y una barrera liberada tampoco prueban por sí solas Internet desde
Grillo: ese es el siguiente ensayo, con Ramona todavía apagada.

## Root y Grillo: conectividad comprobada

En `root-grillo-20261019-094456-677`, Grillo incorporó sus rutas: default y
salida a RPi/Internet por orientación 3 (Este). Root anunció la ruta de retorno
`10.96.0.0/11` por orientación 3. La coordinación avanzó en ambos nodos.

El PC se conectó a Grillo mediante `Wi-Fi 2`, con IPv4 `10.116.0.2` y gateway
`10.116.0.1`. Se ejecutaron cinco pings por destino con `ping -4 -S 10.116.0.2`:

| Destino | Respuestas | RTT observado |
| --- | --- | --- |
| Gateway `10.116.0.1` | 5/5 | 2–50 ms |
| RPi `10.255.255.254` | 5/5 | 57–141 ms |
| Internet `1.1.1.1` | 5/5 | 70–178 ms |

Se guardaron comandos, origen, adaptador, salida y hora en
`pings-wifi2-verificacion.json`, dentro de esa captura, y una marca en la línea
de tiempo. Esto confirma alcance a esos destinos desde Grillo en esa muestra;
no demuestra estabilidad prolongada, DNS ni el comportamiento con Ramona.

Los monitores actuales ocupan COM8 (central Root), COM5 (Sur Root), COM9 (Oeste
Root) y COM7 (central Grillo). Para la siguiente captura con pings se pueden
conservar COM8, COM5 y COM7, dejando el cuarto monitor para el central de
Ramona. Cerrar un monitor no exige desconectar el USB que alimenta esa placa.
No apagar Root o Grillo para cambiar los monitores y no asumir que el COM9 de
una corrida antigua siga identificando Ramona.

Preparar otra captura antes de incorporar Ramona. Primero cerrar correctamente
la captura UART anterior y esperar que libere los COM. Con Root y Grillo
encendidos y Wi-Fi 2 todavía conectada a Grillo:

```powershell
$Captura = Join-Path (Get-Location) ("app\build\capturas\ramona-" + (Get-Date -Format "yyyyMMdd-HHmmss-fff"))
$Captura | Set-Content app/build/captura-activa.txt
python auxiliar/capture_links.py start --interface "Wi-Fi 2" --port root_center=COM8 --port root_south=COM5 --port grillo_center=COM7 --ping gateway=auto --ping rpi=10.255.255.254 --ping internet=1.1.1.1 --output "$Captura" --notes "Root y Grillo con Internet confirmado; Ramona apagada"
```

Guardar al menos 60 segundos de referencia sin Ramona. Si su COM se conoce,
agregarlo desde otro terminal mediante `add-port` antes de encenderla; si todavía
no se conoce, se puede incorporar al enumerarse su USB después del encendido.
La captura de Root/Grillo y los pings ya estará registrando la transición.

```powershell
$Captura = (Get-Content app/build/captura-activa.txt -Raw).Trim()
$Ramona = Read-Host "COM real del central de Ramona"
python auxiliar/capture_links.py add-port --output "$Captura" --port "ramona_center=$Ramona"
python auxiliar/capture_links.py mark --output "$Captura" "RAMONA_ON: antes de encender sus cinco placas"
```

La marca `RAMONA_ON` debe preceder al encendido físico, aunque el monitor de
Ramona se incorpore después. Mantener la conexión del PC en Grillo, sin
autoconexión a Ramona. No cambiar firmware ni cables en medio de esta corrida.
