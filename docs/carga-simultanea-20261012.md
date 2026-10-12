# Primera medición de carga simultánea — 12 de octubre de 2026

Servidor RPi: python3 -m http.server 8765 --bind 10.255.255.254,
archivo carga.bin de 8 MiB sin compresión. No es una prueba de internet,
HTTPS, navegación ni subida; mide descarga TCP/HTTP dentro de la malla.
Intervalos de 20 s, archivos interrumpidos al finalizar deliberadamente.
completed_downloads=0 no significa fallo. Se cuentan bytes del cuerpo HTTP.

Clientes: Wi-Fi 3 Ramona 10.116.0.2/ifIndex25; Wi-Fi 2 Grillo
10.148.0.4/ifIndex6. El nuevo adaptador se asociaba a Ramona sin recibir
DHCP (169.254); recibió IP al conectarse a Grillo. Al intercambiar adaptadores,
ambos obtuvieron IP válida. Causa de la combinación Wi-Fi2–Ramona pendiente.
Los dos hotspots usan canal 11: posible contención, no causa probada.

Herramienta gateway/measure_dual_wifi.py: bind a IP local y selección explícita
Winsock IP_UNICAST_IF por índice, sin proxy ni resolución DNS. Referencia:
https://learn.microsoft.com/en-us/windows/win32/winsock/ipproto-ip-socket-options
No asumir que el bind a IP por sí solo fuerza el adaptador en todas las plataformas.
La herramienta actual está preparada para Windows y estas IP/índices concretos:
actualizarlos si cambia la provisión. No modifica rutas del sistema.

| Intervalo | Ramona Mbit/s | Grillo Mbit/s |
| --- | --- | --- |
| Ramona sola | 1,635 | — |
| Grillo solo | — | 1,985 |
| Ambos, primera medición | 1,115 | 1,014 |
| Ambos, repetición | 1,261 | 0,776 |

Individuales sin errores. La primera simultánea anotó timeouts justo al
vencer el intervalo; se corrigió su clasificación: un timeout al agotarse
el plazo no es una falla de red. La repetición simultánea no registró errores.
Los números iniciales de throughput siguen siendo mediciones de bytes/tiempo;
no interpretar sus marcas de timeout como una reproducción de la falla.

Snapshots de tablas estables durante los tres intervalos iniciales. Consolas
Ramona/Grillo/Root: dual-load-{nodo}.raw, resultados dual-load-results.json,
repetición dual-load-repeat.json, preflight dualwifi-preflight2.json,
todo bajo build/firmware. Primera preflight Ramona requirió un reintento;
la segunda preflight ambos respondió sin errores. No captura pcap RPi
simultánea ni pings periódicos en esta medición; no afirmar pérdida ICMP cero.

Resultado: reparto de capacidad (~2,04–2,13 Mbit/s agregados con ambos)
sin caída completa reproducida en estos intervalos cortos. No basta para
explicar la navegación que se bloquea con los dos nodos cargados.
Siguiente: carga más prolongada, medir directamente RPi/internet, contrastar
tráfico HTTP/HTTPS, pérdidas y retransmisiones. No cambiar firmware ni
canales antes de establecer referencias comparables. Mantener servidor
RPi abierto mientras se realizan estas pruebas; detenerlo después con Ctrl+C.
## Carga simultánea prolongada, 10:25:07–10:28:07 (UTC-3)

Se descargó de RPi durante 180 segundos por ambos adaptadores fijados:
Ramona 32578536 bytes, 1,448 Mbit/s, tres archivos completos; Grillo
19997976 bytes, 0,889 Mbit/s, dos archivos completos. Sin errores de
aplicación registrados; final del intervalo cancela la transferencia restante.
Ambas IP conservadas. Snapshots durante la carga sin cambios observados.
No se reprodujo caída completa en esta prueba TCP/HTTP local, ni se prueba
que no hubo retransmisiones. PCAP en RPi ~/carga_simultanea_larga_20261012.pcap
pendiente de recibir y analizar. Servidor HTTP permanece abierto.
Evidencia local dual-long-results.json, muestras cada ~5 s
 dual-long-results-both-{ramona,grillo}-samples.jsonl y
 dual-long-{ramona,grillo,root}.raw bajo build/firmware.
## Análisis del PCAP recibido

