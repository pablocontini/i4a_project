# Recuperación de la salida a internet

Un hotspot puede responder al ping hacia `10.255.255.254` y no enviar tráfico
hacia internet. La RPi pertenece a `10.0.0.0/8`: una ruta específica hacia ese
bloque puede seguir funcionando mientras la ruta por defecto apunta a una
interfaz cuyo vecino ya se desconectó. La telemetría de `info_server.py` muestra
enlaces y subredes, pero no la tabla ni la ruta por defecto instalada.

## Correcciones de ruteo

- Las solicitudes de nuevo gateway terminan en la primera entrada vacía de
  `hag_networks`. Esas posiciones no se incorporan como rutas `0.0.0.0/0`;
  hacerlo sustituía la salida del receptor por el vecino que pedía ayuda.
- El root tiene DTR 1. Cada enlace entre nodos agrega uno y los cinco módulos
  del mismo anillo conservan el mismo valor. El módulo que recibe la primera
  provisión suma el salto y anuncia ese resultado a sus hermanos. Una elección
  de gateway recibida por SPI no agrega otro salto.
- Una respuesta de gateway se compara con su costo completo, incluyendo el
  salto hasta el vecino. Candidatos de costo igual o peor conservan la salida
  actual. DTR cero y valores que desbordarían el contador no se aceptan.

- La retirada de una interfaz elimina sus rutas específicas e invalida la
  salida por defecto si la usaba. `ROUTING_OUTPUT_NONE` y `ROUTE_NONE`
  representan ese intervalo sin gateway. El hook devuelve una interfaz de
  descarte con `ERR_RTE`: devolver `NULL` permitiría el fallback de lwIP a SPI.
- Durante la recuperación se conserva la subred del nodo, las subredes de los
  módulos y la configuración del AP de clientes. Una provisión repetida no
  renumera un nodo que ya tiene subred, aunque esté buscando gateway.
- Cada interfaz conectada que espera gateway reintenta su solicitud cada
  30 segundos; un tick atrasado provoca como máximo un reintento. Los
  reintentos se cancelan al conocer gateway. El root responde a los 10 segundos
  del primer pedido y los adicionales no postergan esa respuesta.
- Los timers de ruteo se procesan con el token de `RS_ROUTING`, junto con los
  mensajes. Una solicitud que ya contiene la subred del receptor se descarta
  antes de incorporar rutas, evitando que el recorrido vuelva sobre sí mismo.

No se cambian los campos de los mensajes de ruteo. Para probar estas
correcciones, recompilar y actualizar los cinco ESP de cada nodo de la red,
incluido el root, evitando mezclar los cálculos de DTR anteriores y nuevos.

## Verificación

```bash
python tests/run_native_routing.py --cc cc
python tests/run_native_neighbors.py --cc cc
python tests/run_native_netif.py --cc cc
python -m unittest discover -s tests -v
idf.py -C app build
```

En Windows se puede pasar la ruta de TinyCC a `--cc`. Las pruebas nativas
ejecutan los callbacks C de ruteo reales. Reproducen la provisión inicial, la
solicitud con entradas vacías, la distancia tras elegir gateway, el rechazo de
candidatos iguales o inalcanzables y la pérdida de la salida original seguida
de recuperación por un enlace existente al root o a través de otro forwarder.
Comprueban las decisiones de salida y retorno para un cliente, la RPi y una
dirección pública, sin reconfigurar las subredes de los módulos ni el AP de
clientes. También verifican reintentos, timers con token, mensajes repetidos,
provisiones inválidas y el descarte sin fallback. SPI y Wi-Fi se sustituyen por
proveedores simulados; estas pruebas no miden caudal ni garantizan la entrega
de mensajes, DHCP, NAT o DNS en placas.

En hardware, primero probar cada hotspot con los otros clientes sin tráfico;
después navegar desde ambos. Conservar la captura ICMP en la RPi para verificar
pedidos y respuestas. Por último, interrumpir el enlace que dio la provisión
inicial y comprobar que se recupera internet mediante otro enlace. Usar nombres
distintos para los hotspots permite identificar cada prueba.

Un RSSI bajo en el enlace entre nodos puede causar lentitud independientemente
del ruteo. Por ejemplo, en la prueba de tres nodos el enlace root W ↔ nodo
`ComNetAR(2)` W reportó -84 dBm, mientras root E ↔ nodo `ComNetAR` W reportó
-59 dBm. Es una lectura de telemetría que puede tener hasta cinco minutos de
antigüedad, no una medición de pérdida de paquetes o de velocidad.

## Latencia y caudal

La inicialización Wi-Fi desactiva modem-sleep con
`esp_wifi_set_ps(WIFI_PS_NONE)`. ESP-IDF usa `WIFI_PS_MIN_MODEM` por defecto:
una Station puede demorar la recepción hasta el siguiente DTIM. Desactivarlo
reduce esa espera y aumenta el consumo eléctrico. No garantiza una velocidad
de transferencia determinada ni corrige pérdidas en SPI o señal débil.

Referencia: [ahorro de energía Wi-Fi en ESP-IDF 5.4.1](https://docs.espressif.com/projects/esp-idf/en/v5.4.1/esp32/api-guides/wifi.html#esp32-wi-fi-power-saving-mode).

Para separar la red de nodos de la salida WAN, descargar desde cada hotspot un
archivo servido por la propia RPi en `10.255.255.254`. Comparar cada hotspot
por separado y luego ambos con tráfico. Registrar tamaño, tiempo y bytes por
segundo, además del ping; una transferencia local lenta ubica el problema en
el recorrido hasta la RPi sin atribuirlo todavía a un componente específico.

## Recepción bajo carga

El callback de entrada SPI se hace cargo del `pbuf`: lo entrega a lwIP cuando
`netif->input` devuelve `ERR_OK` y lo libera cuando la interfaz está caída o
la cola TCP/IP rechaza el paquete. El llamador no vuelve a liberarlo. Esto
funciona con `CONFIG_ESP_NETIF_RECEIVE_REPORT_ERRORS` habilitado o deshabilitado.
En ESP-IDF 5.4.1 esa opción está deshabilitada por defecto, por lo que
`esp_netif_receive` devuelve éxito aunque su callback rechace un paquete.
Antes, esos rechazos conservaban buffers sin liberar y podían agotar memoria
durante una transferencia sostenida.

Las seis pruebas nativas ejecutan el callback C real con ambos modos de reporte.
Simulan 10.000 entregas por modo, incluidas 7.500 rechazadas por saturación,
además de una interfaz caída o inválida. La prueba de saturación falla con el
callback anterior. No reproducen el tráfico ni los reinicios en hardware.

Un `GET ... 200` del servidor HTTP indica que comenzó una respuesta, no que
terminó la descarga. Un `BrokenPipeError` posterior indica que se intentó
escribir en una conexión cerrada: puede aparecer cuando curl abandona por
timeout o al perderse la conexión. Conservar la salida final de curl junto al
registro serial; para atribuir un reinicio al ESP32 hay que capturar otro
arranque o un mensaje de error, no solamente una desconexión Wi-Fi.
