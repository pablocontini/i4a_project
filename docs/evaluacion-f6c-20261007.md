# Evaluación de la versión actual y variante F6c

Fecha: 7 de octubre de 2026.

Se comparó el worktree `comnetar-stability-tools` con las fuentes de
`C:\Users\jp\Downloads\variante-f6c\variante-f6c`. El usuario informa que la
actual navega muy lentamente, especialmente con dos hotspots cargados, y que
F6c rinde mejor pero termina reiniciando nodos. Todavía no hay medidas de
caudal comparables ni un registro del reinicio de F6c. Esta evaluación no
modifica firmware ni acredita estabilidad en hardware.

## Diferencias verificadas

| Área | Actual | F6c | Consecuencia para la evaluación |
| --- | --- | --- | --- |
| Selección de enlaces al mismo vecino | Conserva una reserva comprometida/activa; el registro no recibe RSSI | Compara calidad medida en ambos extremos; puede sustituir un enlace con 8 dB de mejora sostenida durante 15 s, esperando su retirada | La actual puede conservar un enlace débil; verificar el enlace real antes de atribuirle la lentitud |
| Retirada de gateway | `routing_table_remove_by_output` conserva la salida por defecto | La marca como `ROUTING_OUTPUT_NONE` (255); `rt_do_route` reconoce ese estado | La actual puede seguir eligiendo una salida retirada mientras se recupera; no se ha probado que esto ocurra durante las descargas |
| SPI configurado | 8 MHz, frames de 1600 bytes | 8 MHz, frames de 1600 bytes | La mejora observada no se explica por una frecuencia mayor en los `sdkconfig` comparados |
| Recepción SPI | Ocho transacciones DMA encoladas; entrega desde tarea y descarte de frames cortos | Una recepción síncrona por iteración; entrega desde ISR; cuenta frames cortos | Medir pérdidas, CRC, colas y esperas; no sustituir el driver por mera asociación con el caudal observado |
| Memoria RX lwIP | Callback libera rechazos independientemente del reporte de errores | Habilita el reporte y libera rechazos en el llamador | Ambas tienen una estrategia de liberación; no atribuir los reinicios actuales de F6c a la fuga antigua sin evidencia |
| Pila TCP/IP | 3072 bytes | 6144 bytes | Medir margen en las tareas implicadas, no aumentar todas las pilas sin diagnóstico |
| Diagnóstico | Logs generales deshabilitados; diagnósticos puntuales NM/OTA | INFO, `ring_diag`, `ring_rx_stats`, política y origen de RESET | F6c ya permite una primera captura de estabilidad sin otra recompilación |
| Ahorro Wi-Fi | `WIFI_PS_NONE` explícito | No lo desactiva explícitamente en `device_wifi_init` | F6c funciona mejor sin este ajuste; el ahorro de energía no explica por sí solo la diferencia |
| Sesión entre vecinos | Identificación `I4AN` | Identificación `I4F1` | No mezclar extremos de ambas implementaciones en un ensayo |
| Actualización | OTA por anillo y particiones dobles | No contiene `node_ota`; su configuración usa una aplicación | La imagen F6c no se debe tratar como una actualización OTA intercambiable de la actual |

La carpeta F6c tiene una discrepancia de configuración: sus defaults seleccionan
13 MHz, pero su `app/sdkconfig` selecciona 8 MHz. Un build nuevo que regenere la
configuración puede diferir. Espressif documenta el SPI slave del ESP32 para
hasta 10 MHz; mantener 8 MHz para la comparación y revisar señales/CRC antes
de aumentar frecuencia. Los 8 Mbit/s son el reloj bruto de cada enlace SPI,
no una promesa de caudal IP de la red.

