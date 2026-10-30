# OTA para validar la estabilidad con cuatro nodos

Rama: `fix/stable-links-neighbor-restart`.

Esta versión corrige pérdidas de sesión ante una renovación SPI fallida,
cambios de gateway al reiniciar un vecino y confirmaciones de caminos
simultáneos. Las pruebas automáticas y la compilación pasaron; queda pendiente
validar el síntoma en los equipos. El [informe del cambio](cuarto-nodo-20261030.md)
contiene el análisis y las limitaciones conocidas.

## 1. Descargar el código y los binarios

Desde PowerShell, en una carpeta donde guardar una copia nueva:

```powershell
git clone --branch fix/stable-links-neighbor-restart --single-branch https://github.com/pablocontini/i4a_project.git i4a-validacion-cuatro-nodos
cd i4a-validacion-cuatro-nodos
python -m pip install -r gateway/requirements-ota.txt
```

El repositorio incluye [binarios de aplicación y sus hashes](../firmware/validacion-20261030/README.md):

| Nodo | Imagen OTA |
|---|---|
| Raíz o nodo llamado Grillo | `firmware/validacion-20261030/grillo/main.bin` |
| Nodo llamado Ramona | `firmware/validacion-20261030/ramona/main.bin` |
| Nodo con otro nombre | Compilar su imagen según el apartado 4 |

El rol raíz y las orientaciones se seleccionan con los pines físicos.
La raíz emite `Internet4All_Root` aunque use la imagen Grillo. En los otros
nodos, el SSID de clientes está compilado: no usar la imagen Grillo en todos
si se quieren conservar nombres distintos. La OTA conserva contraseñas,
ajustes y bloques guardados en NVS.

Los binarios incluidos son para ESP32 con flash de **4 MiB** y la tabla
`partitions.csv`: cada slot de aplicación tiene **2 031 616 bytes**. Las cinco
placas deben tener previamente receptor OTA, dos slots y bootloader con
rollback. Si la consulta muestra otro tamaño de slot, preparar una compilación
con el mapa correspondiente; la OTA no instala bootloader ni particiones.

## 2. Consultar las cinco placas de un nodo

Conectar únicamente su central por USB a la PC y mantener las cinco placas
alimentadas y el anillo SPI completo. Cerrar el monitor de ese COM. `COM5` es
un ejemplo: reemplazarlo por el puerto real del central que se va a actualizar.

```powershell
$Puerto = "COM5"
$Roles = "center,north,south,east,west"
$Imagen = "firmware/validacion-20261030/grillo/main.bin"

python gateway/flash_node.py --port $Puerto --roles $Roles --status --timeout 30 --retries 5
```

Antes de cargar deben responder los cinco roles con `phase=0`, `ready=1`,
`boot_paused=0`, `pending_verify=0` y `recovery_pending=0`. Para los binarios
incluidos, verificar también `slot=2031616`. Dentro de cada nodo las cinco
placas deben ejecutar el mismo hash ELF; los hashes entre nodos con distintos
SSID pueden ser diferentes.

Si no responden las cinco placas, guardar la salida y resolver esa condición
antes de cargar. `--local-status` consulta solo el central y no sustituye esta
comprobación del anillo.

## 3. Actualizar y comprobar el resultado

Usar el mismo `$Puerto`, `$Roles` y `$Imagen` del apartado anterior:

```powershell
$CarpetaLogs = "app/build/ota-validacion"
New-Item -ItemType Directory -Force -Path $CarpetaLogs | Out-Null
$Log = Join-Path $CarpetaLogs ("ota-{0}-{1}.log" -f $Puerto, (Get-Date -Format "yyyyMMdd-HHmmss"))

python -u gateway/flash_node.py --port $Puerto --roles $Roles --image $Imagen --timeout 30 --retries 5 --boot-timeout 300 --verbose 2>&1 | Tee-Object -FilePath $Log
if ($LASTEXITCODE -ne 0) { throw "La OTA no quedó verificada. Conservar alimentación y revisar el log: $Log" }

python gateway/flash_node.py --port $Puerto --roles $Roles --status --timeout 30 --retries 5
```

El programa distribuye el mismo `main.bin` a las cinco placas, valida la imagen,
selecciona las particiones y coordina el reinicio. Termina correctamente cuando
aparece:

```text
Verified: every selected role booted and confirmed the new firmware.
```

La consulta final debe conservar los cinco estados sanos indicados antes y
mostrar en todos el hash ELF de la imagen elegida, indicado en su
`capture-manifest.json`. Llegar a 100 % no acredita por sí solo la actualización.

Si falla o se interrumpe, conservar el log, mantener alimentación al menos
120 segundos y volver a consultar `--status` antes de reintentar. Un error de
acceso al COM requiere cerrar la aplicación que lo tenga abierto. Si no existe
receptor OTA o el mapa instalado no es compatible, usar el procedimiento de
[primera instalación por USB](actualizacion_firmware.md).

Repetir el procedimiento **un nodo por vez**, comenzando por la raíz y siguiendo
con los demás. Actualizar las **cinco placas juntas en cada nodo**. El formato
interno de routing SPI pasa de 132 a 144 bytes: no se admite dejar versiones
mezcladas dentro del mismo nodo. Los mensajes Wi-Fi entre nodos conservan su
formato, lo que permite actualizar los nodos secuencialmente. No omitir roles
ni actualizar solo el central para esta migración.

## 4. Compilar para otro nombre de red

Solo se necesita ESP-IDF **5.4.1** para generar una variante nueva; los dos
binarios incluidos se pueden cargar con Python sin instalar ESP-IDF.

En una terminal ESP-IDF, desde esta misma copia nueva del repositorio:

```powershell
idf.py -C app set-target esp32
python auxiliar/build_link_capture.py --ssid "ComNetAR - Nodo 4" --output app/build/validacion-nodo4
```

El nombre admite de 1 a 31 bytes UTF-8. Cambiarlo por el SSID que debe emitir
ese nodo. Para placas con otro tamaño de flash, configurar previamente con
`idf.py -C app menuconfig` el tamaño, la tabla de particiones y rollback según
la instalación existente; el generador usa ese `app/sdkconfig`.

Después, usar el procedimiento anterior con:

```powershell
$Imagen = "app/build/validacion-nodo4/main.bin"
```

Elegir otra carpeta `--output` al generar la siguiente variante: el generador
conserva los paquetes existentes y no los sobrescribe. Guardar el manifiesto
y el log OTA de cada nodo para identificar exactamente qué imagen se probó.

## 5. Ensayo que debe realizar el compañero

Con las cinco placas verificadas en cada nodo, registrar primero la red con
tres nodos y luego incorporar el cuarto. Mantener un cliente y pings por el
nodo cuyo enlace cambiaba. Repetir reiniciando un vecino lateral y comprobar
si el enlace principal y la salida a Internet se conservan.

Anotar el instante de cada encendido/reinicio y conservar la captura completa,
la topología antes/después y los logs OTA. Con cuatro USB, priorizar el central
afectado, su radio principal, el central de la raíz y el central del vecino que
se incorpora. La [guía de captura](captura-cuatro-esps.md) describe la captura
simultánea. Una conmutación por una falla real del principal debe seguir
recuperando conectividad; conservar el principal sano no implica bloquear esa
recuperación.
