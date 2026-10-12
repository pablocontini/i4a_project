# Continuidad del proyecto I4A

Estado consolidado al 12 de octubre de 2026, después de las pruebas de las
20:16–20:18 (Argentina, UTC-3). Este documento describe el estado vigente;
las notas de los ensayos conservan su cronología y pueden describir estados
anteriores. La publicación incluye fuentes, pruebas, documentación y resúmenes
de evidencia; los builds y PCAP completos permanecen locales.

## Objetivo y restricciones

Descubrir enlaces redundantes sin duplicar vecinos, conservar IP durante
un corte individual y recuperar tráfico interno e internet. Investigar
además la degradación cuando dos nodos generan carga simultánea.

Este→Central y Central→Sur pasan por cable; los demás tramos por pistas.
PCB, cableado y frecuencia SPI son restricciones fijas del usuario.
No proponer modificarlos. Todos los experimentos recientes conservaron
SPI a 8 MHz y tramas de 1600 bytes.

Repositorio: pablocontini/i4a_project, rama feat/redundant-link-discovery.
Worktree de implementación: .worktrees/redundant-link-discovery.
El worktree comnetar-stability-tools no contiene necesariamente los últimos
cambios. El paquete de continuidad del 11 de octubre y los PDF del 9–10
son históricos; este estado y sus documentos complementarios los actualizan.

## Criterio de admisión implementado

- Primera provisión: oferta canónica con distancia válida y espacio para subdividir.
- Enlace redundante nuevo: la subred lógica provided_network debe tener
  prefijo mayor y base IP numéricamente mayor que el bloque original del nodo.
  Ambas condiciones deben cumplirse. No se compara DHCP /30 ni la placa /n+3.
- Reconexión del uplink original: acepta exactamente su asignación original.
- Compatibilidad N/S con N/S y E/W con E/W; no exige orientación geográfica.
- Un enlace por vecino lógico UUID, con reserva/admisión coordinada por central.
- Una interfaz libre buscando por nodo, espera aleatoria y turno que vence.
- RSSI fijo, persistido por interfaz y configurable en portal. Excluir lóbulos
  traseros depende de calibración; la mesa no valida antenas direccionales.

Un nodo provisionado por otro nodo puede rechazar un nuevo enlace al Root
por el criterio de máscara. Para el triángulo de prueba, formar primero
ambos uplinks directos y luego el lateral; confirmar admisión en ambos extremos.

## Cambios realizados

1. Portal, modos de orientación y RSSI integrados; corregido el error de
   cabeceras HTTP demasiado largas observado al autenticar.
2. Coordinación de búsqueda con propietario, sesión y secuencia, cola SPI,
   expiración y protección frente a mensajes tardíos. Sincronización v2
   reintenta sin reejecutar callbacks duplicados y conserva trabajo pendiente.
3. Ruteo con un gateway activo y recuperación HAG reactiva. No se implementó
   lista de backups con prioridades ni balanceo. Restaurar el enlace original
   no implica que el default vuelva automáticamente a él.
4. Transporte punto a punto: una Station sin IP/máscara no reclama todos los
   destinos; se evita el desvío a Wi-Fi no disponible durante escaneo.
5. Pérdida no predeterminada: reconstrucción del retorno también al perder
   un enlace que ya no era gateway activo. Radios provisionadas DTR>1
   inician convergencia; Root DTR=1 conserva su salida central. Puede provocar
   una interrupción breve adicional al perder otros enlaces no activos.
6. Ownership de pbuf en entrada: lwIP toma propiedad solamente con ERR_OK;
   rechazos se liberan una vez. Se propaga error real de linkoutput.
7. Diagnósticos acotados de tabla, colas, salida SPI/Wi-Fi/sink y etapas ICMP;
   contadores de todas las tramas para CRC, longitud, colas, devolución,
   transmisión y espera por buffers. No hay captura completa por placa.
8. Experimento SPI: ocho descriptores, pool original de ocho y opción de
   ocho reservas extra. CRC al recoger DMA y comparación antes de procesar
   permiten detectar cambios posteriores del contenido/CRC.

## Recuperación física validada

Con firmware dev-return se realizaron cuatro cortes de radio, 60 s cada uno,
sin reiniciar el hotspot. Dos por Ramona y dos por Grillo, con camino alternativo
admitido y referencia válida. Hubo recuperación antes de vencer el corte,
ruta de retorno reconstruida e IP de cliente conservada.

