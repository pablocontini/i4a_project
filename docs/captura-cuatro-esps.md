# Capturar la pérdida de Internet de Grillo al encender Ramona

**Evaluación actual:** ver
[evaluacion-pruebas-20261019.md](evaluacion-pruebas-20261019.md) para los
resultados y paquetes más recientes. La herramienta incorpora ahora
`stop --output DIR` y `start --ping-timeout-ms 3000`; el timeout predeterminado
permanece en 800 ms. Los comandos históricos de flasheo siguientes no indican
qué firmware está instalado actualmente.

**Última actualización del 19/10:** la primera corrección del despacho no
resolvió el ensayo de Root. Se reprodujo y corrigió además una espera del
driver SPI cuando la aplicación retiene buffers. Para el próximo ensayo usar
[correccion-spi-dma-20261019.md](correccion-spi-dma-20261019.md): contiene el
paquete nuevo, contadores de pérdidas e identidad periódica. Los paquetes y
hashes de las secciones siguientes documentan el firmware diagnóstico anterior.

Procedimiento del 19 de octubre de 2026. Se analiza la rama actual
`comnetar-stability-tools`: el usuario instaló `app/build/main.bin` por USB en
Root y Ramona y recompiló esa misma base para Grillo cambiando el SSID.
No atribuir este ensayo al firmware SOAK que estaba instalado anteriormente.

El síntoma no permite distinguir aún un cambio incorrecto de gateway, una ruta
de retorno perdida, una desconexión de radio, divergencia de tablas o un bloqueo
del anillo. Este procedimiento captura la transición antes de elegir otra
corrección. No requiere OTA.

## 1. Qué cuatro placas observar

| Etiqueta | Placa | Qué permite observar |
| --- | --- | --- |
| `root_center` | Central de Root | Tabla compartida y salida hacia la RPi |
| `grillo_center` | Central de Grillo | Tabla del nodo que pierde Internet |
| `grillo_uplink` | Periférica de Grillo enlazada directamente a Root | Asociación Wi-Fi, BSSID, RSSI, canal y coordinación |
| `ramona_center` | Central de Ramona | Momento en que el tercer nodo queda operativo y recibe sus rutas |

Identificar físicamente los puertos con `python -m serial.tools.list_ports -v`.
No asumir que COM5 corresponde siempre al mismo nodo. **No hace falta conocer
la periférica uplink para empezar:** capturar primero los centrales de Root y
Grillo. Cuando se identifique la periférica que enlazó, incorporarla a la misma
captura con `add-port`, como se muestra abajo. La marca
`event=radio sta_error=0` y su BSSID ayudan a confirmar el enlace después de
abrir ese monitor. Un periférico agregado después no conserva su historia
anterior a la apertura; los centrales sí conservan su captura inicial.

Con uno, dos o tres puertos también se puede capturar: omitir los restantes.
Como segunda corrida, sustituir `ramona_center` por la periférica de Root que
enlaza con Grillo si las tablas o la ruta de retorno apuntan a ese salto.

## 2. Firmware con información suficiente

El build utilizado tenía `CONFIG_LOG_DEFAULT_LEVEL_NONE=y`. La captura básica
conserva boot, panic y cualquier salida existente, pero sin instrumentación no
muestra cambios periódicos de tabla. Se agrega una opción diagnóstica de esta
misma rama, desactivada en el build normal:
`CONFIG_I4A_CAPTURE_DIAGNOSTICS=y`.

Cada dos segundos imprime `I4ACAP` con:

- identidad: UUID, orientación, hash ELF y motivo de arranque;
- rutas: gateway por defecto, tabla completa y salida seleccionada para RPi e Internet;
- coordinación: sesión, secuencia, turno completado, propietario, token y colas pendientes;
- radio: modo, asociación Station, BSSID, RSSI, canal y número de clientes AP;
- memoria: heap, mínimo, mayor bloque y mínimo de pila de la tarea diagnóstica.

La lectura de cada mutex espera como máximo 20 ms. Si no obtiene el dato muestra
`available=0`, en lugar de imprimir una tabla vacía. Las copias de tabla, colas y
token se toman por separado: no constituyen una transacción atómica conjunta.
No se retienen mutex de ruteo al imprimir ni consultar Wi-Fi. La captura agrega
una tarea de 4096 bytes y tráfico UART; puede alterar el tiempo del fallo.
`diag_stack_min` mide solo esa tarea, no todas las tareas del ESP.

Generar las dos variantes sin editar el SSID en el código:

```powershell
python auxiliar/build_link_capture.py --variant ramona
python auxiliar/build_link_capture.py --variant grillo
```

