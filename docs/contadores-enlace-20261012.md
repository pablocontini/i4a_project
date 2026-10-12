# Contadores de enlace y presión SPI — 12 de octubre de 2026

Objetivo: localizar descartes bajo carga TCP simultánea. Los PCAP previos
muestran degradación de Grillo al sumar Ramona, pero no identifican el salto.
No se modificaron tamaños de cola, tiempos de espera SPI, retransmisiones,
criterio de enlaces ni gateway. Se agregaron contadores de todas las tramas,
no solamente ICMP, con snapshots desde la tarea de ruteo en modo desarrollo.

## Métricas

- spi_counts: rx (transacciones completadas), short (longitud distinta de
  sizeof(payload)), rx_full (no se pudo encolar la recepción), free_fail
  (no se pudo devolver buffer), tx y tx_fail (resultado del driver maestro).
- drop_counts: crc (CRC incorrecto), crc_ip (cabecera declaraba tipo IP),
  unknown (tipo no reconocido) y dispatch_full (cola destinataria llena).
  Si falla CRC, el tipo de cabecera también puede estar corrupto: crc_ip es
  clasificación declarada, no prueba de que el contenido era un IP válido.
- buffer_pressure: wait_ge100us, wait_total_us, wait_max_us (adquisición
  de buffer libre), free_min y rx_high (muestreados al rearmar recepción).

Contadores acumulados desde arranque; usar deltas durante cada fase.
wait_max_us es acumulado. free_min y rx_high se reinician tras cada snapshot.
free_min=UINT32_MAX significa que aún no hubo muestra en ese intervalo.
Cero buffers libres puede ser normal porque el driver tiene armados los
buffers DMA: no equivale a pérdida. Espera por buffer es backpressure,
no pérdida confirmada. tx=OK confirma driver maestro, no recepción por peer.
Una trama perdida antes de completar la transacción del esclavo puede no
aparecer en rx; estos contadores tampoco prueban pérdidas radio.

Snapshots aproximadamente cada 5 s, condicionados a la tarea de ruteo;
si esa tarea se bloquea no hay garantía de heartbeat exacto. Formato
I4AROUTE tag=link, con board_id físico. Actualizaciones protegidas por
sección crítica breve; se copia y formatea fuera de la sección crítica.
No se retienen buffers, no se emite UART por cada paquete y no se crea
una tarea nueva. Cada placa necesita la imagen para observar su tramo.

## Implementación y validación

Componente components/internal/link_diagnostics. Instrumentación en spi.c
 y ring_link.c; publicación en routing_task del main con perfil rápido.
Compilación ESP-IDF5.4.1 aprobada. Prueba C real de SPI aprobada: ocho
recepciones DMA, una truncada; LD_RX=8, LD_SHORT=1, sin rx_full/free_fail.
Los flujos de propiedad y retorno de buffers permanecen iguales.

Paquetes preparados dev-link-20261012-ramona y dev-link-20261012-grillo
(compartido por Root, que mantiene su SSID/rol). Instalación y hashes se
registran después de OTA, no se consideran completados por existir build.
La carga y el PCAP deben repetirse solo después de revisar IP/rutas y
confirmar el triángulo: OTA reinicia los nodos y puede cambiar la provisión.

## Próximo ensayo

1. Verificar hashes/ready en las quince placas y topología admitida.
2. Reponer conexiones PC y rutas si cambiaron.
3. Capturar baseline y deltas de contadores en centrales y radios implicadas.
4. Repetir Grillo solo y ambos, cuatro TCP por nodo, timeout10, PCAP RPi.
5. Si contadores centrales no localizan, leer los periféricos del camino:
   Grillo Oeste y Root Oeste, Grillo Este y Ramona Oeste, Ramona Este y
   Root Este según topología vigente. No inferir valores de esas radios
   a partir de la consola del central; counters no se comparten aún.
6. Correlacionar descartes/CRC/esperas con fase, retransmisiones y pausas.

Consultar carga-simultanea-20261012.md para todas las mediciones anteriores,
continuidad-proyecto-20261012.md para política, banco y pendientes.
## Instalación verificada

Las quince placas reportaron ready=1, boot_paused=0, pending_verify=0 y
recovery_pending=0. Ramona ELF bb2f1d67c5f6c7131b6b8befe564fd6ae27f2d695f6450c897654305847aaff7;
Grillo/Root ELF 998d4b3e19831ea6c7f45212227ae5f3fc5b5b3895e4dffc2fcf106a2fb60f4f.
Paquetes firmware_bundle_linkdiag_ramona_20261012 y
firmware_bundle_linkdiag_grillo_root_20261012, 908640 bytes de aplicación.
Reportes build/firmware/linkdiag-<nodo>-installed.json. PruebaSPI y
37 pruebas Python aprobadas; ambas variantes compiladas.

