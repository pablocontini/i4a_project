# Exploración del manejo de buffers SPI — 12 de octubre de 2026

## Restricciones físicas

Este→Central y Central→Sur pasan por cable; los restantes tramos del anillo
van por pistas del PCB. PCB, cableado y frecuencia SPI no se pueden cambiar.
Estos son requisitos del usuario: quedan descartadas propuestas de cambiar
conectores/cables, bajar reloj o rediseñar la placa. Se mantiene SPI8MHz y
la trama existente de1600bytes. Se investiga exclusivamente software.

## Hallazgo reproducido

El driver mantiene8 descriptores DMA, y el pool tenía8buffers. Al entregarlos
inicialmente todos al driver no quedan reservas. Tras obtener una recepción
completada se encola su buffer para la tarea consumidora y queue_rx espera
otro buffer libre. Si el consumidor tarda, el polling deja de recoger otras
recepciones completadas y no rearma ese descriptor inmediatamente.
No significa que desaparezcan los otros7descriptores, ni que esto demuestre
la causa de los CRC. Sí es un acoplamiento de DMA a la latencia del consumidor.

El contrato local ESP-IDF5.4.1 de spi_slave_get_trans_result devuelve la
transacción cuando terminó, permitiendo liberar o reutilizar el buffer.
No se identificó una violación demostrada de ese contrato en el flujo actual.
No se debe reutilizar un buffer entregado al consumidor hasta que lo devuelve.

## Variante experimental

CONFIG_SPI_RX_EXTRA_BUFFERS (0..16, default0), sumados al pool de recepción.
Los descriptores DMA siguen siendo8. Con extra8, el pool/colas tienen16,
con12.800bytes adicionales de buffers más metadatos y punteros de colas.
No se introduce malloc/free por paquete, ni copia adicional de la trama.
Si todos los buffers están ocupados, sigue habiendo backpressure; no es
capacidad ilimitada. El consumo adicional deRAM puede afectar otras partes
(Wi-Fi/TCP/OTA); debe medirse heap libre y estabilidad antes de adoptarlo.
Default0 conserva capacidad anterior para comparación; no seleccionar8
como configuración final antes de pruebas físicas.

Se añaden asserts de tamaño exacto de trama y alineación de stride DMA.
La variante no altera SPI_FREQ, MOSI/CLK/CS, formato de mensajes, CRC wire,
ruteo ni criterio de enlaces. Puede ayudar al rearmado, pero no corregirá
necesariamente corrupción que ya llegue por cable o dentro del driver.

## Diagnóstico de integridad

Se calculaCRC cuando get_trans_result entrega una trama completa, antes de
encolarla. Se guarda CRC calculado yCRC recibido como metadatos externos al
buffer. El procesamiento del anillo compara los valores antes de modificar
TTL/retransmitir. No retiene el buffer tras liberarlo.
I4AROUTE tag=link event=rx_integrity:
- crc_at_dma: ya no coincide al recoger la recepción del driver.
- changed_after_dma: cambióCRC del contenido o campoCRC antes del procesamiento.

crc_at_dma no distingue corrupción del cable frente a escritura incorrecta
hasta el momento de completar DMA. changed_after_dma indica cambio entre
las dos comprobaciones (salvo colisiónCRC), y ayuda a investigar propiedad,
reutilización o escritura concurrente en memoria. No es comparación byte
por byte. Los contadores agregados requieren deltas e intervalos comparables.
La nueva comprobación agrega trabajo CPU; el baseline0 también la incluye
para comparar reservas sin confundirlo con coste de instrumentación.

## Pruebas nativas

Tres casos del códigoSPI real, ejecutados con extra0 yextra8 (6 ejecuciones):
1. Ocho recepciones y una trama truncada: no se entrega trama incompleta.
2. Consumidor retenido: extra0 espera tras primera entrega; extra8 completa
   ocho recepciones manteniendo8descriptores sin reutilizar buffers retenidos.
3. CRC corrupto desde recepción se cuenta por separado de una modificación
   posterior simulada del contenido oCRC. Punteros NULL/desalineados no se
   interpretan como slots del pool.
Todos aprobaron. La simulación no reproduce tiempos eléctricos, ISR real
ni throughput Wi-Fi. No afirmar que se resolvieron las pérdidas físicas.

## Estado y plan