Se utiliza el entorno ESP-IDF activado o las rutas del build local existente.
La configuración de flash, particiones y tiempos se copia de `app/sdkconfig`;
no se modifica ese archivo ni `app/build/main.bin`. El build de diagnóstico es
independiente. Las salidas son:

```text
app/build/link-capture/ramona/
app/build/link-capture/grillo/
```

Un destino existente se conserva: para recompilar usar `--output` con una carpeta
nueva. No ejecutar dos builds diagnósticos simultáneamente. Cada paquete contiene
`main.bin`, `main.elf`, mapa, sdkconfig, fuentes exactas en ZIP, manifiestos y todas
las imágenes referenciadas por `flash_args`.

Instalar el paquete **ramona en las cinco placas de Root y las cinco de Ramona**;
instalar el paquete **grillo en las cinco de Grillo**. Root decide su modo por la
configuración del nodo, no por el nombre de la variante. No mezclar hashes dentro
de un nodo: su barrera de arranque requiere firmware coincidente.

Para cada placa de Root o Ramona, conectada individualmente por USB:

```powershell
$Puerto = Read-Host "Puerto de la placa individual de Root o Ramona"
python auxiliar/esp_rpi_flasher.py flash --port $Puerto --flash-args app/build/link-capture/ramona/flash_args --baud 460800
```

Para cada placa de Grillo:

```powershell
$Puerto = Read-Host "Puerto de la placa individual de Grillo"
python auxiliar/esp_rpi_flasher.py flash --port $Puerto --flash-args app/build/link-capture/grillo/flash_args --baud 460800
```

Conservar NVS: no agregar `erase_flash` ni `--erase-first`. Mantener el mismo
layout y tamaño físico de flash que se configuró para el ensayo original.
El diagnóstico no cambia el protocolo de comunicación; la coincidencia del hash
de arranque sí exige completar todas las placas antes de probar el nodo.

## 3. Preparar la alimentación y el PC

Cerrar otros monitores, esptool y consultas OTA que usen esos puertos. El
capturador no transmite comandos ni solicita reset por DTR/RTS. Algunos
adaptadores pueden generar un pulso al abrirse: abrir los monitores antes de la
referencia y distinguir ese arranque del fallo provocado por Ramona.

**Este procedimiento se puede realizar solamente con los USB de las placas.**
No se necesitan adaptadores UART externos ni cables en los GPIO. Puede perderse
el comienzo del boot mientras Windows enumera el USB; el objetivo principal es
capturar la incorporación de Ramona a la red y la pérdida de Internet de Grillo,
no garantizar el primer byte del arranque.

Preparar el encendido con USB de esta manera:

1. Antes del ensayo, conectar el central de Ramona, identificar y anotar su COM.
   Después apagar Ramona desconectando toda su alimentación, incluidos los USB
   que puedan alimentar cualquiera de sus cinco placas.
2. Dejar Root y Grillo funcionando y abrir la captura con los cuatro COM anotados.
   Es normal que `ramona_center` indique puerto ausente mientras Ramona está
   apagada. Los otros tres monitores deben estar conectados y registrando.
3. Tras la referencia estable, guardar la marca `RAMONA_ON`, encender las cinco
   placas de Ramona y conectar el USB de su central inmediatamente. Usar el mismo
   conector del PC o hub donde se identificó el COM.
4. El capturador reintenta abrir ese COM cada segundo. Esperar el mensaje
   `connected ramona_center` y comprobar que su archivo `.log` recibe datos.
   No hace falta detener la captura ni abrir otro monitor.

Si las cinco placas de Ramona se alimentan por USB, conectar sus alimentaciones
en esa misma fase; los USB de alimentación adicionales no requieren monitores.
Un hub dedicado puede facilitar la operación, siempre que al apagarlo se quite
toda alimentación de Ramona, también la que pueda llegar desde el PC.

**Cortar solo la fuente externa no apaga una DevKit alimentada por USB.**
Al marcar `RAMONA_OFF`, desconectar también sus USB. Conservar conectados los
monitores de Root y Grillo durante todo el ensayo. La reconexión permite observar
el ensayo con USB normales, pero no garantiza conservar un panic que ocurra antes
de que el puerto de Ramona vuelva a abrirse.

Conectar **Wi-Fi 2 a Grillo** y usar `--interface "Wi-Fi 2"` en el capturador.
La otra interfaz Wi-Fi puede conservar su conexión habitual. El capturador fija
la IPv4 de origen mediante `ping -4 -S` y toma el gateway de Wi-Fi 2 con
`--ping gateway=auto`. Verifica que el adaptador esté activo, tenga una sola IPv4
utilizable y tenga WeakHostSend desactivado; si no, se detiene antes de abrir
los COM. No modifica rutas ni configuración de adaptadores.