| Ensayo | Gateway | RPi | Internet | Intervalo aproximado de recuperación |
| --- | --- | --- | --- | --- |
| Ramona 1 | 143/143 | 134/143 | 134/143 | 11,3 s |
| Ramona 2 | 137/137 | 130/137 | 130/137 | 9,9 s |
| Grillo 1 | 148/148 | 139/148 | 139/148 | 11,2–11,3 s |
| Grillo 2 | 133/133 | 126/133 | 126/133 | 9,8 s |

Son intervalos de muestras ICMP, no medida exacta del inicio físico del corte.
No prueban pérdida simultánea de dos enlaces, todas las topologías ni rendimiento.
Reconexiones originales se confirmaron por telemetría después de ensayos previos;
no se registró el instante exacto de cada reconexión. Los fallos anteriores a
la corrección de retorno y sus capturas también se conservaron.

## Investigación de velocidad

Servidor RPi HTTP local: 10.255.255.254:8765, archivo de 8 MiB. Herramienta
Windows selecciona índice/IP de interfaz explícitos; no usa proxy. No confundir
esta prueba con internet, HTTPS o navegación sostenida.

Con un flujo por nodo durante 180 s: Ramona 1,45 Mbit/s y Grillo 0,89 Mbit/s,
sin bloqueo completo. Los PCAP muestran segmentos repetidos y pausas.
Con cuatro flujos por nodo aparecen mayor degradación y pausas de Grillo.
Un control consecutivo mostró Grillo 1,91 Mbit/s solo y 0,67 Mbit/s al sumar
Ramona, con timeout10 y sin errores de aplicación. Repetidos exactos de Grillo
~3,9 % solo/~6,3 % ambos, pausa máxima ~1,07/~6,28 s en ese PCAP.

Timeout3 cerraba conexiones durante algunas pausas recuperables. Timeout10
permitió recuperarlas sin reconexión, pero no resolvió la lentitud. Las tablas
permanecieron estables en las muestras. El analizador cuenta repeticiones exactas
como candidatos a retransmisión, no porcentaje de pérdida radio ni clasificación
completa Wireshark; no descarta duplicación de captura ni cuenta todo solapamiento.

Se observaron CRC en los centrales. Los Oeste de Grillo y Root no tuvieron
nuevos CRC durante su ensayo. Captura Este/Central de Ramona: central +10CRC,
Este0, sin colas/transmisiones fallidas registradas. Esto acota esos CRC al
último tramo o a su recepción/memoria, pero no prueba una causa eléctrica.
Los CRC centrales de Grillo no aumentaron al sumar Ramona en otra prueba;
no explican por sí solos toda la degradación concurrente.

## Comparación experimental de reservas

Seis ejecuciones nativas aprobaron: tres casos con extra0 y tres con extra8.
La simulación reproduce espera con consumidor retenido y comprueba que las
reservas permiten rearmar sin reutilizar buffers todavía retenidos. Distingue
CRC inválido al completar recepción de modificación posterior simulada.
No reproduce ISR real ni señales eléctricas. Builds de ambas variantes aprobados.

| Ramona con ambos cargados | Buf0 A | Buf8 B |
| --- | --- | --- |
| Descarga | 1,382 Mbit/s | 1,381 Mbit/s |
| Espera acumulada por buffer, central | +2,86 s | +0 |
| CRC central | +29 | +21 |
| CRC ya inválido al recoger DMA | +29 | +21 |
| Cambios posteriores a DMA | 0 | 0 |

Espera contada para adquisiciones de al menos100 us; cero incremento no significa
latencia física cero. Extra8 conservó 7–8 reservas en las muestras. Incremento
estático RAM:12.896 bytes, más punteros de colas; falta medir heap libre real.
No se demostró mejora de velocidad ni eliminación de CRC. Cambió el bloque de
Grillo entre A y B (128/11→96/11) y hubo horas de diferencia: comparación de
throughput no es causal completa. Los CRC ya malos al recoger DMA no prueban
corrupción física ni descartan escritura incorrecta previa dentro del driver.

## Imágenes instaladas actualmente

| Nodo | Versión activa verificada | SHA-256 ELF |
| --- | --- | --- |
| Ramona | dev-buf8-20261012-ramona (experimental) | b7056e3b9783bfd187f59f641ceab164a88c708f3b3e8782d0a4bac14ab73838 |
| Grillo | dev-link-20261012-grillo | 998d4b3e19831ea6c7f45212227ae5f3fc5b5b3895e4dffc2fcf106a2fb60f4f |
| Root | dev-link-20261012-grillo, rol Root | 998d4b3e19831ea6c7f45212227ae5f3fc5b5b3895e4dffc2fcf106a2fb60f4f |