Archivo: Downloads/carga_simultanea_larga_20261012.pcap, 57008600 bytes.
Análisis reproducible: gateway/analyze_load_pcap.py; resultado
build/firmware/dual-long-pcap-analysis.json. Captura Ethernet clásica.
Se filtraron conexiones con más de 100000 bytes de payload para excluir
la preflight pequeña; se midieron segmentos enviados desde RPi al cliente.

| Cliente | Segmentos con payload | Repetidos exactos | Porcentaje de segmentos | Bytes repetidos | Ventanas cero |
| --- | --- | --- | --- | --- | --- |
| Ramona 10.116.0.2 | 22553 | 223 | ~0,99 % | 325580 | 0 |
| Grillo 10.148.0.4 | 13923 | 181 | ~1,30 % | 264260 | 0 |

Repetición exacta = mismo flujo TCP, secuencia, longitud y CRC del contenido.
Son candidatos a retransmisión; no clasificación Wireshark. No se descarta
duplicación de captura y no se cuentan retransmisiones resegmentadas con
solapamiento parcial. ACK consecutivos con mismo número/ventana: 1571 en
Ramona, 1880 en Grillo; no todos equivalen a pérdida (posible reordenamiento).
Ventana raw mínima 255, sin aplicar escala: no convertir a bytes efectivos.
Ningún RST en los siete flujos de descarga considerados.

Mayor intervalo entre segmentos de payload nuevos observados saliendo de
RPi: Ramona 1,596 s, Grillo 1,603 s. Esto no mide directamente cuándo llegaron
al cliente. Los 37 registros de aplicación por nodo muestran avance en todos
los intervalos; máxima separación de muestras 5,625/6 s. No bloqueo sostenido
reproducido ni cierre por ventana cero en esta prueba de un flujo por nodo.

Siguiente prueba útil: varias conexiones paralelas por nodo o navegación
HTTPS real, con referencia directa de internet en RPi y captura TCP. La
capacidad compartida y las retransmisiones observadas por sí solas no prueban
la causa del fallo descrito por el usuario. No modificar canales/firmware
antes de comparar carga equivalente y verificar si se reproducen errores.
## Cuatro conexiones por nodo, 10:38–10:39 (UTC-3)

Se extendió measure_dual_wifi.py con --connections 1..8, archivos de
muestras separados por worker y resumen agregado por nodo. Referencia
HTTP pequeña desde ambas interfaces aprobada sin errores. Cada fase 20 s.

| Fase | Ramona Mbit/s agregados | Grillo Mbit/s agregados | Errores |
| --- | --- | --- | --- |
| Ramona sola, 4 TCP | 2,800 | — | 0 |
| Grillo solo, 4 TCP | — | 1,606 | 0 |
| Ambos, 4 TCP cada uno | 0,448 | 0,370 | 1 timeout por nodo |

Todos los workers recibieron datos. Timeouts reales antes del plazo:
Ramona worker2 10:39:04.490876, Grillo worker3 10:39:04.424566.
Snapshots de tablas permanecieron estables durante este ensayo. No captura
PCAP nueva de estas ocho conexiones; no inferir retransmisiones del PCAP
anterior de solo dos conexiones. No pings periódicos en esta prueba.
Resultado: degradación y pausas bajo carga múltiple reproducidas; no caída
completa ni causa establecida. Falta distinguir congestión/capacidad de
cola/SPI/Wi-Fi/servidor HTTP de cambios de sesión. Captura próxima debe
incluir SYN, ACK, retransmisiones, ventanas y reinicios de conexión.
Evidencia multistream-results.json, multistream-*.raw y muestras por worker
bajo build/firmware. Límite de inactividad del medidor: 3 s, registrado como
timeout antes de finalizar intervalo; puede ser pausa transitoria y no
pérdida permanente. El medidor todavía no registra la etapa del timeout.
## Repetición de ocho TCP con PCAP, 10:42:58–10:44:28

90 s, cuatro conexiones por nodo. Ramona 17763820 bytes, 1,579 Mbit/s,
sin errores; Grillo 8041515 bytes, 0,715 Mbit/s, un timeout en response_body
(worker3, hora PC 10:43:41.415941). Todos los workers avanzaron. Tablas
estables en consola. No se aplicaron cortes ni cambios de firmware.