La IPv4 queda fija durante toda la captura: si cambia o se desconecta Wi-Fi 2,
los intentos fallan, sin quitar la selección de origen para usar otra conexión.
Reiniciar la captura en otra carpeta si se renueva la IP. El nombre real en este
PC es `Wi-Fi 2`; también se acepta `WIFI-2`. Verificarlo antes de empezar:

```powershell
Get-NetIPConfiguration -InterfaceAlias "Wi-Fi 2"
```

El capturador registra adaptador, índice, origen y gateway en `session.json`,
y el origen en cada resultado de ping. También guarda `ipconfig`, rutas e interfaz Wi-Fi al inicio,
en cada marca y al terminar. No habilitar conexión automática a Ramona durante
este ensayo, para separar un cambio del PC de un fallo de la malla.

## 4. Iniciar la captura

Desde la raíz del proyecto, en PowerShell:

```powershell
python -m pip install -r gateway/requirements-ota.txt
python -m serial.tools.list_ports -v
ipconfig

$Root = Read-Host "COM del central de Root"
$Grillo = Read-Host "COM del central de Grillo"
$Uplink = Read-Host "COM de la periferica de Grillo hacia Root"
$Ramona = Read-Host "COM del central de Ramona"
$Captura = Join-Path (Get-Location) ("app\build\capturas\" + (Get-Date -Format "yyyyMMdd-HHmmss-fff"))
$Captura | Set-Content app/build/captura-activa.txt

python auxiliar/capture_links.py start --interface "Wi-Fi 2" --port "root_center=$Root" --port "grillo_center=$Grillo" --port "grillo_uplink=$Uplink" --port "ramona_center=$Ramona" --ping gateway=auto --ping rpi=10.255.255.254 --ping internet=1.1.1.1 --output "$Captura" --notes "Root y Grillo operativos; Ramona apagada al inicio"
```

Dejar este terminal abierto. Los COM deben ser distintos. Usar otra dirección
externa si la referencia no responde a ICMP; no interpretar un destino que nunca
respondió como una caída nueva. Windows puede retornar código 0 para “host
inaccesible”: el capturador requiere una respuesta con TTL para contar el ping.

En **otro PowerShell**, también en la raíz, cargar la misma ruta:

```powershell
$Captura = (Get-Content app/build/captura-activa.txt -Raw).Trim()
```

La siguiente marca se usa **solo después de comprobar que Internet funciona
desde Wi-Fi 2 a través de Grillo**. Que el PC se asocie a su Wi-Fi no basta:
confirmar respuestas al gateway, a la RPi y al destino externo durante al menos
60 segundos. Si un destino nunca responde, anotarlo y revisar la referencia
antes de interpretar una caída.

```powershell
python auxiliar/capture_links.py mark --output "$Captura" "GRILLO_OK: Internet estable sin Ramona"
```

### Si Root y Grillo no enlazan o arrancan sin Internet

Dejar Ramona apagada. Este fallo de arranque se captura primero, en una carpeta
nueva; no requiere que Internet funcione. Si Wi-Fi 2 aún no tiene IPv4/gateway,
iniciar una captura **solo UART**, omitiendo `--interface` y todos los `--ping`:
Comenzar con los dos centrales; el periférico se puede agregar después de
identificarlo. No hace falta completar `$Uplink` para iniciar esta corrida.

```powershell
$Captura = Join-Path (Get-Location) ("app\build\capturas\" + (Get-Date -Format "yyyyMMdd-HHmmss-fff"))
$Captura | Set-Content app/build/captura-activa.txt
python auxiliar/capture_links.py start --port "root_center=$Root" --port "grillo_center=$Grillo" --output "$Captura" --notes "Arranque Root y Grillo; Ramona apagada; periferico por identificar"
```

Desde un **segundo PowerShell**, después de identificar y conectar por USB la
periférica enlazada, sin detener la captura:

```powershell
$Captura = (Get-Content app/build/captura-activa.txt -Raw).Trim()
$Uplink = Read-Host "COM de la periferica de Grillo que enlazo a Root"
python auxiliar/capture_links.py add-port --output "$Captura" --port "grillo_uplink=$Uplink"
```

