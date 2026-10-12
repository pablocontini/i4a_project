# Diagnósticos del plano IP

Estos cambios buscan localizar la falla de conectividad previa al corte.
No demuestran que se haya corregido su causa física. El firmware anterior
tenía sesiones y control INFO activos mientras los paquetes del cliente
fallaban antes de llegar a la RPi.

## Cambios

- Las netif RX/TX propagan el err_t de linkoutput en vez de devolver éxito
  incondicional. Se conserva el manejo y propiedad de pbuf.
- `ip_diagnostics` registra ICMP echo request/reply por etapa: recepción SPI,
  entrada a lwIP aceptada/rechazada, intento y resultado de transmisión,
  descarte por compuerta OTA y retorno del transmisor físico.
- Los pbuf encadenados se inspeccionan copiando un prefijo acotado, sin tomar
  propiedad ni leerlos después de que lwIP pueda consumirlos.
- Los logs tienen totales acumulados, último ejemplo, origen, destino,
  ID/secuencia ICMP y error. Emisión máxima: una línea por etapa cada 5 s.
  Se encolan mediante os_diag; no se imprime UART desde callbacks SPI.
- El hook publica consultas de rutas como `route_lookup`, separadas de
  ICMP: un hook no conoce el protocolo y puede estar consultando por UDP.
- En modo de tiempos rápidos, cada módulo publica `route_snapshot` cada
  cinco ticks de un segundo: salida por defecto, cantidad/huella de la tabla
  y salidas hacia RPi e internet. `queue_snapshot` informa trabajo pendiente.
- Las entradas completas se publican inicialmente y después cada 30 ticks.
  La huella excluye padding; permite comparar contenido entre módulos.

El snapshot usa mutex_try_lock y copia la tabla antes de formatear. No espera
el token ni modifica rutas, colas o timers de recuperación. Si la tabla está
ocupada informa snapshot_busy. Se llama antes de sync_on_tick; un bloqueo
posterior de la tarea puede atrasar la siguiente muestra. No es una promesa
de heartbeat de cinco segundos exactos bajo cualquier bloqueo.

No se leen global_state/dtr de la unión de roles como si el mutex de la
tabla los protegiera. No se usa un cliente fijo como representación del
cliente actual: las entradas completas permiten evaluar su subred.

## Interpretación

`board_id` en tag=ip es config_get_id: N=0, S=1, E=2, W=3,
central doméstico=4 y central Root=5. `orientation` en routing sigue siendo
N=1, S=2, E=3, W=4, C=5. No mezclar las numeraciones.

`spi_tx_ok` no significa que el paquete llegó al destino. Una compuerta
de mantenimiento descarta IP deliberadamente reportando ESP_OK; por eso
se agregó `spi_gate_drop`. `spi_wire_tx_ok` describe el retorno del driver
físico, no el procesamiento remoto de lwIP ni la recepción del ICMP reply.

Si el central registra envío y el sur no registra recepción, investigar ese
tramo y sus colas/CRC. Si el sur recibe pero el hook elige sink, comparar
sus rutas con las del uplink. Si input rechaza o wire_tx falla, usar su error
sin atribuirlo a DNS/NAT. La telemetría que genera una Station no prueba el
recorrido del cliente desde el central doméstico.

Los contadores son por etapa y se publican al siguiente evento elegible;
no imprimen un resumen final si cesa el tráfico. No contabilizan UDP ni ICMP
port-unreachable. Los frames sin cabecera echo interpretable y descartes
anteriores a la netif no quedan cubiertos completamente. La cola os_diag
puede descartar líneas y lo informa; no interpretar ausencia de una línea
aislada como prueba de pérdida. Se conservan los mensajes de control v2.

## Configuración de laboratorio

Los tiempos rápidos se mantienen. La imagen de diagnóstico usa un sdkconfig
separado: `build/firmware/sdkconfig-ipdiag`, copia de la configuración actual
con la pila TCP/IP aumentada de 3072 a 4096 bytes para dejar margen a la
instrumentación en la ruta de transmisión que ya tiene un frame de 1600 bytes
en la pila. No se presenta ese margen como corrección de una falla demostrada.
`app/sdkconfig` conserva el tamaño anterior.

Para futuras compilaciones normales, seleccionar explícitamente app/sdkconfig
en la variable SDKCONFIG de CMake y desactivar los tiempos rápidos antes de
la entrega final. El build genérico conserva el último perfil seleccionado.

## Verificación

28 verificaciones nativas aprobadas: 5 del parser/contadores, 10 de ownership
de pbuf (ambos modos de reporte), 8 de output real RX/TX, 3 de snapshots y
2 de mantenimiento/retorno físico. Las 37 pruebas Python aprobaron.
Recuperación y hook compilaron; esa DLL no se ejecutó en esta comprobación.
La suite anterior de recuperación v2 había aprobado en Linux.

La compilación ESP-IDF 5.4.1 del perfil con margen de pila terminó y se
instaló en las cinco placas de Ramona por COM5. Versión:
`dev-ipdiag-20261012-ramona`. Bundle usado:
`firmware_bundle_ipdiag_ramona_20261012_stack4096`.
ELF SHA-256: `04ae7ffc55cc36c1bdc1aa184566b1374f024b4e97d6ee4945e470e3ddcd13ce`.
BIN SHA-256: `0aff7f27451b95b09b4ba7cebcfe78dc8bf76e284e314bf3a5050251e065351d`.
Aplicación: 907648 bytes. Las cinco placas quedaron ready=1, boot_paused=0,
pending_verify=0 y recovery_pending=0 después de la recuperación automática.
Grillo y Root conservan sus imágenes rápidas anteriores, compatibles en v2.
El primer bundle sin margen de pila no se instaló.

Registros: build/firmware/ipdiag-ramona-ota.log e ipdiag-ramona-installed.json.
La primera captura de central COM5 y sur COM7 mostró la misma tabla:
default=4, count=2, hash=bfddf9b1, rpi_out=4 e internet_out=4.
Entradas: 10.148.0.0/14→central(5) y 10.96.0.0/11→norte(1).
Las colas reportaron peer=0, sibling=0 y waiting=0.

La medición posterior no llegó a enviar los pings: las rutas /32 de Windows
desaparecieron durante la reconexión del hotspot y el monitor abortó al
comprobar que ya no seleccionaba Wi-Fi 3. Se mantienen IP 10.148.0.2 y
gateway 10.148.0.1. Reponer rutas y registrar sondas junto a estos diagnósticos
antes de atribuir una causa o provocar el corte.