Baseline de central Ramona muestra contadores funcionando, sin short,
rx_full, free_fail, tx_fail, crc ni dispatch_full en las muestras leídas.
No representa el estado de sus periféricos ni una prueba con carga.
Referencia PC HTTP falló en connect (WinError10065, host inaccesible), antes
 de iniciar nueva carga; IP/rutas/topología después de OTA en comprobación.
## Referencia posterior a OTA, 11:35

RPi wlan0UP10.255.255.254/30, pingRoot3/3. Triángulo admitido nuevo:
RootEste–RamonaOeste, RootOeste–GrilloOeste, RamonaSur–GrilloNorte.
Lateral RSSI aproximadamente -86 a -90, distinto del ensayo previo.
IP PC Ramona10.116.0.2, Grillo10.148.0.2 (cambió desde .4).
Se corrigió el endpoint de la herramienta; las rutas /32 de interfaz25
seguían apuntando incorrectamente a10.148.0.1, que ya pertenece a interfaz6.
Eso explica una referencia local inválida; no atribuir WinError10065 a SPI.
Reposición de rutas por interfaz pendiente para iniciar nueva medición.
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
## Captura de Oeste de Grillo y Oeste de Root, 14:58–15:01

Se identificaron pasivamente COM8 GrilloOeste (orientation4/default4) y
COM9 RootOeste (orientation4/default5). COM5 continúa centralRamona.
Carga cuatroTCP timeout10: Grillo solo1,150 Mbit/s; ambos Ramona1,353/
Grillo0,470 Mbit/s. Cero errores de aplicación. No hubo reset ordenado.

Durante captura, OesteGrillo:RX+6337/TX+9451, short permanece1 histórico,
CRC0, colas/freedevolución/transmisión fallidas0. OesteRoot:RX+16568/
TX+13446, short0, CRC0, colas/freedevolución/transmisión fallidas0.
rx_high1 en muestras; máximos de espera acumulados no crecieron:
Grillo6373 us,Root4517 us. Esto no prueba ausencia de pérdidas antes de
recepción, Wi-Fi ni mailbox TCP/IP. CentralRamona aumentóCRC27 (CRCdeclaradoIP25)
mientras sus colas/transmisiones fallidas permanecieron0.

Distinguir estos datos de la prueba previa de los centrales, que no fue
simultánea con esta lectura de periféricos. Los Oeste no muestran los
CRC observados en centrales. Según anillo físico C→S→W→N→E→C, CRC en
recepción del central apunta al último tramo E→C o manejo de esos buffers;
no demuestra corrupción eléctrica ni explica toda la degradación concurrente.
Siguiente lectura útil: Este y central del mismo nodo bajo la misma carga,
o contadores de rechazo TCP en tcpip_input. No concluir que Oeste está
libre de cualquier pérdida solamente porque contadores de descartes son0.

Evidencia west-identify-COM{8,9}.raw, west-load-results.json,
west-load-{ramona-center,grillo-west,root-west}.raw y *-counts.json.
No PCAP nuevo simultáneo en este ensayo. Las capturas terminaron.
## Comparación simultánea Este→Central de Ramona, 15:05–15:08

COM8 identificado EsteRamona (board_id2, orientation3), COM5 central.
Carga cuatroTCP por nodo, timeout10, fases60 s. Grillo solo1,308 Mbit/s;
ambos Ramona0,725/Grillo0,352 Mbit/s. Cero errores de aplicación.

CentralRamona:CRC2494→2504 (+10), CRCdeclaradoIP2418→2427 (+9),
RX268422→272774. Este:CRC0→0, TX268418→272765 con tx_fail0.
Tramas cortas permanecen históricas:central1,Este37, sin incremento.
No colas llenas, devolución de buffer fallida ni error de transmisión
registrado en ninguna de las dos placas. rx_high1 muestreado, máximos
acumulados de espera por buffer sin cambios. Este no era enlace Wi-Fi
activo en esta topología; reenvía el anillo hacia el central.

En fase Grillo solo CRCcentral permanece2494; aumento aparece durante
carga propia de Ramona. Comparación acota el defecto de CRC a salidaSPI
Este→recepciónCentral o manejo de los buffers de ese tramo. La ausencia
de CRC en entradaEste no garantiza que su salida no se corrompa. Tampoco
se ha probado causa eléctrica, frecuencia excesiva ni carrera DMA.
Los snapshots no son sincronizados por paquete: no calcular pérdidas
exactas restando RX/TX de instantes distintos. No PCAP nuevo simultáneo.

Siguiente control: variar un factor del tramo (cableado/conector o frecuencia
SPI de prueba) manteniendo carga y medir deltas CRC. Si persiste, agregar
huella/identidad de tramas en ambos extremos para distinguir corrupción
en transmisión/recepción del manejo de DMA. CRCobservados no explican
por sí solos toda la degradación de Grillo con carga concurrente.
Evidencia east-center-{center,east}.raw y *-counts.json,
east-center-results.json, bajo build/firmware. Capturas completadas.