El primer terminal debe mostrar `port_added grillo_uplink` y luego
`connected grillo_uplink`. Si el COM todavía no está disponible, lo reintenta
cada segundo. Se conserva la misma carpeta y línea de tiempo; `session.json`
incorpora el puerto. Se rechazan etiquetas o COM repetidos y un quinto monitor.
`add-port` requiere una captura iniciada con esta versión del script: un proceso
que ya estaba corriendo con una versión anterior debe terminarse y comenzar
otra captura en una carpeta nueva. El cambio no requiere reflashear los ESP.

Si Grillo nunca enlaza y no se puede identificar la periférica, conservar la
captura de ambos centrales igualmente. No reiniciar solo para elegir un monitor.
Verificar que ambos centrales impriman `board=5` en las líneas `I4ACAP`;
`board=1` a `board=4` identifica una periférica, aunque la etiqueta del COM diga
`root_center` o `grillo_center`.

Los puertos ausentes se reintentan. Abrir la captura antes de conectar/alimentar
los nodos; con USB puede perderse el comienzo del boot mientras se enumera el
puerto. Encender primero las cinco placas de Root, luego las cinco de Grillo,
guardando una marca antes de cada acción desde el segundo PowerShell:

```powershell
$Captura = (Get-Content app/build/captura-activa.txt -Raw).Trim()
python auxiliar/capture_links.py mark --output "$Captura" "ROOT_ON: comienzo del encendido"
# Encender Root; luego, antes de encender Grillo:
python auxiliar/capture_links.py mark --output "$Captura" "GRILLO_ON: comienzo del encendido"
```

Anotar el tiempo real entre ambos encendidos. Tras encender Grillo, observar
al menos 180 segundos sin reiniciar: es una ventana para conservar evidencia,
no un tiempo garantizado de conexión. Marcar el resultado observado:

```powershell
python auxiliar/capture_links.py mark --output "$Captura" "GRILLO_SIN_ENLACE: no conecta a Root"
# O, si enlaza pero no tiene Internet:
python auxiliar/capture_links.py mark --output "$Captura" "GRILLO_SIN_INTERNET: enlazado pero sin salida"
```

Si se reproduce el efecto de reiniciar Root, marcar `ROOT_RESTART` justo antes
y conservar al menos otros 180 segundos. Reiniciar el nodo completo como en la
reproducción original y anotar qué placas se reiniciaron; no cambiar firmware
durante la corrida. No encender Ramona ni usar `GRILLO_OK` mientras este fallo
persista. Si luego se recupera Internet, registrar `GRILLO_RECUPERADO`.

La captura solo UART no ejecuta pings. Cuando Wi-Fi 2 ya tenga IP, se puede
registrar una comprobación puntual desde otro PowerShell, incluso sin Internet:

```powershell
$RedGrillo = Get-NetIPConfiguration -InterfaceAlias "Wi-Fi 2"
$OrigenGrillo = $RedGrillo.IPv4Address.IPAddress
$GatewayGrillo = $RedGrillo.IPv4DefaultGateway.NextHop
# Ejecutar solo si se obtuvo una IPv4 valida y un gateway de Grillo:
ping -4 -S $OrigenGrillo -n 5 $GatewayGrillo
ping -4 -S $OrigenGrillo -n 5 10.255.255.254
ping -4 -S $OrigenGrillo -n 5 1.1.1.1
```

Guardar esa salida y una marca con la hora de la comprobación. Para el ensayo
posterior con Ramona, terminar esta captura y comenzar otra con pings y una
referencia estable de Root y Grillo.

## 5. Repetir el disparador una sola vez

1. Mantener Root y Grillo funcionando y Ramona apagada. Confirmar pings de
   referencia y guardar al menos 60 segundos antes del cambio.
2. Marcar la acción y luego encender **las cinco placas de Ramona**:

   ```powershell
   python auxiliar/capture_links.py mark --output "$Captura" "RAMONA_ON: comienzo del encendido"
   ```

3. Cuando se observe la caída, agregar una marca sin reiniciar ningún nodo:

   ```powershell
   python auxiliar/capture_links.py mark --output "$Captura" "GRILLO_SIN_INTERNET: caida observada"
   ```

4. Conservar al menos 180 segundos posteriores. Una captura de unos 12 minutos
   tras el fallo también cubre el ciclo AP/Station de 10 minutos de esta rama.
5. Solo después de guardar esa evidencia, marcar `RAMONA_OFF` y apagar Ramona
   completamente. Mantener otros 180 segundos para observar si Grillo recupera.
6. Terminar con **Ctrl+C en el primer terminal**. No cerrar a la fuerza: así se
   conservan líneas incompletas y el resumen final.