Se preparan perfiles SDKCONFIG aislados para0y8 y bundles identificados,
con relojes iguales. Los nodos aún ejecutan dev-link, la imagen anterior.
Antes de flashear comprobar memoria y hashes, conservar alimentación externa
y revisar topología/IP trasOTA. Comparar fases de carga iguales, deltas de
crc_at_dma/changed_after_dma, CRC delanillo, tiempo por buffers, throughput,
heap y estabilidad. Si CRC ya es malo al salir deDMA y el pool extra no
mejora, investigar recepción/propiedadDMA y recuperación por software;
no volver a proponer cambios físicos que el usuario excluyó.
## Compilación completada y memoria

Ambas variantes Ramona compiladas con ESP-IDF5.4.1, SPI8MHz y trama1600.
- firmware_bundle_buf0_ramona_20261012, 909024bytes, ELF
  c15279ff2c14a1a5d4123373edc29d54086d84bbb73e8a75ffa8f78d4ee01768.
- firmware_bundle_buf8_ramona_20261012, 921824bytes, ELF
  b7056e3b9783bfd187f59f641ceab164a88c708f3b3e8782d0a4bac14ab73838.

Memoria estática según ELF: extra0 dram.data29464/bss31728;
extra8 dram.data42264/bss31824. IncrementoRAM12896bytes, más diferencia
de punteros de colas asignadas dinámicamente. IRAMtext105035 igual en ambas.
No confundir esta medición con heap libre real durante Wi-Fi/TCP: falta
medirlo en el dispositivo. La aplicación flash también crece12800bytes
porque el poolDMA va a una sección de datos en esta compilación.

Perfiles aislados build/firmware/sdkconfig-buffers-{baseline,spare}.
CMakecache/genericmain.bin apuntan actualmente a buf8Ramona; usar siempre
bundle identificado para una instalación concreta. No usarlo en Grillo.
Ninguna variante buf0/buf8 se flasheó: los nodos conservan dev-link.
Prueba siguiente: piloto controlado únicamente en Ramona, primero0 y luego8,
con Este/central registrados y misma carga. Revisar heap, CRCtemprano/tardío,
espera por buffer y velocidad antes de adoptar reserva en otros nodos.
La restricción del usuario sobre hardware y reloj sigue aplicando.
## Inicio de comparación física

Ramona buf0 instalado y verificado en cinco placas: ready1/paused0/
verify0/recovery0, ELFc15279ff2c14a1a5d4123373edc29d54086d84bbb73e8a75ffa8f78d4ee01768.
Grillo yRoot siguen dev-link. TrasOTA Ramona fue provisionada con bloque
10.32.0.0/11 (hotspot10.52.0.0/14), salidaSur (default2), distinto del
baseline previo96/11. PCWi-Fi3 obtuvo10.52.0.2 tras reconectar. Grillo
PCWi-Fi2 sigue10.148.0.2. Reponer rutas/validar referencia antes de carga.
No hay resultados de comparación todavía, buf8 aún no instalado.
Medidor admite --ramona-ip/--grillo-ip para evitar direcciones obsoletas.
Antes de sacar conclusiones, buf8 debe recuperar una topología comparable.
Un reinicio cambió provisión; no atribuir a la reserva de buffers.
Datos preliminares sin carga: centralcrc6/crc_at_dma6/changed_after_dma0.
No demuestra causa física; CRC ya era inválido al recogerDMA en esas muestras.
## Referencia buf0 medida, 16:03:56–16:05:59

Preflight aprobada por ambos clientes, servidorRPi activo. Desconexiones
Wi-Fi3 previas impidieron medir antes; el registroWLAN mostró seguridad
interrumpida. No atribuirlas todavía aSPI. En carga se muestrearon ambas
IP estables (Ramona10.52.0.2,Grillo10.148.0.2), en buf0-load-client-ip.csv.
Topología de Ramona original32/11, defaultSur2; hash11dc0e72 en snapshots
centro/Este durante referencia. Buf8 deberá compararse con condiciones
similares; todavía no hay resultadosB.

CuatroTCP por nodo, timeout10, fases60s:Grillo solo1,311 Mbit/s;
ambosRamona1,382/Grillo0,735 Mbit/s. Sin errores de aplicación.
Deltas de toda captura aproximada144s: centralRamonaCRC+29,
crc_at_dma+29, changed_after_dma0; CRCdeclaradoIP+26. EsteCRC0,
crc_at_dma0/changed_after_dma0. No evidencia de modificación de trama
entre devoluciónDMA y validación en tareas en estos datos. No descarta
corrupción dentro del driver antes de devolver la transacción.
Espera acumulada por buffers:central+2863053 us en~7772 recepciones;
Este+2928535 us en~7773. free_min0 y rx_high1. Máximos sin incremento
durante la captura; máximoEste~19,99s era anterior y no corresponde a
esta carga. No PCAP nuevo en este ensayo.
Evidencia buf0-load-results.json, buf0-load-{center,east}.raw,
*-counts.json y clienteIPcsv, bajo build/firmware. Instalaciónbuf8 iniciada
solo después de cerrar capturas, sin cambiar PCB/cableado/reloj.
## Buf8 instalado; medición B pendiente