PCAP recibido: Downloads/carga_8_conexiones_20261012.pcap, 28996679 bytes.
Resultado build/firmware/multi8-pcap-analysis.json. Descargas consideradas:
Ramona 4 flujos, 12574 segmentos payload, 302 repetidos exactos (~2,40 %);
Grillo 5 flujos por reconexión, 5893 segmentos payload, 319 repetidos
(~5,41 %). Método de candidatos exactos con las mismas limitaciones anteriores.
Las cifras son porcentajes de segmentos capturados, no pérdida radio medida.

El parser inicialmente contaba la ventana cero de paquetes RST como si
fuera una ventana de recepción. Se corrigió para excluir RST de esa medida:
no hubo ventanas cero en ACK válidos considerados. Los cierres RST desde
clientes al terminar intervalos son esperados, no prueba de fallo del ESP.

Grillo puerto52774: mayor pausa de nuevos segmentos RPi 10:43:38.820571–
10:43:41.908182, 3,088 s; RST cliente 10:43:42.878330 y posterior flujo
puerto60089. Es consistente con el timeout de 3 s del medidor; las horas
PC y RPi tienen desfase y no deben equipararse exactamente. Otro flujo
puerto52773 tuvo pausa 3,421 s a 10:43:53.474857–10:43:56.896148.
Ramona mayor pausa 1,072 s. No se observó bloqueo general persistente.

Conclusión acotada: con múltiples flujos hay más repeticiones y pausas
largas, especialmente por Grillo; el medidor cierra y reabre al superar
3 s de inactividad. No se ha localizado si la causa está en SPI, Wi-Fi,
colas, host cliente o servidor. Un único PCAP en RPi no identifica el
salto donde se pierde un paquete. Siguiente control: mismo ensayo con
límite de inactividad 10 s, manteniendo captura y carga, para separar
pausas recuperables del efecto de reconexión del medidor. Después comparar
contadores/colas y, si es necesario, capturas adicionales por tramo.
Evidencia local multi8-results.json, muestras por worker y multi8-*.raw.
## Control con timeout de 10 s, 10:54:29–10:55:59

Ocho conexiones, 90 segundos, mismo servidor y nodos. Ramona 17324230
bytes, 1,539 Mbit/s; Grillo 5313815 bytes, 0,472 Mbit/s. Cero errores de
aplicación y tablas estables. El rendimiento de Grillo sigue bajo; alargar
el timeout no demuestra solución de la lentitud ni comparación causal con
una sola repetición. PCAP timeout10 pendiente de recibir y analizar para
comprobar pausas y retransmisiones. Evidencia multi8-t10-results.json,
muestras por worker y multi8-t10-{ramona,grillo,root}.raw en build/firmware.
No cambios de firmware ni de canal durante el ensayo.
## PCAP con timeout10 analizado

Mismo método de repetidos exactos, ACK sin RST para ventanas. Ramona:
12313 segmentos, 372 repetidos (~3,02 %), pausa máxima de nuevos datos
saliendo de RPi 1,648 s. Grillo: 4212 segmentos, 513 repetidos (~12,18 %),
pausa máxima 4,722 s. Cuatro flujos de descarga por nodo, sin reconexión
por timeout y sin ventanas cero válidas. RST únicamente al final del plazo,
coherentes con cierre deliberado del medidor. No equivalen a resets del nodo.

Comparación limitada con timeout3: Grillo pasó de ~5,41 % repetidos y pausa
3,42 s a ~12,18 % y 4,72 s; velocidad de 0,715 a 0,472 Mbit/s. No afirmar
que cambiar timeout causó el aumento: son dos realizaciones de una red variable.
La pausa larga se recuperó manteniendo TCP abierto, sin error de aplicación.
El timeout corto amplificaba una pausa recuperable mediante reconexión;
no explica por sí solo la menor capacidad ni identifica dónde se pierden datos.

Consolas no muestran eventos de fallo SPI seleccionados por búsqueda; esos
contadores IP son principalmente ICMP y no bastan para descartar pérdidas
TCP en SPI/colas. No se localizó aún el salto. Siguiente control debe comparar
carga múltiple de Grillo solo y ambos con PCAP, manteniendo timeout10, y/o
capturar ambos lados del tramo sospechoso para contar pérdidas reales.
Evidencia multi8-t10-pcap-analysis.json y PCAP en Downloads. Análisis y
limitaciones anteriores aplican: segmentos repetidos no son porcentaje
medido de pérdida radio ni clasificación completa Wireshark.
## Control consecutivo Grillo solo y ambos, 11:05:34–11:07:37