Las marcas se guardan con su hora original incluso si tardan en incorporarse a
la línea de tiempo. Son la hora de la acción manual, no una medida eléctrica
exacta del encendido. No cambiar canales, firmware ni posiciones durante la
misma corrida. Repetir en una carpeta nueva si se cambia una condición.

## 6. Qué conservar y cómo interpretar

- `timeline.log`: lectura conjunta de las cuatro UART, marcas, pings y estado del PC.
- `timeline.jsonl`: los mismos eventos en formato estructurado, con hora UTC,
  tiempo monotónico transcurrido, secuencia y etiqueta de origen.
- `*.raw`: bytes originales de cada UART, incluidos panic/backtrace y datos no UTF-8.
- `*.log`: texto de cada ESP; `session.json`, `summary.json` y `markers/`.
- `capture-manifest.json` y el `main.elf` **de cada variante instalada**.

Las marcas de las UART corresponden a recepción por el PC, no a un reloj
sincronizado de los ESP. La lectura evita esperar a llenar un bloque grande,
pero existe latencia de UART/USB y planificación. Los pings se registran cuando
termina cada intento y conservan su duración y salida completa.

| Evidencia al encender Ramona | Qué investigar después |
| --- | --- |
| Falla gateway del cliente y cambia el SSID/IP del PC | Asociación del cliente o configuración del hotspot |
| Gateway responde, pero RPi e Internet caen | Camino de malla hacia Root o ruta de retorno |
| RPi responde y solo falla destino externo | Salida WAN, no una pérdida completa del anillo |
| Cambia `default` o `entries` al entrar el tercer nodo | Evento que reemplaza gateway/ruta; comparar Root y las dos capturas de Grillo |
| Tablas distintas entre central y periférica de Grillo durante varios ciclos | Publicación o circulación del token dentro del nodo |
| Secuencia/completado no avanza y crecen colas o `pending_publish` | Coordinación bloqueada, token o publicación pendiente |
| `sta_error` cambia de 0 a error en la periférica uplink | Pérdida de asociación Wi-Fi; correlacionar BSSID y canal |
| Boot, panic, brownout o backtrace antes de la caída | Primer ESP que falla y análisis con el ELF correspondiente |

Orientaciones de ruteo: 1=Norte, 2=Sur, 3=Este, 4=Oeste, 5=Central; salida 0
significa ninguna. Un `default` válido no prueba por sí solo entrega remota.
La memoria estable tampoco prueba que el anillo esté progresando.

Para observar una UART sin abrir por segunda vez su COM:

```powershell
Get-Content "$Captura\grillo_uplink.log" -Wait
```

Una vez terminada la captura, comprimir el conjunto:

```powershell
Compress-Archive -LiteralPath "$Captura" -DestinationPath ($Captura + ".zip")
```

No se ha reproducido el fallo físico durante el desarrollo de estas herramientas.
La captura permitirá atribuirlo antes de modificar nuevamente la selección de rutas.

## Validación de las herramientas

- Once pruebas del capturador aprobadas, incluyendo incorporación de UART a una
  captura iniciada, continuidad de datos y rechazo de duplicados o un quinto COM,
  además de selección de interfaz,
  rechazo de adaptadores desconectados y fijación de la IPv4 de origen.
  Se comprobó `ping -4 -S` con loopback en este Windows y el rechazo de Wi-Fi 2
  desconectada antes de abrir UART. La ruta real por Grillo requiere conectarla.
- Las 47 pruebas Python previas estaban aprobadas, incluidas ocho del capturador y dos de la
  configuración diagnóstica. Se simulan cuatro UART, desconexiones, bytes no
  UTF-8, líneas incompletas, puertos repetidos, marcas y respuestas ICMP.
- Regresiones nativas: 18 de ruteo, 5 de coordinación y 20 OTA aprobadas.
- Ambos paquetes compilados con ESP-IDF 5.4.1; tamaños y hashes de todos los
  segmentos exportados verificados. La compilación con diagnóstico desactivado
  también se comprobó con GCC Xtensa. No se abrieron puertos ni se instalaron
  imágenes durante esta validación.

Paquetes preparados el 19/10, aplicaciones de 807200 bytes:

| Paquete | Hash ELF que debe aparecer en `event=identity` |
| --- | --- |
| Root / Ramona | `48221379f6eb0679c640b9da7a4edfe1a4f04c2bd6573b1db099a24731c91061` |
| Grillo | `014a06d3cfa7071bfa196a64d4258f347dabebad1975d2ddf911647e9ab1b36e` |

Estos hashes corresponden a los paquetes ya generados. Una recompilación puede
tener otra identidad; usar siempre el manifiesto y el ELF de la imagen instalada.