Referencia: [SPI Slave ESP-IDF 5.4.1](https://docs.espressif.com/projects/esp-idf/en/v5.4.1/esp32/api-reference/peripherals/spi_slave.html).

F6c declara `PROJECT_VER=ab-f6c-2b26b28`, mientras su
`comparison-manifest.json` conserva la identificación F4. Sus notas también
indican un SSID que difiere del definido actualmente en `node.c`. Para
identificar cada ensayo usar la imagen, su configuración y el ELF correspondiente;
el nombre de la carpeta o el manifiesto heredado no bastan. La versión actual
fue compilada con ESP-IDF 5.4.1; las notas F6c declaran compilación con 5.5.1.
No se verificó una compilación nueva de F6c en esta evaluación.

## Comprobaciones ejecutadas

- Se ejecutó el código C real de las tablas de ambas versiones con el mismo
  escenario: instalar gateway 3, conservar una ruta específica por 1 y retirar
  salida 3. La actual siguió devolviendo 3 para internet; F6c devolvió 255.
  Ambas conservaron la ruta específica por 1. Esto prueba la diferencia de
  tabla, no todo el recorrido de los paquetes en lwIP.
- Se ejecutó con TinyCC el harness C de política RSSI de F6c: selección,
  histéresis, espera de liberación, muestras antiguas y recuperación con
  reportes frescos aprobados. No se ejecutó su batería completa ni sanitizers.
- Las seis pruebas nativas de recepción de la actual aprobaron con ambos
  modos de reporte de errores. No miden caudal ni reproducen reinicios.
- Los resultados de las sondas se guardaron en
  `app/build/evaluation-f6c/results.txt` (artefacto local de build).

## Secuencia recomendada

### 1. Capturar el primer reinicio de F6c

Conservar F6c como referencia del rendimiento que el usuario ya observó.
Registrar reposo y luego carga desde los dos hotspots, con las mismas fuentes,
placas, posición, clientes y alimentación. Guardar consola del central afectado
y, si se dispone de otro puerto, de su direccional de salida. Conservar varios
intervalos de estadísticas antes de la falla y el arranque posterior.

F6c ya registra `RESET TX/RX source_mac`, orientación, motivo y uptime. Una
placa que reinicia puede provocar reinicios coordinados de sus hermanas;
hay que localizar la primera falla, no confundir todos los reinicios con
excepciones independientes. Si el central sólo recibe una orden RESET,
seguir la orientación indicada y capturar su consola.

Guardar `Guru Meditation`, `Backtrace`, desbordamiento de pila, watchdog,
brownout y el motivo del nuevo arranque. El mensaje de overflow documentado en
`LEEME-F1.md` pertenece a una versión anterior: F6c ya incorpora ese arreglo.
No demuestra el motivo de los reinicios que se observan ahora.

### 2. Comparar caudal local y ruta usada

Usar la misma descarga HTTP desde `10.255.255.254`, con tamaño conocido, y
guardar bytes realmente recibidos, tiempo y error de curl. Hacer tres
condiciones por versión: sólo cliente A descargando, sólo B, ambos a la vez
desde dos clientes. Mantener los tres nodos encendidos para conservar la
topología; separar después el ensayo de apagar un nodo o retirar un enlace.

Registrar RSSI de ambos extremos y el gateway/direccional utilizado por cada
nodo, cantidad de saltos, canales, latencia y pérdida durante cada condición.
La telemetría de hace cinco minutos no identifica necesariamente la salida
actual. La configuración de canales sitúa los centrales en canal 11, por lo
que también merece medirse la contención radio bajo carga concurrente.

Observar `ring_diag`: CRC incorrectos, frames cortos, descartes, retornos fallidos
al pool, tiempo de TX, timeout del mutex y profundidad de colas. Observar
`ring_rx_stats`: rechazos, errores de memoria, fallos de asignación, memoria
libre/mínima y mayor bloque libre. Registrar mínimos de pila de las tareas
de transporte, TCP/IP, callbacks y control; `ring_diag.stack_min` mide sólo
su propia tarea, no todas las tareas del nodo.

### 3. Resolver la estabilidad según la evidencia

- Brownout o pérdida de alimentación: medir tensión bajo carga y revisar la
  fuente, cable y distribución de energía del nodo.
- Overflow/panic: identificar tarea y llamada con el ELF de esa imagen;
  revisar pila, vida de interfaces y tareas antes de ajustar tamaños.
- RESET coordinado: localizar el emisor original y su motivo; conservar la
  coordinación hasta entender el evento que la inicia.
- Caída progresiva de memoria o errores de asignación: estudiar buffers,
  sockets y tareas con los contadores y su recuperación tras dejar de cargar.
- Sin reinicio, con errores SPI o esperas crecientes: abordar el transporte
  y la presión de colas. Ninguna de las dos versiones usa una señal GPIO
  de receptor listo; encolar buffers no garantiza que nunca se agoten.
  Considerar sincronización/control de flujo si las medidas muestran esa
  pérdida, con una variante de protocolo explícita.

### 4. Portar mejoras por separado y volver a medir

Mantener la actual como base de integración para preservar OTA y las
correcciones de DTR y memoria. Aprovechar F6c como referencia y fuente de
mejoras verificables, en este orden:

1. Diagnósticos agregados de transporte, memoria, pila y origen de reset,
   con frecuencia acotada y el mismo nivel de diagnóstico en los ensayos.
2. Invalidación de la salida por defecto retirada y manejo coherente de
   ausencia de ruta. Auditar también el fallback de lwIP al devolver NULL
   desde el hook: cambiar sólo el valor de la tabla no garantiza descarte.
3. Selección de enlaces por calidad con histéresis, conservando un único
   enlace admitido por vecino y confirmando retirada antes de reemplazo.
4. Ajustes de colas, reparto de buffers SPI, afinidad y tráfico de control,
   elegidos por los contadores del ensayo.

F6c conserva diferencias en el cálculo de DTR que la actual ya corrigió.
No copiar todos sus callbacks de ruteo como parte de una optimización de radio.
Tampoco importar juntas la política de enlaces, el driver SPI y una versión
distinta de ESP-IDF: impediría atribuir la mejora o la regresión a un cambio.

Primera aceptación: transferencias completas en ambos clientes, caudal agregado
y por cliente registrado, y ausencia de reinicios durante una hora bajo carga.
Después prolongar el ensayo por encima del tiempo habitual hasta la falla
(como mínimo el doble una vez conocido) y repetir transiciones de enlaces.
Una hora sin reinicios no acredita estabilidad prolongada.
