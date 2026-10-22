# Portal de configuración sobre el firmware de estabilidad

La integración está en `feat/config-portal-stability`, creada desde `ad32317`
de `fix/comnetar-stability-tools`. Recupera el portal, las credenciales y el modo
de orientación de `feat/redundant-link-discovery` (`81aa146`). Conserva las
correcciones de SPI, DMA, buffers, coordinación y recuperación de rutas del
firmware de estabilidad. No incorpora el administrador de descubrimiento de
esa otra rama.

El firmware instalado que se consultó en COM5 no incluía el monitor del botón
BOOT ni el portal. BOOT necesita ese código para solicitar un reinicio al modo
de configuración durante la ejecución.

## Uso del botón y del portal

En el central de Grillo o Ramona, con el arranque del anillo completo:

1. Presionar BOOT durante aproximadamente un segundo y soltarlo. El filtro de
   rebote exige 150 ms; la solicitud se procesa al soltar.
2. Esperar el reinicio coordinado y conectarse a `ComNetAR_Config`.
3. Abrir `http://192.168.4.1/`. La pantalla principal permite cambiar las
   credenciales de la red doméstica; `/admin` permite configurar potencias,
   umbrales RSSI, administración y modo de orientación.
4. Guardar y aplicar para reiniciar en modo normal. Otra pulsación corta de
   BOOT sale del portal. También sale automáticamente a los cinco minutos.
5. Reconectar la PC a la red doméstica del nodo y comprobar acceso al gateway,
   a la Raspberry y a Internet.

Una pulsación sostenida de seis segundos restablece las credenciales Wi-Fi
domésticas y reinicia al soltar. Conserva las potencias, los umbrales RSSI y las
credenciales administrativas. Para entrar al portal, usar una pulsación corta.

El central de Root conserva su comportamiento anterior: no se habilita allí
el monitor de BOOT para el portal doméstico. El modo de orientación, iniciado
desde la administración, usa `Orientacion_Antenas` y `192.168.50.1`.

Durante el arranque y el mantenimiento OTA se ignoran las pulsaciones para
evitar un reinicio en medio de una actualización. En configuración u orientación
se permite consultar `--status`, pero se rechaza iniciar una carga OTA. Para
actualizar, volver primero al modo normal.

## Compatibilidad y paquetes compilados

STARTUP pasa a versión 2 para transmitir modos, cuatro potencias y cuatro
umbrales RSSI. Al instalar esta rama hay que actualizar las cinco placas de
cada nodo: no mezclar versiones de STARTUP dentro del anillo. Los valores por
defecto mantienen el umbral en -128 dBm y la potencia en 80 cuartos de dBm.

Paquetes locales compilados, con diagnósticos habilitados:

| Nodo | Carpeta bajo `app/build/link-capture/` | Identidad ELF que informa `--status` |
| --- | --- | --- |
| Grillo | `grillo-config-20261022` | `9fc0b426486968e1dda506695d854d8959b0cd8b81b7facfa16e986f820d4352` |
| Root / Ramona | `ramona-config-20261022` | `9054ee70eb3a4c38eac75e4007f418f09256bce603e0af1ad47522f8f1b44288` |

Root se distingue por su configuración física; comparte el paquete de Ramona.
Cada paquete contiene los cuatro segmentos, el manifiesto de hashes, el ELF y
un archivo de fuentes. Los binarios y las capturas son archivos locales bajo
`app/build`, no se incluyen en el commit.

Para reproducir la compilación, usar carpetas de salida nuevas y ejecutar las
variantes sucesivamente, porque comparten el directorio de compilación:

```powershell
python auxiliar/build_link_capture.py --variant grillo --output app/build/link-capture/grillo-config-nuevo
python auxiliar/build_link_capture.py --variant ramona --output app/build/link-capture/ramona-config-nuevo
```

## Validación y prueba pendiente en placas

Pasaron 120 casos nativos y 54 pruebas Python, incluida la compatibilidad del
protocolo OTA entre C y Python. Se comprobó la compilación de las dos variantes.
Los casos nuevos cubren rebotes y pulsaciones de BOOT, bloqueo de cambios de
modo durante mantenimiento, rechazo de BEGIN en modos especiales, lectura de
estado, STARTUP, persistencia de ajustes y filtros RSSI. Se mantienen las pruebas
de recuperación de rutas, Wi-Fi y transporte del firmware de estabilidad.

Esta integración no se flasheó durante su preparación. Las doce horas sin
reinicios corresponden al firmware anterior y no validan todavía el portal.
Antes de extenderla a los tres nodos, probar un nodo completo con captura:
consultar los cinco estados, entrar al portal con BOOT, salir, comprobar el
retorno de Internet y repetir guardando un ajuste. Confirmar además la
persistencia de los cuatro ajustes de radio y el retorno desde orientación.
Registrar los reinicios solicitados por BOOT para distinguirlos de fallas.

La rama `fix/comnetar-stability-tools` se conserva como referencia de estabilidad.
