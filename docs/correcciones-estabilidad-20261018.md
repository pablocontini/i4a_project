# Correcciones de estabilidad durante la búsqueda de enlaces

Fecha: 18 de octubre de 2026.

**Actualización tras el intento OTA en COM5:** el firmware instalado se identificó
como SOAK Grillo 17/10, ELF `1eb86333cf9f23f7571af8a6a425d6106223195b1c4036109e4676c6e2427d74`.
La imagen de 804816 bytes compilada en esta revisión pertenece a otra base y no
conserva todos los componentes de esa variante. No utilizarla como sustituto
que preserve las funciones SOAK. Los dos formatos internos llamados v2 también
difieren. El intento falló antes de SELECT y no seleccionó esta imagen.
Ver [diagnóstico OTA de COM5](diagnostico-ota-com5-20261018.md).

Se revisó el worktree `comnetar-stability-tools`, basado en `a0ccdf3`,
conservando sus modificaciones locales previas. El PDF
`resumen-implementacion-y-pruebas.pdf` describe también otro worktree y revisiones
posteriores: aquí no existían `discovery_manager` ni `node_control`. Las pruebas
físicas citadas allí no acreditan el estado de este checkout.

## Causas y correcciones

| Área | Defecto | Corrección |
| --- | --- | --- |
| Mutex | Ruteo y estado compartido pasaban el handle donde la API espera su dirección, interpretando memoria interna de un semáforo como otro handle. | Referencias `&mutex`, pruebas estrictas y comprobación de creación al arrancar. |
| Búsqueda Station | Eliminar la tarea con `vTaskDelete(handle)` podía dejar retenidos mutex de escaneo o coordinación. | Cancelación cooperativa; espera de salida antes de retirar la interfaz y comprobación tras cada escaneo. |
| DHCP | Un reintento podía abortar al encontrar DHCP ya iniciado. | `DHCP_ALREADY_STARTED` es idempotente. Desconexión limpia el estado aun sin evento; escaneos fallidos descartan resultados anteriores. |
| Máscara cero | Durante el escaneo, subred/máscara `0/0` coincidían con cualquier destino y capturaban el tráfico de Internet. | Clasificación punto a punto exige interfaz viva, máscara válida y conexión IP completa en Station. |
| AP ocupado | Una asociación aún sin listener TCP podía interpretarse como AP libre. | Se consulta la lista real de estaciones; ante error se conserva AP. La búsqueda espera durante mantenimiento y bloqueo de Station. |
| Sesión TCP | Un envío concurrente con la admisión cerraba el socket porque la sesión todavía no estaba admitida. | El envío prematuro falla sin cerrar la negociación. Las sesiones admitidas y vencidas siguen cerrándose. |
| Gateway del proveedor | El saludo del vecino sin provisión anunciaba `0/0` y sustituía el gateway válido. | Se ignora esa oferta vacía. |
| Provisión inválida | Máscaras no contiguas, redes desalineadas y bloques demasiado pequeños alcanzaban desplazamientos indefinidos o direcciones inválidas. | Se valida antes de modificar estado; bloque del nodo como máximo `/27`, cinco sectores de al menos `/30`, orientaciones válidas y cálculo unsigned. |
| Reconexión y retorno | Se reinstalaba la ruta específica sin restaurar WAN. Perder un enlace que ya no era gateway tampoco recuperaba el retorno. | Gateway por DTR sin renumeración y anuncio de retorno por los enlaces supervivientes. |
| Ruta retirada | El default podía seguir usando el enlace perdido; `NULL` desde el hook permitía fallback de lwIP a SPI. | Se invalida esa salida; una decisión sin ruta o interfaz disponible devuelve descarte con `ERR_RTE`. |
| Colas | Las llegadas durante un lote podían borrarse al finalizarlo; `PEER_LOST` se perdía con cola llena. | Mutex, lotes acotados, ticks serializados y prioridad para la pérdida del enlace. |
| Búsqueda de gateway | Pedidos repetidos postergaban la respuesta de Root y faltaban reintentos. | El primer pedido fija el plazo; reintento a 30 s, cancelado al ganar. Se rechazan ciclos HAG y DTR que desborda. |
| Token perdido | Un envío perdido dejaba la coordinación ocupada indefinidamente. | Sesión y secuencia; se repite la misma circulación a 5 s, agrupando solicitudes y sin repetir callbacks completados. |
| Tabla no publicada | Un envío fallido se ignoraba y el turno avanzaba con tablas distintas. | `ss_refresh` informa resultado; retención del token hasta publicar, sin reaplicar el lote. El siguiente lote espera otra circulación. |
| Mensajes de estado | Faltaban validación de mensajes vacíos e identidad de publicación. | Longitud, versión, componente, sesión, secuencia y propietario; una versión atrasada no sobrescribe la tabla actual. |
| Bloqueo de broadcast | El callback podía esperar el mutex del emisor que esperaba el retorno de esa misma trama. | Reenvío físico antes de entregar al callback; los errores de callback no abortan el nodo. |
| ACK atrasado | Un retorno anterior podía confirmar por error el envío siguiente. | ID pendiente, aceptación única, limpieza de notificaciones y protección del handle durante confirmación y retirada. |
| Pila TCP/IP | Cada callback TX reservaba una trama de 1600 bytes con una pila TCP/IP configurada en 3072 bytes. | Trama temporal fuera de la pila, liberada en todas las salidas. GCC mide 1632 bytes antes y 32 después por callback, más 32 del helper. |
| Tamaños SPI/IP | RX aceptaba longitudes superiores a la trama y trataba IPv6 como IPv4. | Validación de capacidad, cabecera y longitud antes de copiar. Se propagan errores de TX y asignación. |

