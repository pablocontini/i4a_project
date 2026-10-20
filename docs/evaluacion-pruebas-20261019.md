# Evaluación de las pruebas y cambios propuestos, 19/10/2026

La revisión posterior adopta pérdida SPI como condición de operación y corrige
provisión y respuesta de recuperación que no toleraban un anuncio perdido:
[tolerancia-perdidas-spi-20261019.md](tolerancia-perdidas-spi-20261019.md).
Las imágenes y validaciones siguientes corresponden a la revisión de integridad
anterior; no incluyen esos reintentos nuevos.

Las capturas confirman pérdidas intermitentes incluso con Ramona apagada.
En las placas monitoreadas no se reprodujo la caída permanente de Internet
ni un reinicio durante estas tres pruebas. Corresponde corregir los accesos
a memoria sin límites encontrados en la revisión y mejorar la medición antes
de modificar la elección de enlaces. Esos defectos todavía no están
demostrados como causa de las pérdidas observadas.

Los cambios nuevos de esta revisión no están instalados en los ESP. Pasaron
54 pruebas Python y 100 casos nativos; las variantes Grillo y Root/Ramona
compilaron con ESP-IDF 5.4.1. Esto no demuestra que esté resuelta la pérdida
de conectividad: falta comprobar las imágenes nuevas en las placas.

## Referencias conservadas

Las tres carpetas están en `app/build/capturas/`. Se analizaron sus archivos
`timeline.jsonl`, los UART y los siguientes resúmenes:

| Captura | Estado y alcance | Archivo de análisis |
| --- | --- | --- |
| `root-grillo-dma-20261019-101607-281` | Root y Grillo actualizados; Ramona apagada; 180,7 s y tres UART | `analisis-referencia-dma.json` |
| `grillo-este-20261019-102250-495` | Misma topología; Este de Grillo agregado en t=59,437 s | `analisis-este.json` |
| `ramona-controlada-20261019-103102-194` | Ramona apagada al inicio, encendida durante la captura; cuatro UART | `comparacion-encendido-ramona.json` |

Root ejecutaba la variante DMA `a965f3c7…`; Grillo, `52a3f320…`.
La primera captura terminó con `summary.json` y `failures=[]`; la segunda
también. La tercera finalizó sin `summary.json`: se conserva
`nota-cierre.json` y se pudo leer la línea de tiempo completa. No debe
presentarse ese cierre como una captura finalizada sin errores.

Los pings de estas ventanas se enviaron desde Wi-Fi 2, origen `10.116.0.2`,
hacia gateway `10.116.0.1`, RPi `10.255.255.254` e Internet `1.1.1.1`, con
espera de 800 ms. Una falta de respuesta dentro de ese plazo no distingue
por sí sola un descarte definitivo de una respuesta tardía.

| Ventana | Gateway: perdidos/intentos | RPi: perdidos/intentos | Internet: perdidos/intentos |
| --- | --- | --- | --- |
| Referencia DMA, 180,7 s, Ramona apagada | 3/180 | 25/179 | 23/179 |
| Segunda captura, desde conexión de Este de Grillo | 5/120 | 30/120 | 47/120 |
| Controlada, 120 s antes de la marca previa al encendido | 8/118 | 25/119 | 32/119 |
| Controlada, 120 s desde confirmación del usuario | 15/120 | 22/119 | 45/119 |

Son ventanas diferentes, sin repeticiones suficientes para estimar una
variación estable de la tasa de pérdida. Tampoco se debe tratar la segunda
ventana de la captura controlada como dos minutos con Ramona ya integrada.

## El encendido y la incorporación de Ramona ocurrieron en momentos distintos

En `ramona-controlada-20261019-103102-194`, el tiempo relativo de la captura
permite ordenar los hechos. Las horas de esta tabla son UTC; Argentina es
UTC−3. Las marcas del usuario indican acciones o confirmaciones, no mediciones
eléctricas ni el instante de asociación de las cinco placas.

| Hecho registrado | Tiempo relativo | Hora UTC |
| --- | --- | --- |
| `ANTES_DE_ENCENDER_RAMONA`, recepción de marca | 121,187 s | 13:33:08.312 |
| `USUARIO_CONFIRMA_RAMONA_ENCENDIDA`, recepción de marca | 152,797 s | 13:33:39.915 |
| Primera ruta de Ramona en central de Grillo | 271,781 s | 13:35:38.900 |
| Primera ruta de Ramona en Este de Grillo | 271,890 s | 13:35:39.017 |
| Primera ruta de Ramona en central de Root | 273,203 s | 13:35:40.322 |
| Primera ruta de Ramona en Sur de Root | 273,328 s | 13:35:40.450 |
| Último evento conservado | 282,547 s | 13:35:49.664 |