Las cinco placas de cada nodo se verificaron tras OTA: listas, sin pausa ni
verificación/recuperación pendientes. Los reportes históricos de otras imágenes
no indican el estado activo actual. No se borró NVS. Generic build/main.bin
local corresponde a buf8 Ramona: usar siempre el bundle identificado.

## Banco y herramientas

Anillo: Central→Sur→Oeste→Norte→Este→Central. Alimentación externa conservada.
En una etapa Root estaba alimentado solo por hubUSB; después se corrigió.
No atribuir todos los fallos a alimentación. Un corte accidental de todos
los nodos invalidó otro ensayo; eso no es corte individual de radio.

Puertos actuales: COM5 CentralRamona; COM8 EsteRamona; COM9 OesteRoot.
PC actual: Wi-Fi3/if25 Ramona10.52.0.2, Wi-Fi2/if6 Grillo10.116.0.2.
Reconexiones pueden cambiar IP, retirar rutas o dejar next hop obsoleto.
Perfil de Ramona quedó manual durante ensayos. Hubo desconexiones WLAN/DHCP
que invalidaron referencias; deben distinguirse de errores de transferencia.

RPi: SSH rpi@192.168.0.114; wlan0 10.255.255.254/30 hacia Root .253,
ruta10/8 por .253; wlan1 para internet. SSH automático no autentica: usuario
opera capturas/servidor. Sin receptor UDP8000 aparecen ICMP port-unreachable
con JSON citado: no son muestras nuevas ni fallos de echo. Servidor HTTP puede
quedar detenido; un rechazo de8765 no prueba fallo de SPI.

Python reparado en %LOCALAPPDATA%/I4A-Repair/idf541-env, ESP-IDF5.4.1.
No se desactivó Control de aplicaciones; el entorno anterior se conserva.
Tiempos rápidos están activos en banco, normales por defecto en Kconfig.
Antes de cerrar desarrollo restaurar perfil normal y repetir verificación.
SDKconfig de diagnóstico local usa TCPIPstack4096; perfil normal3072.
CONFIG_SPI_RX_EXTRA_BUFFERS default0; valor8 solo experimental.

## Evidencia y validación

Resúmenes portables: [evidencia/20261012](evidencia/20261012/README.md), con
manifest SHA-256. Allí están resultados de pings, carga, instalaciones,
análisis PCAP y contadores. Los UART/PCAP completos están locales y sus
nombres se documentan por ensayo. No todos los ensayos tuvieron PCAP simultáneo.

37 pruebas Python aprobaron;15 casos C de recuperación ejecutados, además
de pruebas nativas de transporte/diagnósticos documentadas por cambio.
No afirmar que toda suite se reejecutó después de cada build. Nuevas pruebas
SPI con0/8reservas aprobadas, ambas variantes compiladas y probadas como se
indica arriba. Las compilaciones validan tamaño/alineación de trama.

## Pendientes priorizados

1. Auditar TX de Este: identidad/CRC justo antes de enviar, ownership y
   buffer exclusivo alineado/DMA hasta fin de transmisión. Preparar control
   de software sin cambiar hardware/reloj; todavía NO implementado ni probado.
2. Localizar CRC anteriores a devolución DMA: driver, preparación TX, recepción
   y memoria hasta ese punto. No se confirmó modificación posterior DMA.
3. Repetir A/B de reservas con topología/recorrido fijo y medir heap/estabilidad;
   no adoptar extra8 como solución definitiva a pérdidas/velocidad.
4. Completar comparación Grillo solo/ambos con contadores del camino y PCAP
   simultáneos; agregar conteo TCP de rechazos lwIP si la evidencia lo exige.
5. Investigar desconexiones de los adaptadores/DHCP para tener banco estable.
6. Después medir HTTPS/internet con referencia WAN directa desde RPi, nodos
   separados y ambos. Aún NO validado; no afirmar resuelta la navegación lenta.
7. Restaurar tiempos normales y revisar instrumentación/coste RAM para entrega.
8. Validar más topologías, cortes prolongados y casos de pérdida simultánea;
   los cuatro cortes exitosos no equivalen a validación exhaustiva.

## Documentos de detalle

- [Recuperación y firmware](instalacion-retorno-20261012.md).
- [Carga y PCAP](carga-simultanea-20261012.md).
- [Contadores por tramo](contadores-enlace-20261012.md).
- [Reservas e integridad DMA](manejo-buffers-spi-20261012.md).
- [Coordinación](correcciones-coordinacion-v2.md).
- [Admisión y búsqueda](redundant-link-discovery.md).
- [Entorno Python](reparacion-python-20261012.md).
- [Tiempos de desarrollo](tiempos-desarrollo.md).