La reducción de pila es una medición de compilador, no la reproducción de un
panic físico. Las regresiones de negociación TCP, orden de broadcast y hook
sin ruta fallan al compilar sus fuentes anteriores. La prueba de ACK atrasado
también falla al retirar la correlación de ID.

## Validación ejecutada

34 pruebas Python y 85 casos nativos aprobados:

| Runner nativo | Casos |
| --- | ---: |
| neighbors | 21 |
| wireless | 6 |
| routing | 18 |
| coordination | 5 |
| netif | 10 |
| broadcast | 6 |
| routing_hooks | 5 |
| ota | 14 |

OTA verifica además compatibilidad C/Python y rechazo de corrupción y
truncamiento. Algunos casos recorren varias placas o modos; la tabla cuenta
casos del runner, sin sumar esas iteraciones como pruebas independientes.

La simulación de coordinación cubre pérdida de token en cada uno de sus cinco
destinos, envío fallido, repetición sin duplicar callbacks y publicación fallida
en cada placa, incluido el central. Verifica reintento sin repetir mutaciones y
rechazo de mensajes antiguos, truncados o de otro propietario.

Para repetir desde la raíz, con compilador nativo:

```powershell
python -m unittest discover -s tests -p 'test_*.py'
python tests/run_native_neighbors.py --cc app/build/tools/tcc/tcc.exe
python tests/run_native_wireless.py --cc app/build/tools/tcc/tcc.exe
python tests/run_native_routing.py --cc app/build/tools/tcc/tcc.exe
python tests/run_native_coordination.py --cc app/build/tools/tcc/tcc.exe
python tests/run_native_netif.py --cc app/build/tools/tcc/tcc.exe
python tests/run_native_broadcast.py --cc app/build/tools/tcc/tcc.exe
python tests/run_native_routing_hooks.py --cc app/build/tools/tcc/tcc.exe
python tests/run_native_ota.py --cc app/build/tools/tcc/tcc.exe
```

TinyCC 0.9.27 para Windows se descargó del servidor oficial de Savannah a
`app/build/tools/`, ignorado por Git. En Linux se puede usar `--cc cc`.
Los proveedores simulados ejecutan las funciones C reales.

Compilación completa ESP-IDF 5.4.1 con GCC Xtensa 14.2 aprobada, mediante
`idf.py -C app build` con el entorno IDF activado. La imagen cabe en las
particiones existentes. Log local: `app/build/stability-build-final.log`.

Imagen generada: `app/build/main.bin`, 804816 bytes; SHA-256:
`7b58dac9a883a74b5ec1971b667cf2916581b3567f04b7a9006aa7b5e311981d`.
La comprobación de tamaño deja 60% libre en la partición de aplicación actual.

La suma estática de marcos de publicación hasta la cola SPI es aproximadamente
3232 bytes en el seguidor sobre 4096 disponibles para callbacks. Excluye contexto
del puerto y ramas de error/logging; no es un mínimo de pila medido en ejecución.
Datos locales: `app/build/native-tests/stack_usage_report.txt` y `.su` asociados.

## Compatibilidad y límites

SYNC y SHARED_STATE usan ahora el protocolo interno v2. Hay que actualizar
las cinco placas de cada nodo con firmware compatible antes de validar la
operación; no mezclar versiones anteriores dentro del anillo. No se requiere
borrar NVS ni cambiar particiones por estos cambios.

El retorno físico de un broadcast no acredita el consumo de cada callback de
aplicación. No se añadió un ACK de aplicación por cada tabla en cada placa.
El ID físico sigue siendo de 8 bits: la correlación nueva no elimina una
colisión si una trama excepcionalmente retenida reaparece después de 256 envíos.

No se flashearon placas ni se operaron puertos serie. Los logs disponibles
no permiten atribuir cada reinicio observado a uno de estos defectos. Las
pruebas nativas no reproducen DMA eléctrico, RF, alimentación ni las colas de
los cinco schedulers reales.

Falta validar alternancias AP/Station bajo tráfico, pérdidas y restauraciones
repetidas de cada uplink, conservación de IP, salida y retorno a Internet y
estabilidad prolongada con carga concurrente. Conservar UART y ELF de la primera
placa que falle para distinguir panic, brownout y RESET recibido del anillo.
