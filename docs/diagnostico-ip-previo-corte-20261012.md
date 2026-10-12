# Diagnóstico IP antes del corte: 12/10/2026

Todavía no se aplicó un corte con las imágenes rápidas. Primero se intenta
establecer una referencia estable de tráfico del cliente por Ramona.

## Ensayo anterior al reinicio accidental

PC en Wi-Fi 3 (índice 25), IP 10.52.0.2 y gateway 10.52.0.1. Rutas /32
verificadas antes y después de cada ronda; todas las sondas usaron esa interfaz.

- Primera referencia: gateway 10/10; RPi e internet 3/10, las últimas rondas.
- Segunda referencia: gateway 10/10; RPi e internet 0/10.
- Captura ICMP en RPi: cero paquetes con host 10.52.0.2 durante las sondas.
- RPi conservaba wlan0 10.255.255.254/30, la ruta 10/8 vía .253 y ping al
  Root 3/3. Llegaba telemetría de Ramona, generada en su uplink.
- Comparación con ping.exe: RPi 0/5; internet 2/5. La intermitencia no se
  atribuyó exclusivamente a .NET Ping.
- Prueba de saltos: gateway 10/10; Station norte 10.32.0.2, AP Root norte
  10.32.0.1, Root central .253, RPi .254 e internet 0/10 cada uno.
- INFO del central y luego de todo el anillo: cinco placas listas, sin pausa
  ni recuperación pendiente, con ELF 60b476da... de desarrollo rápido.
- UART norte: sesión renovada y transporte 10.32.0.2 activo. La captura del
  central no tuvo texto; eso no acreditó que estuviera apagado, porque respondió
  a INFO. Algunos registros UART de periféricos tuvieron grandes cantidades
  de fragmentos repetidos y no permitieron reconstruir tablas completas.

La telemetría de ese ensayo mostraba Root norte–Ramona norte,
Root este–Grillo oeste y Ramona este–Grillo este, admitidos en ambos extremos.
La prueba de saltos apunta al recorrido interno de Ramona o su retorno,
pero no prueba por sí sola qué placa/cable/tabla descarta los paquetes.

## Distinción importante de planos

La revisión del código confirma que HOME (central doméstico) manda el tráfico
fuera de su subred a SPI sin consultar rt_do_route. En el recorrido físico
Central→Sur→Oeste→Norte→Este→Central, los periféricos reciben el paquete en
lwIP y lo vuelven a enrutar usando sus propias copias compartidas de la tabla.
Sur u oeste pueden descartarlo si su salida es 255 aunque el norte tenga
sesión Wi-Fi. La salida por defecto del central por sí sola no explica eso.

La Station inicia su propio cliente de telemetría. Un UDP originado en la
placa del uplink puede llegar a la RPi sin atravesar el recorrido del cliente
desde el central. INFO usa frames INTERNAL y colas distintas del plano IP
ESP_NETIF; su éxito no valida el encaminamiento IP.

Quedan abiertas pérdidas TX/RX/cola/CRC y copias de tabla divergentes. Los
ESP_LOG normales de esas capas están desactivados por LOG_DEFAULT_LEVEL=0.
Los diagnósticos actuales no muestran el recorrido de cada ICMP ni permiten
atribuir todos los descartes. Además, las output_function de netif TX/RX
ignoran el retorno de linkoutput y reportan ESP_OK: identificado por lectura,
sin corregir ni demostrar que sea la causa de este ensayo.

Si el fallo se reproduce en la nueva referencia, instrumentar de forma
acotada ingress, destino, salida elegida, resultado TX y motivo de descarte
RX. Mantener sincronización y evitar imprimir/bloquear callbacks SPI.

## Reinicio accidental y nuevo ensayo

El usuario informó que desconectó accidentalmente la alimentación de todos
los nodos. Se perdió el estado previo; este arranque se registra aparte y
no cuenta como un corte aislado del enlace principal.

Wi-Fi 3 recibió 10.148.0.2 y gateway 10.148.0.1. Las rutas de prueba anteriores
ya no estaban presentes. Ping al nuevo gateway: 3/3. Se solicitaron rutas
temporales /32 vía el nuevo gateway a 10.128.0.2, 10.128.0.1, .253, .254 y
1.1.1.1; su creación y el nuevo baseline todavía están pendientes.

La telemetría recibida está parcialmente truncada: Root este y oeste admitidos;
Grillo oeste asociado/admitido a Root este; Grillo norte asociado/admitido
al AP norte de Ramona. La fila de Ramona que permitiría confirmar el otro
extremo y su Station principal no se recibió completa. No elegir todavía
un objetivo de corte usando el mapa anterior.

Consolas actuales: COM5 central de Ramona; COM7 sur de Ramona. INFO después
del reinicio confirma las cinco placas listas, sin pausa ni recuperación
pendiente, todas con ELF 60b476dad292a526d38957c006e1692628b9a1941da0611076c02edcde9f19cb.
El sur escanea y emite scan_no_candidate con threshold=-128. Eso no es por
sí solo un fallo: puede ser una interfaz libre buscando sin candidato admitido.

El ICMP udp port 8000 unreachable de la RPi es una respuesta al UDP sin
receptor escuchando; contiene una copia del paquete original. No es otro
informe del nodo ni una prueba del destino de los pings del cliente.

Evidencia local en build/firmware: baseline-dev-fast-20261012-005735.csv,
baseline-stable-dev-fast-20261012-005903.csv,
icmp-capture-probes-20261012-010123.csv, hops-dev-fast-20261012-011408.csv,
ramona-routing-latest-captures.json, after-power-reset-captures.json,
ramona-ring-hoptest-status-20261012.log y ramona-post-power-reset-status-20261012.json.

Próximo paso: actualizar rutas, obtener las filas completas de los tres nodos,
comprobar la referencia y solamente después preparar el corte controlado.