Las horas de las rutas son las de recepción UART, no relojes sincronizados
entre los ESP. Indican cuándo se observó la ruta; la incorporación efectiva
pudo preceder a la siguiente muestra de diagnóstico.

La ventana publicada como «después» va de 152,797 a 272,797 s. Casi toda
precede a la primera ruta de Ramona. Entre la confirmación y esa primera
ruta se registraron 44 pérdidas de Internet. Desde la primera ruta hasta el
final solo quedaron unos 10,8 s: gateway 11/11 respuestas, RPi 9/11 e Internet
7/11. No hay dos minutos de observación con la topología nueva estabilizada.
El aumento de pérdidas no puede atribuirse al cambio de rutas con estos datos.

Root mantuvo `default=5`; Grillo, `default=3` (Este), incluso después de
aparecer Ramona. Los bloques son coherentes: Grillo `10.96.0.0/11` incluye
la red de clientes `10.116.0.0/14`; Ramona usa `10.64.0.0/11`. Son dos
agregados distintos. Las tablas agregaron rutas específicas a Ramona sin
reemplazar la salida hacia RPi o Internet.

## Qué permiten concluir los CRC

Orden físico del anillo, informado por el usuario el 11/10/2026 y confirmado
para el tramo de entrada al central el 12/10/2026:

`Central → Sur → Oeste → Norte → Este → Central`.

La entrada SPI del central recibe de **Este**; su salida va a **Sur**. En
Grillo ambos extremos de la recepción que se investiga ya están disponibles
por USB: Este en COM17 y central en COM7. No es necesario volver a pedir
identificación del cableado para este tramo.

En la referencia DMA aumentaron tres CRC en central de Root, tres en Sur
de Root y seis en central de Grillo. Durante la ventana de la segunda captura
con los cuatro monitores, central de Grillo aumentó nueve; los otros tres
monitores no registraron incrementos CRC. Este de Grillo mantuvo asociación
con Root, RSSI próximo a −24 dBm y su salida configurada.

No aumentaron `short`, `no_buffer`, `queue_full`, `local_full`, `forward_fail`
ni `tx_fail` en esas ventanas. Esto solo describe las placas monitoreadas:
central y Sur de Root, central y Este de Grillo. No hay cobertura de todos
los receptores del anillo ni de los cinco ESP de Ramona. Un `tx_fail=0`
tampoco demuestra recepción correcta en el siguiente ESP.

El contador CRC existente reúne distintos tipos de trama y se consulta cada
dos segundos. No permite identificar un ping concreto ni separar corrupción
de control, IP u otros datos. Las pérdidas de pings también aparecen fuera
de los intervalos de incremento CRC; no está demostrado que esos CRC
expliquen todas las pérdidas, ni que su causa sea exclusivamente física.

## Cambios implementados

La revisión encontró dos accesos sin límite útiles de corregir por sí mismos:

- `components/integration/callbacks/callbacks.c`: la recepción de siblings
  copiaba `len` bytes en `message_t.data[512]` sin comprobar puntero ni
  longitud. Además, `rs_broadcast` permite 512 bytes de datos y antepone el
  byte de componente: un mensaje válido puede ocupar **513 bytes**. Antes,
  ese último byte se sobrescribía al escribir la longitud. Se amplió el
  almacenamiento a 513 y se agregó una guarda que rechaza nulos, vacíos y
  longitudes mayores; el límite de peers permanece en 512. La regresión
  comprueba rechazo de 514 bytes y copia íntegra del máximo válido de 513.
  Longitudes aún mayores podían sobrescribir memoria de pila; las capturas
  no prueban que haya ocurrido durante el fallo de Internet.
- `components/internal/ring_link_netif/ring_link_netif.c`: un descarte de
  paquetes imprimía datos binarios con `%s`. Ese formato puede leer fuera
  de la trama al buscar un terminador. Se reemplazó por un registro limitado a
  metadatos, sin recorrer el contenido como cadena.

Para dirigir el análisis siguiente se agregó un contador CRC sobre la copia
de recepción, antes de rearmar DMA (`rx_crc_checked`, `rx_crc_error`), y se
clasificaron los descartes posteriores
del anillo (`crc_internal`, `crc_ip`, `crc_other`). Compararlos permite saber
si una trama ya era inválida en la primera observación del driver. No identifica
por sí solo la causa eléctrica o temporal. Son contadores de diagnóstico;
no cambian el formato de la trama ni la elección de rutas. Se habilitan con
`CONFIG_I4A_CAPTURE_DIAGNOSTICS`; la clasificación usa el tipo observado,
que podría estar alterado, y no garantiza que la trama fuera IP o de control.
`crc_at_copy` en el log corresponde a `rx_crc_error`. Los contadores se leen
en instantes próximos, por lo que una diferencia puntual también puede ser
una trama pendiente de procesar o un descarte en la cola; comparar ventanas
completas, no inferir corrupción de memoria por una sola diferencia.