Cuatro TCP por nodo, timeout10, fases60 s. Grillo solo:14310080 bytes,
1,907 Mbit/s, sin errores. Ambos:Ramona15829940 bytes/2,107 Mbit/s;
Grillo4992870 bytes/0,665 Mbit/s, sin errores. Grillo redujo su velocidad
aproximadamente65 % al sumar Ramona en este ensayo. Tablas observadas estables.
Esto vincula la degradación a carga concurrente en estas condiciones,
pero no localiza la pérdida entre SPI, radio, cola, servidor o host cliente.
PCAP grillo_solo_vs_ambos_20261012.pcap en RPi pendiente de comparar por fase.
Evidencia grillo-control-results.json, muestras y consolas bajo build/firmware.
## PCAP del control consecutivo analizado

Grillo solo:10263 segmentos de descarga,402 repetidos exactos (~3,92 %),
pausa máxima1,074 s. Grillo con Ramona:3721 segmentos,236 repetidos
(~6,34 %), pausa máxima6,281 s (otro flujo5,970 s). Ramona simultánea:
11127 segmentos,239 repetidos (~2,15 %), pausa máxima0,490 s.
Sin ventanas cero válidas. Flujos separados por fase a partir de tiempos
SYN: Grillo solo11:05:35; ambos11:06:37 según reloj RPi. El desfase con
PC es aproximadamente0,6 s; se separan por inicio de flujo, no por una
frontera exacta de reloj común. Resultados grillo-control-pcap-analysis.json.

Sumar Ramona coincide con aumento de repetidos y pausas de Grillo, además
de disminución de throughput. No se localizó físicamente el descarte.
Revisión código: spi.c descarta transacciones de tamaño inválido o colaRX
no disponible; ring_link.c descarta por CRC, tipo desconocido o cola de
salida llena. Falta contar esos eventos para TCP: el diagnóstico ICMP
actual no los descarta. Próxima instrumentación debe medir esos descartes
y presión de buffers por placa antes de elegir una corrección de capacidad.
## Carga repetida con contadores, 14:51–14:53

Se restablecieron adaptadores sin reiniciar nodos: PC Ramona10.116.0.4,
Grillo10.148.0.2. La preflight inicial recibió rechazoTCP porque servidor
RPi8765 estaba detenido; el usuario lo reinició y ambas referencias HTTP
aprobaron. No confundir este rechazo con pérdida bajo carga.

Cuatro TCP por nodo, timeout10, 60 s Grillo solo y60 s ambos:
Grillo solo1,468 Mbit/s; ambos Ramona1,475/Grillo0,690 Mbit/s. Cero errores
de aplicación registrados. Contadores centrales, deltas aproximados por
snapshots inmediatamente anteriores a las fronteras (desfase hasta~5 s):

| Central | CRC Grillo solo | CRC ambos | RX Grillo solo | RX ambos |
| --- | --- | --- | --- | --- |
| Ramona | 0 | 39 | 191 | 7413 |
| Grillo | 24 | 2 | 8342 | 3936 |
| Root | 3 | 5 | 4677 | 6295 |

Durante toda la captura: CRC Ramona+40, Grillo+27, Root+11. short,
rx_full, free_fail, tx_fail, unknown y dispatch_full permanecen0 en las
muestras. Es evidencia concreta de descarte por integridad en el anillo
SPI de los centrales; no identifica corrupción eléctrica vs software/DMA.
No medirlo como porcentaje de pérdida TCP o radio. CRC declarados IP:
Ramona0/36, Grillo18/2, Root2/5 por fase; el tipo puede estar corrupto.

La degradación simultánea de Grillo no se correlaciona con aumento del
CRC de su central: disminuyó. No afirmar que CRC explica por sí solo las
retransmisiones o el reparto de capacidad. Los periféricos no fueron
capturados y pueden tener otros descartes. Falta lectura de GrilloOeste
y RootOeste, y comprobar el camino alternativo según tabla/topología.
No PCAP nuevo simultáneo en esta repetición con contadores.
Evidencia linkdiag-load-results.json, linkdiag-load-{nodo}.raw y
linkdiag-load-{nodo}-counts.json en build/firmware. Memoria de buffers
free_min0 por sí sola no prueba saturación; rx_high1 observado no es
muestreo continuo de la cola.