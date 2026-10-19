# Próxima prueba: Root aislado, sin reflashear

La captura `20261019-062637-751` finalizó el 19/10/2026 a las 06:35:59
(Argentina), sin errores del capturador. Se conservaron tres monitores.
Entre 06:33:01 y 06:35:56 el central de Root registró siete comprobaciones
con `ready_mask=0x10 error=0x107` y `ack_mask=0x0`: no logró validar periféricas.
Eso no significa que las cuatro estén apagadas; COM5 sí imprimió diagnóstico
y recibió tráfico. Grillo había completado su barrera con `0x1f` y reanudado
tráfico a las 06:27:53.

No se puede atribuir aún la falla a un cable o a una línea concreta de código.
No reflashear, borrar NVS ni cambiar cables durante esta comparación.

## 1. Apagar los tres nodos

Mantener Grillo y Ramona completamente apagados, incluidos USB que puedan
alimentarlos. Apagar también las cinco placas de Root antes de iniciar.
Se encenderá Root con sus cinco placas y anillo completo; no arrancar solamente
su central. Conservar la alimentación normal de la RPi/gateway para no cambiar
otra condición de la salida de Root.

Puertos conocidos de la corrida anterior:

| Puerto | Placa confirmada |
| --- | --- |
| COM8 | Central de Root, `board=5` |
| COM5 | Sur de Root, `board=2` |
| COM7 | Central de Grillo, `board=5` |

Usar los mismos conectores USB; si Windows reasigna COM, corregirlos. No abrir
otro monitor ni consultas OTA sobre esos mismos puertos durante la captura.

## 2. Iniciar captura antes de encender

Desde la raíz del proyecto, PowerShell:

```powershell
$Captura = Join-Path (Get-Location) ("app\build\capturas\root-aislado-" + (Get-Date -Format "yyyyMMdd-HHmmss-fff"))
$Captura | Set-Content app/build/captura-activa.txt
python auxiliar/capture_links.py start --port root_center=COM8 --port root_south=COM5 --port grillo_center=COM7 --output "$Captura" --notes "Root primero; Grillo y Ramona apagados; mismos firmware y cables"
```

Es normal que aparezcan puertos ausentes mientras los nodos están apagados.
El capturador reintenta. USB puede perder las primeras líneas de arranque
mientras Windows enumera el dispositivo; las comprobaciones periódicas quedan
registradas después. No requiere Wi-Fi del PC, IP ni pings.

En un segundo PowerShell:

```powershell
$Captura = (Get-Content app/build/captura-activa.txt -Raw).Trim()
python auxiliar/capture_links.py mark --output "$Captura" "ROOT_SOLO_ON: Grillo y Ramona apagados"
```

Ahora encender las cinco placas de Root y conectar sus dos USB de monitor.
Esperar aproximadamente tres minutos desde que se encendió el nodo completo,
sin reiniciar. Ese intervalo permite registrar varios intentos; no es una
garantía de que Root deba conectar o validar en un plazo fijo.

## 3. Decidir según lo que registre Root

Consultar la salida guardada, sin volver a abrir el COM:

```powershell
Get-Content "$Captura\root_center.log" -Tail 100
```

- Si sigue con `ready_mask=0x10 error=0x107`, **no encender Grillo**.
  Marcar `ROOT_SOLO_FALLA`, terminar con Ctrl+C en el primer terminal y conservar
  la carpeta. El siguiente diagnóstico se concentra en el anillo y firmware de
  Root, no en elegir un periférico uplink de Grillo.
- Si muestra `ready_mask=0x1f error=0x0` **y después**
  `event=boot_traffic_resumed`, registrar `GRILLO_ON` justo antes de encender
  sus cinco placas, con Ramona todavía apagada. Conectar COM7 y conservar al
  menos otros tres minutos. Anotar si aparece el hotspot de Grillo y si tiene
  Internet; su mera asociación Wi-Fi no confirma Internet.
- Si no hay datos de COM8, comprobar `connected root_center` y el COM asignado.
  Un monitor vacío no demuestra un fallo del firmware.

Marcas (ejecutar únicamente la que corresponda):

```powershell
python auxiliar/capture_links.py mark --output "$Captura" "ROOT_SOLO_FALLA: timeout de validacion sin Grillo"
# O, si Root completo su arranque, antes de encender Grillo:
python auxiliar/capture_links.py mark --output "$Captura" "GRILLO_ON: Root previamente validado"
```

Terminar con Ctrl+C en el primer terminal. No sobrescribir la captura anterior.
Si ya hay un cuarto COM identificado de Root, puede agregarse con `add-port`,
pero no hace falta conocer un enlace de Grillo para iniciar esta prueba.