La herramienta de captura incorpora cierre solicitado desde otra terminal
(`stop`) y un tiempo de espera de ping configurable y guardado en la sesión.
El primero permite liberar los COM y generar el resumen sin depender de
Ctrl+C en una consola concreta; el segundo permite repetir con 800 y 3000 ms
sin mezclar mediciones. Aumentar la espera no constituye una corrección de
la conectividad. Ambos cambios están cubiertos por pruebas de la herramienta.

Para cerrar una captura iniciada con esta versión:

```powershell
$Captura = (Get-Content app/build/captura-activa.txt -Raw).Trim()
python auxiliar/capture_links.py stop --output "$Captura"
```

El comando espera confirmación en `summary.json`; si el proceso murió antes,
informa el fallo y no declara un cierre correcto. No permite cerrar mediante
este mecanismo una captura anterior que no soporte solicitudes de parada.
`start --ping-timeout-ms 3000` modifica y registra el límite de respuesta;
el valor predeterminado sigue siendo 800 ms para conservar comparabilidad.

No hay evidencia suficiente para aumentar las colas, cambiar la frecuencia
SPI, modificar métricas o mover gateways. Esos cambios dificultarían comparar
la siguiente prueba y podrían ocultar el problema sin identificarlo.

## Riesgos de recuperación pendientes de reproducir

La inspección detectó dos condiciones distintas de las pérdidas actuales.
No se implementan en esta revisión sin una reproducción y una prueba específica:

- En `forwarder/peer.c`, la primera provisión de siblings cambia el estado
  local antes de un `rs_broadcast` cuyo resultado se ignora. Si falla esa
  publicación, los handshakes siguientes ya ven el dispositivo provisionado.
  Comprobar con fallo inyectado de esa única publicación si los siblings
  recuperan su provisión sin reiniciar; definir después un reintento idempotente.
- En `neighbor_manager.c`, una revisión de snapshot solo se acepta si supera
  la conocida. Si reinicia únicamente el central, su revisión vuelve a cero
  y los periféricos podrían conservar una mayor. Reproducir ese reinicio
  parcial antes de diseñar una época de arranque que distinga snapshots viejos.

## Próxima comprobación

Se terminaron las regresiones y la compilación. No se ha flasheado ningún ESP
como parte de esta evaluación. Se conservaron los paquetes anteriores.

| Destino | Paquete nuevo bajo `app/build/link-capture/` | ELF SHA-256 |
| --- | --- | --- |
| Las cinco placas de Grillo | `grillo-integridad-20261019/` | `176be4fb9ba2ecfb565023283357c91affda09687ddfe8441fa26a171ec3e146` |
| Las cinco placas de Root o Ramona | `ramona-integridad-20261019/` | `9e8de05e34416e9b121a32a375349b4b9a6fe71e86a25bc980308362ebefd265` |

Ambas aplicaciones ocupan 808192 bytes. Se comprobaron los hashes de los
cuatro segmentos exportados y que el ZIP de fuentes contiene el código
compilado. Ambos paquetes conservan el mismo ZIP de fuentes, SHA-256
`1b1d95d9296b8ba19b15d513c00d1d25bee3868e52ea8214f52c9f6ed7318fc2`.

Las 100 pruebas nativas incluyen 24 de vecinos/SPI/callbacks, 6 de ingreso,
10 de netif, 18 de routing, 20 de OTA, 5 de coordinación, 6 de broadcast,
6 de ciclo Wi-Fi y 5 de hooks de routing. También pasó compatibilidad del
protocolo C/Python. La nueva instrumentación CRC añade un cálculo por copia
recibida en las variantes de diagnóstico; su efecto temporal debe medirse
en hardware. No se cambió la frecuencia SPI ni el formato de las tramas.

Conviene probar primero el paquete nuevo en Grillo completo, manteniendo
Root con la versión conocida y Ramona apagada durante esa comparación.
Así se verifica una modificación por nodo; no hace falta actualizar los
tres nodos simultáneamente para evaluar la mejora. Conservar el paquete DMA
anterior para poder comparar las mismas condiciones.

En una nueva comparación, conservar los mismos monitores y Wi-Fi 2, marcar
el encendido y esperar a observar incorporación efectiva en las tablas.
Medir una referencia estable y al menos dos minutos después de esa
incorporación, con igual timeout y frecuencia de pings. Conservar la carpeta
completa y comprobar el cierre y `summary.json`. Si persisten CRC, la nueva
separación de contadores debe orientar qué receptor conviene monitorear,
sin atribuirle aún todas las pérdidas de Internet.