Ocho reservas instalado/verificado en cinco placas deRamona:ELF
b7056e3b9783bfd187f59f641ceab164a88c708f3b3e8782d0a4bac14ab73838,
ready1/paused0/verify0/recovery0. Reporte buf8-ramona-installed.json.
Grillo/Root no se actualizaron. Imágenes yPCB/cables/SPI8MHz siguen iguales.

Tras arrancar, Ramona fue provisionada con32/11 (hotspot52/14), pero la
tabla quedó solamente con ruta local, default255, hash9f611060. Antes de
carga B no hay salida válida. No clasificar esta condición como0Mbit/s
comparativo ni afirmar que extra8 empeoró rendimiento sin localizar causa.
Se pidió telemetría RPi para recuperar topología comparable. Buf8 aún activo.

Muestras iniciales en reposo conextra8:free_min7,rx_high1,wait_ge100us0,
wait_total_us0,wait_max_us5–9 frente a esperas frecuentes de buf0. Es evidencia
de desacoplamiento inicial de reservas, no mejora bajo carga demostrada.
En muestra a194s:CRC3,crc_at_dma3,changed_after_dma0. No hay evidencia
registrada de modificación posterior aDMA; tampoco descarta problemas
previos del driver ni impacto de la reserva en memoria/arranque.
Resultados de referencia A documentados arriba; todavía no resultado B.
## Buf8 finalmente medido al retomar, 20:16–20:18

Ramona recuperó salidaSur2 sin nueva instalación y sigue con buf8.
Ambos adaptadores estaban sin IP válida; se reconectaron y preflightHTTP
aprobó:Ramona10.52.0.2/if25,Grillo10.116.0.2/if6. Grillo cambió de bloque
antes usado128/11 (hotspot148) a96/11 (hotspot116). Por ello el A/B de
throughput NO es un control causal completo de reservas: misma carga,
pero distinta asignación/recorrido de Grillo y varias horas de diferencia.

CuatroTCP por nodo, timeout10, fases60s. Grillo solo1,352 Mbit/s;
ambosRamona1,381/Grillo0,563 Mbit/s. Sin errores de aplicación y con
IPestables en muestras. TablasCentro/Este hash907c0e72 estables duranteB,
default2; enA hash11dc0e72,default2. No se capturóPCAP nuevo.

| Métrica Ramona durante captura | Buf0 A | Buf8 B |
| --- | --- | --- |
| Velocidad con ambos cargados | 1,382 Mbit/s | 1,381 Mbit/s |
| Espera acumulada adquisición buffers, central | +2863053 us | +0 us |
| Espera acumulada adquisición buffers, Este | +2928535 us | +0 us |
| CRC descartados en central | +29 | +21 |
| CRC inválidos al recoger DMA, central | +29 | +21 |
| Cambios después de DMA | 0 | 0 |

Contadores de espera solo suman adquisiciones>=100us; +0 no significa
latencia física cero. B free_min7–8 en las muestras, frente a0 enA.
Máximos acumulados de esperaB ya incluían eventos dearranque: central~11s,
Este~20s, no crecieron en carga; no atribuirlos al intervalo medido.
CRCdeclaradoIP B+20; EsteCRC/CRCtemprano permanecen0. No modificación de
buffer posteriorDMA detectada. B no tuvo nuevos descartes por colas ni
fallos de devolución/transmisión en las muestras. Sin medición de heap
libre real; incremento estático12.896bytes medido previamente.

Resultado acotado: reservas eliminan las esperas registradas en la cargaB,
pero no se demostró mejora de velocidad ni eliminación de CRC. La diferencia
29→21 no prueba mejor integridad con una repetición y topología diferente.
La corrupción observada ya existe al recoger el bufferDMA: revisar transporte
previo/driver y propiedad hasta ese punto, sin cambiar hardware ni reloj.
Para mejora real de throughput se requiere nuevoA/B con topología fija.

Evidencia buf8-load-results.json, buf8-load-{center,east}.raw y *-counts.json,
buf8-load-client-ip.csv, bajo build/firmware. La variante activa deRamona
queda buf8,Experimental. Grillo yRoot siguen dev-link. Capturas terminadas.