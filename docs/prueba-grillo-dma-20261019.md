# Prueba de Grillo con la corrección DMA, sin Ramona

La evaluación posterior, las correcciones nuevas y sus pruebas están en
[evaluacion-pruebas-20261019.md](evaluacion-pruebas-20261019.md). El procedimiento
de actualización DMA de este documento ya se completó en Root y Grillo.

## Resultado posterior a la actualización, 10:16–10:19

La captura `root-grillo-dma-20261019-101607-281` duró 180,7 segundos y
terminó correctamente. Root y Grillo ejecutaban sus variantes DMA; Ramona
permaneció apagada. La lectura inicial de Grillo con cuatro `ready=0` fue
transitoria: una consulta posterior confirmó las cinco placas listas y sin
pausa de arranque.

| Destino | Recibidos | Perdidos |
| --- | --- | --- |
| Gateway de Grillo | 177 | 3 |
| RPi | 154 | 25 |
| Internet | 156 | 23 |

No se observaron retrocesos de uptime en los tres monitores ni aumentos de
`no_buffer`, `queue_full`, `short` o `tx_fail` durante esta ventana. Sí
aumentaron los errores CRC: tres en central de Root, tres en Sur de Root y
seis en central de Grillo. Son incrementos durante la captura, no totales
acumulados desde el arranque. No permiten atribuir todavía todas las pérdidas
a una causa física o de software.

La salida de Grillo hacia RPi e Internet permanecía en `default=3`, Este.
El siguiente monitor necesario es la placa Este de Grillo para observar el
enlace de radio hacia Root. Mantener Root y Grillo encendidos y Ramona apagada;
identificar el COM de Este antes de iniciar la siguiente captura de cuatro
placas. Los resultados completos están en `analisis-referencia-dma.json`.

## Antecedentes y procedimiento utilizado

La captura `ramona-20261019-095515-501` registró pérdidas también después de
apagar Ramona. Se cerró correctamente y conservó `summary.json` con
`failures=[]`. No se identificó una caída permanente causada exclusivamente
por la incorporación de Ramona en esta corrida: ya estaba encendida al iniciar.

Resultados guardados en `comparacion-ramona.json`:

| Ventana | Gateway: perdidos/intentos | RPi: perdidos/intentos | Internet: perdidos/intentos |
| --- | --- | --- | --- |
| 60 s antes de desconectar COM17 | 1/60 | 7/59 | 14/59 |
| 186 s después de confirmar apagado | 6/185 | 37/184 | 51/184 |
| Últimos 60 s, Ramona apagada | 2/59 | 7/59 | 8/59 |

Las ventanas usan la hora de finalización del ping. El primer límite es la
desconexión USB observada, no una medición eléctrica de las cinco placas.
La comparación confirma que las pérdidas persisten sin Ramona; no atribuye
por sí sola la causa a una placa o a un cambio de topología.

## Versión realmente instalada en Grillo

Con los monitores ya cerrados se consultaron las cinco placas por COM7.
Todas respondieron `ready=1 boot_paused=0`, pero aún ejecutan
`014a06d3cfa7071bfa196a64d4258f347dabebad1975d2ddf911647e9ab1b36e`.
Es el diagnóstico original, sin las correcciones de despacho y recepción DMA
aplicadas a Root. Se había conservado para comparar un cambio por vez.
La consulta está en `estado-grillo-antes-dma.txt`, en la carpeta de captura.

Es válido que dos nodos tengan hashes diferentes; la barrera exige coincidencia
dentro de cada nodo. Lo relevante aquí es que Grillo todavía contiene dos
defectos reproducidos del transporte. Actualizarlo permite comprobar si esos
cambios mejoran el enlace completo. No garantiza resolver las pérdidas restantes:
también hubo tramas cortas/CRC en Root y no se ha descartado la parte de radio.

## Paquete preparado

- Carpeta: `app/build/link-capture/grillo-dma-20261019/`.
- SSID: `ComNetAR - Grillo`.
- Aplicación: 807968 bytes.
- ELF: `52a3f3204f525ea3bad22fff96526a5a0c4f3a7060af5ac44499198c6fd004b1`.
- Mismas fuentes de ejecución que la variante DMA instalada en Root; cambia el
  parámetro de SSID. Se verificaron los cuatro segmentos, identidad ELF, ZIP
  de fuentes y coincidencia del código con el paquete de Root.
- Compilado con ESP-IDF 5.4.1; las 77 pruebas nativas de esas fuentes ya pasaron.
  Esta variante de Grillo aún no se probó en las placas.

## Actualizar únicamente las cinco placas de Grillo

Dejar Ramona apagada. Root conserva su firmware y puede seguir encendido.
Apagar Grillo completo. Conectar individualmente por USB cada placa para
flashearla, manteniendo las otras placas de Grillo apagadas durante esa operación.
Cerrar monitores que usen el COM de la placa.

```powershell
$Puerto = Read-Host "COM de la placa individual de Grillo"
python auxiliar/esp_rpi_flasher.py flash --port $Puerto --flash-args app/build/link-capture/grillo-dma-20261019/flash_args --baud 460800
```

Repetir para sus cinco placas. No cambiar NVS, selectores ni nombre de red.
Después encender Grillo completo y conectar el central a su COM habitual.
Con los monitores cerrados, consultar:

```powershell
python gateway/flash_node.py --port COM7 --roles center,north,south,east,west --status --timeout 1 --retries 20
```

Verificar el hash nuevo en las cinco líneas. Tras completar la barrera deben
mostrar `boot_paused=0`; una lectura inicial puede preceder los cinco segundos
de reanudación. Si un hash difiere, identificar esa placa antes de otro ensayo.
No interpretar una conexión al hotspot como prueba de Internet.

## Referencia de Root y Grillo actualizados

Reconectar Wi-Fi 2 a Grillo y confirmar su IPv4/gateway. Verificar los COM si
se cambiaron conectores USB. Iniciar otra captura, con Ramona todavía apagada:

```powershell
$Captura = Join-Path (Get-Location) ("app\build\capturas\root-grillo-dma-" + (Get-Date -Format "yyyyMMdd-HHmmss-fff"))
$Captura | Set-Content app/build/captura-activa.txt
python auxiliar/capture_links.py start --interface "Wi-Fi 2" --port root_center=COM8 --port root_south=COM5 --port grillo_center=COM7 --ping gateway=auto --ping rpi=10.255.255.254 --ping internet=1.1.1.1 --output "$Captura" --notes "Root y Grillo con DMA separado; Ramona apagada"
```

Mantener tres minutos y terminar con Ctrl+C en ese mismo terminal. Conservar
la carpeta completa para comparar pings y contadores con las pérdidas anteriores.
No incorporar Ramona hasta evaluar esta referencia. Los contadores de Grillo
permitirán analizar su transporte si las pérdidas continúan.
