# Continuidad del proyecto I4A: enlaces redundantes y estabilidad

Fecha de corte: 11 de octubre de 2026. Horarios de los ensayos: Argentina, UTC−03:00.

Este documento permite continuar el trabajo en otra PC, otra sesión de Codex o un agente como Copilot. Reúne el estado del código, las decisiones del usuario, la evidencia de pruebas y lo pendiente. No es una certificación de funcionamiento: la formación del enlace redundante está observada, pero la recuperación completa ante un corte todavía necesita una prueba física satisfactoria con la versión actual.

## 1. Punto de partida para el próximo agente

El objetivo es que una red de nodos I4A descubra enlaces redundantes sin crear enlaces duplicados al mismo vecino y que, al perder su conexión principal, conserve sus IP y recupere tráfico interno e internet por otro camino. Se trabaja con tres nodos: Root, Ramona y Grillo; cada uno contiene cinco ESP32.

Estado al cerrar este documento:

- Los tres nodos tienen instalada y verificada la corrección de coordinación v2. Las cinco placas de cada nodo coinciden en su hash ELF.
- El último montaje observado tiene los enlaces Root norte–Ramona sur, Root oeste–Grillo este y Ramona este–Grillo oeste. Los tres enlaces aparecen admitidos en ambos extremos.
- El teléfono conectado a Ramona tiene IP **10.52.0.4** y gateway **10.52.0.1**. El usuario informó que ambos pings respondieron; en el intercambio se interpretaron como RPi e internet. Conviene reconfirmar los destinos exactos antes de otro corte.
- No se realizó el nuevo corte en este montaje. El corte previsto corresponde al **sur de Ramona**, no al norte usado en ensayos anteriores.
- Ramona se observó funcionando después de desconectar los USB de la PC y conservar la alimentación externa. No quedó demostrada la causa de las fallas anteriores.
- Se agregó un modo de tiempos rápidos para desarrollo y se activó en el sdkconfig local. **Todavía no se compiló ni se flasheó ese cambio.** Las imágenes instaladas conservan los tiempos normales.
- Hay numerosos cambios sin commit y archivos nuevos sin seguimiento. Un clon del repositorio remoto no recupera por sí solo el trabajo de esta sesión.

Prioridad inmediata: conservar una referencia de funcionamiento, resolver cómo ejecutar y observar un corte sin alterar la alimentación ni reiniciar el nodo, y validar la salida y el retorno por Grillo. La revisión de velocidad queda para después de estabilizar la recuperación.

## 2. Repositorio, rama y traslado a otra PC

Repositorio de trabajo: `https://github.com/pablocontini/i4a_project.git`.
Referencia upstream: `https://github.com/CoNexDat/i4a_project.git`.

La implementación actual está en la rama `feat/redundant-link-discovery`, basada en:

```text
a0ccdf36e06638e1490fab3ced176abf6632f168
fix: prevent reciprocal duplicate links between logical nodes
```

Ruta de la implementación en la PC original:

```text
C:\Users\jp\OneDrive\Documentos\GitHub(t480)\i4a_project\.worktrees\redundant-link-discovery
```

La sesión también muestra otro directorio como cwd:

```text
C:\Users\jp\OneDrive\Documentos\GitHub(t480)\i4a_project\.worktrees\comnetar-stability-tools
```

**No confundirlos.** Los cambios descritos aquí están en `redundant-link-discovery`. Se incorporaron allí cambios pendientes de estabilidad; el worktree de origen y el repositorio del portal no deben sobrescribirse.

Otros worktrees existentes: `baseline-stable-tree`, `ota-only`, `packet-trace` y `single-redundant-link`. Son referencias anteriores o experimentos, no la implementación final descrita aquí. El usuario ya compartió un paquete anterior con su compañero; ese paquete puede ser anterior a coordinación v2 y a los tiempos rápidos.

El paquete de continuidad adjunto contiene una instantánea del árbol actual, los dos bundles de firmware instalados, documentos y evidencia seleccionada. No contiene `.git` ni el directorio genérico de compilación. Incluye explícitamente `app/sdkconfig`, que Git normalmente ignora.

Para continuar en otra PC:

1. Descomprimir el paquete en una carpeta nueva y leer este documento y el índice de archivos. La carpeta `proyecto/` permite inspeccionar y compilar sin depender del worktree original.
2. Si se necesita Git, clonar el repositorio, comprobar la presencia del commit base y crear una rama local desde ese commit. Copiar encima el contenido de `proyecto/`, conservando el `.git` del clon. Revisar `git status` y el diff antes de cualquier commit o merge.
3. No trasladar el archivo `.git` del worktree original: apunta al repositorio padre de esa PC y no es portátil.
4. Instalar ESP-IDF 5.4.1 y Python; regenerar la compilación en la PC nueva. No reutilizar `CMakeCache.txt` ni rutas absolutas del build original.
5. Identificar nuevamente los COM, los adaptadores Wi-Fi, las IP y las conexiones físicas. Son datos de montaje, no configuración estable del proyecto.

El manifiesto del paquete registra SHA-256 de los archivos incluidos. Los hashes identifican contenido, no resultados de pruebas.

## 3. Arquitectura y significado de las direcciones

Cada nodo lógico contiene un ESP32 central y cuatro periféricos: norte, sur, este y oeste. Los periféricos conectan nodos por Wi-Fi; el central ofrece el hotspot de usuarios. En Root el central ofrece la conexión hacia la RPi.

El anillo SPI físico informado por el usuario es:

```text
Central -> Sur -> Oeste -> Norte -> Este -> Central
```

El orden lógico de concesión del token de ruteo puede ser distinto. Que falle una solicitud del norte no identifica un supuesto cable directo central–norte: su broadcast recorre el anillo completo.

Hay dos numeraciones que no deben mezclarse:

| Orientación | Rol GPIO / I4ALINK / OTA | Orientación del ruteo |
| --- | --- | --- |
| Norte | 0 | 1 |
| Sur | 1 | 2 |
| Este | 2 | 3 |
| Oeste | 3 | 4 |
| Central | 4 | 5 |

El modo Root se determina por el hardware/configuración de la placa central; no se deduce solamente del nombre de la imagen.

Un nodo recibe inicialmente un bloque lógico `/n`, que distribuye entre sus módulos con prefijos `/n+3`. El handshake de un AP ofrece una subred lógica (`provided_network`). El DHCP de un enlace Wi-Fi usa además una subred de transporte `/30`. Las IP internas de SPI pertenecen a otro espacio, por ejemplo `192.168.0.10` para el norte.

Son tres conceptos diferentes. No usar una dirección DHCP `/30` ni la IP fija SPI como si fueran el bloque lógico del nodo. La aparición de una IP SPI como origen de telemetría durante una recuperación no prueba por sí sola una renumeración de los clientes.

## 4. Reglas acordadas e implementadas para admitir enlaces

La conversación con el responsable se recibió en `Video Project 1.txt`, originalmente en Downloads. El usuario aclaró que la IP a comparar es la base de la subred ofrecida. Las indicaciones de documentos se trataron como material de análisis; las decisiones de implementación se tomaron según la solicitud y aclaraciones del usuario.

### Primer enlace

El nodo todavía sin provisión acepta una oferta válida y canónica, con espacio suficiente para subdividir el bloque entre sus módulos. Este primer enlace no aplica el filtro de búsqueda redundante. La coordinación impide que dos interfaces lo provisionen al mismo tiempo.

### Nuevo enlace redundante

La Station compara la base y el prefijo de `provided_network` contra el bloque que el nodo recibió en su primera provisión. Deben cumplirse **ambas** condiciones:

- El prefijo ofrecido es mayor: por ejemplo `/14` es más específico que `/11`.
- La base de la subred ofrecida es numéricamente mayor que la base del bloque original del nodo.

La comparación de IPv4 es numérica, octeto a octeto o mediante su entero de 32 bits; no es una comparación alfabética de cadenas. Por ejemplo, `10.144.0.0` es mayor que `10.32.0.0`.

Ejemplo del montaje actual: Ramona recibió `10.32.0.0/11`; Grillo oeste ofrece `10.144.0.0/14`. La oferta es mayor y más específica, por lo que pasa ese filtro. Luego deben cumplirse también las reglas de radio, dirección, turno y vecino único.

Si no cumple cualquiera de las condiciones, se rechaza antes de entregar el handshake al ruteo y antes de completar la respuesta de Station. Se libera la sesión/asociación y el nodo no se renumera.

### Reconexión del enlace principal

La interfaz que dio la provisión inicial puede recuperar **exactamente la asignación original**. Es una excepción específica de restauración del uplink; no autoriza cualquier nueva conexión al Root ni cualquier oferta menor. Mantener esta excepción al revisar el filtro.

### Dirección, radio y vecino único

- Norte/sur se conectan con norte o sur. Este/oeste con este u oeste. No se exige únicamente N–S o E–W.
- Las orientaciones no son necesariamente direcciones geográficas. Los nodos pueden estar girados; las cuatro antenas están aproximadamente a 90 grados.
- Cada periférico usa un umbral mínimo RSSI fijo configurado por el usuario y persistido. Se admite una señal igual o superior al umbral; una inferior se descarta.
- El central coordina reservas y sesiones por UUID lógico mediante `neighbor_manager`. Un vecino no se identifica solamente por MAC, RSSI o SSID. Se impide un segundo enlace al mismo nodo mientras su reserva/sesión está activa.
- El filtro de RSSI puede ayudar a excluir lóbulos traseros, pero eso depende de medición y configuración. No se demostró que elimine siempre esos enlaces en una mesa con antenas integradas u omnidireccionales.

No afirmar que las máscaras por sí solas garantizan cualquier propiedad espacial o toda la conectividad de una malla arbitraria. La oferta de cierre C→Root del ejemplo Root→A→B→C se rechaza como nuevo redundante cuando no cumple el prefijo/base; la restauración de un uplink original es otro caso.

## 5. Coordinación de la búsqueda y modos de radio

`discovery_manager` concede un único turno por nodo a una interfaz libre. El central recibe peticiones en cola; su callback SPI no espera una respuesta ni ejecuta cambios de radio.

Cada turno identifica propietario, sesión de arranque y secuencia. Mensajes tardíos y liberaciones viejas no reemplazan una búsqueda posterior. Los límites normales conservados son:

| Operación | Tiempo |
| --- | --- |
| Espera AP antes de nueva búsqueda redundante | Aleatoria, 600–700 s |
| Intento local autorizado | Hasta 60 s |
| Reserva del central | 90 s, sin renovación |
| Cuarentena inicial del árbitro central | 90 s |
| Espera de respuesta SPI | 3 s |
| Reintento cuando el turno está ocupado, tras la espera AP | 1–2 s |

La provisión inicial usa el árbitro sin esperar los 600–700 s del ciclo redundante. Una placa pendiente de recibir su provisión no puede obtener otra asignación inicial si cualquier radio del nodo ya tiene una reserva o enlace.

`node_control` es la tarea que cambia AP/STA, gestiona vencimientos y ejecuta los cortes de prueba. La Station comprueba la vigencia del turno antes de escanear, después de un escaneo bloqueante y antes de asociarse. La desconexión Wi-Fi no dispara reconexión automática desde el callback.

La cancelación del worker de Station es cooperativa para no eliminar una tarea que conserva un mutex de RPC. Un intento fallido retorna a AP antes de liberar el turno. El mantenimiento OTA impide conceder o usar nuevos turnos; los plazos siguen siendo de tiempo real y no se reviven al terminar el mantenimiento.

La barrera de arranque confirma que driver y worker del modo están listos. No espera la asociación Wi-Fi ni la obtención de un turno.

## 6. Recuperación del gateway y rutas

La tabla conserva **un único `default_gateway` activo**. No se implementaron prioridades ni una lista de gateways de respaldo preparados. Un enlace redundante ya admitido aporta un camino que el protocolo existente puede usar al volver a buscar gateway.

Al perder un vecino se retiran las rutas de esa salida. Si era la salida por defecto, se invalida y comienza la recuperación. Se conservan el bloque del nodo, las subredes de sus módulos, el hotspot y las IP de clientes. Una provisión repetida no renumera un nodo ya provisionado.

Correcciones incorporadas:

- Las entradas vacías de `hag_networks` no se insertan como rutas `0.0.0.0/0`.
- El Root tiene DTR 1. Cada salto entre nodos suma uno; los cinco módulos de un nodo comparten el mismo valor. Elegir un gateway por SPI no añade otro salto entre nodos.
- Se compara el costo completo del candidato, incluido el salto al vecino. Candidatos iguales o peores no sustituyen una salida válida; se rechazan distancias inválidas.
- Las solicitudes que ya incluyen la subred del receptor se descartan para impedir que el recorrido vuelva sobre sí mismo.
- La búsqueda reintenta cada 30 s. El Root programa una respuesta 10 s después del primer pedido; nuevos pedidos no postergan esa respuesta.
- Sin ruta se devuelve una interfaz de descarte con `ERR_RTE`, usando salida `255`. Devolver `NULL` permitiría un fallback de lwIP a SPI que no representa una ruta válida.

La ida y el retorno deben cambiar de manera coherente. Conectar la radio lateral no demuestra que el default de Ramona ni la ruta de retorno del Root hayan cambiado.

Agregar prioridades y gateways de respaldo sería una ampliación adicional del modelo de selección y mantenimiento de rutas. Se discutió como alternativa, pero no se implementó. No presentarlo como parte de coordinación v2.

## 7. Correcciones de coordinación v2

La prueba controlada de PC falló y motivó revisar entrega de eventos, token de ruteo, tablas compartidas y servidor de sesiones. Los defectos se reprodujeron o identificaron en código; eso no prueba que sean la única causa física de los timeouts.

Cambios actuales:

1. **Colas de ruteo:** mutex y lotes separados para ejecutar eventos. Los eventos nuevos no desaparecen al limpiar un lote anterior. Una cola pendiente vuelve a pedir turno cada 3 s, incluso si la pérdida todavía no se procesó y no hay timer de recuperación activo.
2. **Token `sync` v2:** sesión de arranque y secuencia. El central reintenta la misma circulación cada 5 s. Un periférico que ya completó ese turno reenvía sin ejecutar dos veces su callback. Se descartan secuencias antiguas y sesiones ajenas; se agrupan solicitudes concurrentes.
3. **Publicación de tablas:** flags de cambios pendientes y reintento `ss_flush`. Un fallo de publicación conserva el turno mediante `sync_defer_completion`. Se vuelve a publicar antes de consumir más eventos. Los mensajes incluyen versión, propietario, sesión y secuencia. Cambios de un mismo propietario/turno todavía dependen del orden FIFO SPI.
4. **Servidor TCP:** una tarea supervisora reintenta red/socket/bind/listen/select cada 2 s. Una parada solicitada termina la tarea y los reintentos. Se evita quedar marcado activo sin listener después de un fallo transitorio.
5. **Diagnósticos:** cola no bloqueante y worker UART separado para `I4AROUTE`, sin imprimir sincrónicamente esas trazas desde callbacks SPI.
6. **Telemetría:** `peer_admitted` y `sample_age_ms`; muestras SPI normales cada 30 s y publicación UDP cada minuto.

SYNC y SHARED_STATE internos usan versión 2. **Actualizar las cinco placas de cada nodo juntas.** La sesión se establece con el arranque del nodo completo; no se cambia aceptando un grant de otra sesión. Un callback que no termina no se revoca para fabricar un segundo token.

Esta estrategia reintenta pérdidas sin crear dos secciones críticas simultáneas. No puede reparar un anillo físicamente interrumpido ni garantiza entrega si el transporte sigue fallando.

## 8. Portal, RSSI, configuración y otras mejoras

El portal, orientación de antenas, credenciales y RSSI provienen del repositorio local `i4a-2`, commit `9a89e2e5f25728eb6da7f139ef192a1ed84b2dda`. Se integraron componentes de esa fuente sin modificar el repositorio de origen.

- Portal de configuración: SSID `ComNetAR_Config`, dirección `http://192.168.4.1`.
- Modo orientación: SSID `Orientacion_Antenas`, dirección `http://192.168.50.1`.
- Cuatro umbrales RSSI en NVS, rango −128 a −1 dBm. El valor por defecto −128 prácticamente no filtra señales débiles.
- El paquete STARTUP v2 distribuye umbrales, potencias y modo a los periféricos. No mezclar con placas que usan STARTUP antiguo.
- Guardar cambios desde el portal conserva configuración y coordina el reinicio del nodo.
- Las credenciales y nombres personalizados del hotspot persisten. No borrar NVS para una actualización normal.

El usuario encontró `Header fields are too long` en Android después del login, además de `ERR_CONNECTION_RESET`. Se aumentó `CONFIG_HTTPD_MAX_REQ_HDR_LEN` de 512 a 2048 mediante defaults y configuración local. Después confirmó que pudo ingresar con usuario y contraseña. No hay evidencia de que todos los resets de conexión tengan esa misma causa.

Los defaults del proyecto incluyen tanto `components/sdkconfig.defaults` como `app/sdkconfig.defaults`. Se agregó selección de SSID doméstico mediante `I4A_HOUSE_SSID` y de versión mediante `PROJECT_VER` para generar variantes identificables.

Otras correcciones presentes en el árbol de estabilidad/ruteo:

- Desactivar modem-sleep con `WIFI_PS_NONE`; puede reducir latencia y aumenta consumo. No constituye una medición de caudal.
- El callback de entrada SPI entrega el `pbuf` a lwIP si se acepta y lo libera si la interfaz/cola rechaza el paquete, evitando fugas bajo carga con o sin reporte de errores de esp-netif.
- Reenvío físico del broadcast antes de encolarlo para procesamiento de aplicación; reduce una dependencia circular identificada. No quedó probado que resuelva todas las fallas del anillo.
- Evitar circulación de tokens de tick innecesarios cuando no hay trabajo/timers pendientes, conservando reintentos necesarios.
- Salida UTF-8 con reemplazo en el CLI OTA para que bytes de consola dañados no cancelen una actualización por un error de impresión.

## 9. Tiempos rápidos solicitados para desarrollo

El usuario pidió reducir las esperas en futuros flasheos para acelerar ensayos y restaurar los valores normales al terminar el desarrollo.

Se añadió `CONFIG_I4A_FAST_TEST_TIMING` en `app/main/Kconfig.projbuild`, con `default n`. La opción está activada en el `app/sdkconfig` local. También existe `app/sdkconfig.development` como fragmento para defaults de una configuración nueva.

| Tiempo | Normal | Desarrollo |
| --- | --- | --- |
| Espera aleatoria de búsqueda redundante | 600–700 s | 5–10 s |
| Espera inicial del emisor UDP | 300 s | 1 s |
| Intervalo de publicaciones UDP | 60 s | 5 s |
| Intervalo de muestras por SPI | 30 s | 5 s |
| Separación inicial por orientación | 60 s | 0,2 s |

Los tiempos comienzan cuando arranca la tarea/ciclo correspondiente. **No prometen un enlace redundante 5–10 s después de encender:** se conservan cuarentena inicial del árbitro, boot/OTA, provisión, turnos y tiempos de asociación. Los primeros mensajes pueden tener muestras pendientes.

Estado: cambio en fuente y sdkconfig, sin compilación ni validación física. Usar otra versión explícita al compilar el firmware rápido para no confundirlo con el ya instalado. Los defaults no reemplazan automáticamente un valor guardado en sdkconfig.

Para la versión final: desactivar la opción en `idf.py menuconfig`, verificar `# CONFIG_I4A_FAST_TEST_TIMING is not set`, recompilar y actualizar todos los nodos. El usuario pidió esta restitución; conservarla como pendiente explícito.

## 10. Firmware instalado y flasheo

Las imágenes de coordinación v2 se compilaron con ESP-IDF 5.4.1. Se verificaron los tres nodos por INFO después de OTA el 11/10/2026: cinco ACK por nodo, hashes coincidentes, `ready=1`, `boot_paused=0`, `pending_verify=0` y `recovery_pending=0`.

| Nodo | Bundle | Versión |
| --- | --- | --- |
| Ramona | `firmware_bundle_coord_v2_ramona_20261010` | `coord-v2-20261010-ramona` |
| Grillo y Root | `firmware_bundle_coord_v2_grillo_root_20261010` | `coord-v2-20261010-grillo` |

Hashes ELF completos informados por INFO:

```text
Ramona:
05f036c4372ae9f4b11513f63b59693819f4e8df9ef1917c1f8d6aa36da5ca60

Grillo y Root:
aa89562c87ddaaf594ab2541ca979c5faa56159f2301e713040ecc1d369a703d
```

El Root puede usar la variante de Grillo porque determina modo y SSID por hardware. No necesita compartir el nombre doméstico de Grillo. Ambas aplicaciones tienen 904.976 bytes; los slots actuales de placas con al menos 4 MiB tienen tamaño `0x1f0000`, con `ota_0` en `0x20000` y `ota_1` en `0x210000`. Estar en slots diferentes no significa ejecutar firmware diferente si el hash ELF coincide.

El último `build/firmware/main.bin` genérico de la PC original corresponde a Grillo/Root, no a Ramona. Además, ese build es anterior a los tiempos rápidos. Elegir siempre el bundle exportado o un build nuevo identificado. El hash ELF de INFO no es el SHA-256 del archivo BIN: el manifiesto del bundle registra este último por separado.

Consultar, sin escribir flash, con el central realmente conectado al COM elegido:

```powershell
python gateway/flash_node.py --port COM5 --roles center,north,south,east,west --status --verbose
```

Actualizar por OTA usando una imagen de aplicación elegida explícitamente. Este ejemplo supone estar en `proyecto/` del paquete de continuidad, con los bundles en la carpeta hermana `firmware/`:

```powershell
python gateway/flash_node.py --port COM5 --roles center,north,south,east,west --image ..\firmware\firmware_bundle_coord_v2_ramona_20261010\main.bin --verbose
```

En el worktree original los bundles están dentro de la raíz; adaptar la ruta en ese caso. Este comando no debe ejecutarse por leer el documento: primero comprobar identidad del nodo, energía estable, anillo completo y necesidad de actualizar. La OTA inicial requiere firmware compatible en todas las placas; si no existe, seguir el bootstrap USB de `docs/actualizacion_firmware.md`. Una actualización por OTA no exige subir imágenes a la RPi: la RPi puede actuar como flasheador si los ESP están conectados allí, pero desde esta PC se usa el central por COM.

Durante OTA no cortar energía ni abrir otra aplicación que use el mismo COM. El protocolo actual coordina mantenimiento, arranque y verificación de las cinco placas. Conservar logs y verificar INFO al terminar. No borrar NVS ni cambiar particiones en una actualización normal.

Un `No ACK from ...` no demuestra que todas las placas estén muertas: INFO consulta primero al norte y un fallo inicial puede impedir que se reciban los demás ACK. Un ACK prueba una respuesta del protocolo, no el funcionamiento del gateway ni del tráfico de usuarios.

## 11. Herramientas disponibles

| Archivo | Uso |
| --- | --- |
| `gateway/flash_node.py` | Estado y OTA coordinada a través del USB del central |
| `gateway/cut_link.py` | Corte temporal de una Station periférica, sin OTA ni reinicio |
| `gateway/capture_link.py` | Captura serial con marcas temporales, sin ordenar reset |
| `gateway/set_test_routes.ps1` | Agrega/retira rutas de prueba /32 en Windows |
| `gateway/monitor_link_pings.ps1` | Pings concurrentes a gateway, RPi e internet y CSV de rutas/resultados |
| `auxiliar/export_firmware_bundle.py` | Exporta imágenes y manifiesto a un bundle nuevo |

El corte de prueba usa operación USB 9. Duración válida: 1–300 s; restauración inmediata con `--restore`. Reintentos de la misma orden no prolongan el corte. Se verifica que las cinco placas están listas, inactivas para OTA y con la misma imagen.

```powershell
# Solo tras verificar el montaje actual y resolver alimentación/consola.
python gateway/cut_link.py --port COM5 --role south --seconds 120 --verbose
python gateway/cut_link.py --port COM5 --role south --restore --verbose
```

El ACK confirma que se encoló la orden, no que ya se produjo la caída. `node_control` detiene la radio elegida y su sesión; conserva SPI, las otras radios y el hotspot. Al vencer se reactiva la radio y permite una búsqueda con turno sin la espera AP larga. Restauración de radio no implica asociación instantánea.

Las herramientas seriales mantienen DTR/RTS desactivados al abrir; aun así, conectar físicamente USB o cambiar alimentación puede resetear alguna placa. No atribuir un arranque nuevo a un comando que nunca se envió ni asumir que enchufar USB siempre es inocuo.

En Windows se usó un adaptador Realtek USB, nombre `Wi-Fi 3`, índice 25. El Wi-Fi habitual Intel, índice 4, conserva internet de administración. El script de rutas requiere PowerShell administrador y agrega únicamente `10.255.255.254/32` y `1.1.1.1/32` mediante el gateway del adaptador de prueba, sin cambiar la ruta por defecto.

El monitor usa .NET Ping y revisa `Find-NetRoute` antes/después de cada ronda. Solo considerar filas con `RouteVerified=True`. No ata el socket a una interfaz: la verificación documenta la ruta elegida por Windows. Un cambio transitorio de ruta entre comprobaciones puede escapar. Los nombres, índices e IP deben detectarse de nuevo en otra PC.

## 12. Red y acceso a la Raspberry Pi

Última configuración conocida, pendiente de reconfirmar si se cambia de lugar/red:

| Elemento | Dirección / función |
| --- | --- |
| SSH administrativo | `rpi@192.168.0.114`, por wlan1 |
| RPi wlan1 | `192.168.0.114/24`, red de administración |
| Salida por defecto de RPi | `192.168.0.1` por wlan1 |
| RPi wlan0 | `10.255.255.254/30`, conectada al Root |
| Root central hacia RPi | `10.255.255.253` |
| Ruta de RPi hacia la malla | `10.0.0.0/8 via 10.255.255.253 dev wlan0` |
| Destino de telemetría | UDP `10.255.255.254:8000` |

Antes se usaron `192.168.0.113` y `192.168.103.128`. Son históricos. No reutilizarlos sin comprobar la red actual. La sesión no consiguió autenticación SSH automática; el usuario ejecutó las órdenes desde su consola. No pedir ni incluir contraseñas, claves privadas o cookies en el documento.

El receptor `/home/rpi/info_server.py` intenta bind a `10.255.255.254:8000`. Cuando wlan0 estaba DOWN y sin esa IP falló con `OSError: [Errno 99] Cannot assign requested address`. Cambiar el bind a todas las interfaces no restablece el enlace: primero recuperar wlan0 y la ruta.

El perfil NetworkManager existente para Root es `i4a-root`, UUID `33a237b6-a845-44f7-a920-16f855bc0c9a`. Se activó con:

```bash
sudo nmcli connection up uuid 33a237b6-a845-44f7-a920-16f855bc0c9a ifname wlan0
ip -br addr
ip route
ping -c 3 10.255.255.253
```

Si la red no aparece, revisar el AP central de Root y su arranque. No conectar la RPi a un lateral I4A para sustituir silenciosamente este montaje. Conservar wlan1 y el acceso administrativo.

Las capturas tcpdump funcionan aunque `info_server.py` no esté escuchando. En ensayos anteriores, al no existir receptor UDP, la RPi devolvió ICMP port 8000 unreachable; eso no equivale a un fallo del ping.

Captura útil:

```bash
sudo timeout 300 tcpdump -ni wlan0 -s0 -A 'udp dst port 8000' 2>&1 | tee ~/telemetria_sin_usb_ramona.txt
```

El archivo más reciente está en la RPi: `/home/rpi/telemetria_sin_usb_ramona.txt`. Solo se recibió en la conversación una parte de su salida, que se conserva como extracto en el paquete. No afirmar que se descargó el archivo completo.

Para trasladarlo desde una PC con OpenSSH:

```powershell
scp rpi@192.168.0.114:/home/rpi/telemetria_sin_usb_ramona.txt .
```

El signo `:` hace remoto el destino/origen. En un intento previo se omitió en un envío y el ZIP se copió localmente en lugar de llegar a la RPi.

## 13. Cómo leer los diagnósticos

`I4ALINK`: cambios AP/STA, búsquedas, turnos de descubrimiento, provisión, corte y transporte de telemetría.

`I4AROUTE`: `sync`, `routing`, `gateway`, `shared_state`, `server` y `session`. Incluye tiempo monotónico local de cada ESP; no comparar `t_ms` de placas diferentes como si compartieran reloj. La captura serial añade hora de la PC para correlacionar.

Eventos útiles: `peer_queued`, `peer_dispatch`, `loss_processed`, `grant_send`, `grant_forward`, `circulation_done`, `completion_deferred`, `table_publish`, `table_received`, `route_add`, `route_remove_output`, `table_state`, `request_send`, `request_received`, `response_received`, `root_response_scheduled`, `root_response_publish`, `listener_ready`, `listener_retry`, `admitted`, `renewed`, `renew_failed` y `released`.

Los `turn_*` de discovery no son el token de ruteo `RS_ROUTING`. El `table_state` actual contiene un campo de cliente de laboratorio fijo `client_10_52_0_3_out`. El cliente actual es `10.52.0.4`: ese campo no demuestra directamente su ruta. Ajustar o generalizar la sonda antes de atribuirle una salida.

`I4AOTA_DIAG`: ACK, timeouts, fase de intercambio y resultado de broadcast. `sent=0` o `broadcast_ok=0` indica un fallo informado por transporte, pero no descarta recepción parcial ni identifica un tramo físico específico.

Telemetría UDP actual:

```text
orientation, uuid, link, subnet, mask, channel, rssi,
rx_bytes, tx_bytes, peer_admitted, sample_age_ms
```

- `peer_admitted=1`: sesión inter-nodos aceptada y reserva activa en el momento de la muestra. En el central, cero no implica que el hotspot o la RPi estén desconectados.
- `sample_age_ms`: edad local de recepción de esa fila; −1 si todavía no llegó una muestra. No mide por sí solo el tiempo desde la caída del enlace.
- SSID propio en una radio AP no significa ausencia de vecino: consultar también `peer_admitted`, el otro extremo y las trazas.
- RSSI −127, SSID o contadores acumulados solos no acreditan salud actual de sesión ni ruteo. Ver muestras repetidas y edades.
- Una solicitud ICMP llegando a wlan0 de la RPi prueba la ida hasta allí; una respuesta saliendo prueba su emisión. No prueba recepción en el teléfono/PC ni todo el recorrido de retorno.

## 14. Ensayos y resultados acumulados

### Conectividad inicial y navegación

Se lograron DHCP, ping al gateway, ping a RPi y a `1.1.1.1`, y navegación desde hotspots de los nodos. En un intervalo hubo timeout hacia la RPi pese a funcionar internet; luego se capturaron solicitudes y respuestas a ambos destinos y el usuario confirmó respuestas.

Hubo lentitud importante: `example.com` por HTTP y HTTPS funcionó, mientras páginas con muchas imágenes cargaron muy lentamente. Se probaron clientes separados para reducir tráfico concurrente. Los CSV de ping antiguos tuvieron medias aproximadas de 909 ms en Ramona y 688 ms en Grillo; algunos exportan respuestas y repiten secuencias, por lo que no permiten deducir confiablemente pérdida total. Velocidad, caudal y carga sostenida siguen pendientes.

### Prueba de teléfono anterior

En un corte previo el usuario informó dos timeouts y luego continuidad del ping a RPi, con IP `10.52.0.2` conservada. Internet quedó sin funcionar durante un rato y después volvió. Ese resultado parcial no valida de manera suficiente la conmutación de ida y retorno ni invalida un ensayo controlado posterior que falló.

### Prueba controlada de PC del 10/10, antes de coordinación v2

Topología de ese ensayo: Root norte–Ramona norte; Ramona sur–Grillo sur; Grillo este–Root este. PC `10.52.0.3`, gateway `10.52.0.1`, rutas /32 verificadas por Wi-Fi 3. Ramona tenía la imagen anterior `0d2e3358...`; Grillo/Root, `4e042034...`.

- Referencia: gateway, RPi e internet respondieron 12/12 cada uno.
- Corte del norte de Ramona: aplicado a las 21:14:00.704 por 120 s, sin reiniciar el nodo.
- Durante el corte: gateway 60/60; RPi 0/60; internet 0/60.
- Reactivación de radio: 21:16:00.741. Readmisión del principal: 21:16:11.802.
- Primer envío de ping exitoso después del corte: 21:16:12.977, unos 132,27 s después de aplicar el corte.
- IP de PC y selección de rutas se conservaron. Recuperó después de volver el enlace original, no por un failover demostrado.

Conclusión de ese ensayo: conservación del acceso local e IP y reconexión original verificadas; conmutación redundante fallida. No atribuir todos los timeouts a NAT o DNS: el ping interno a RPi tampoco recuperó.

### Intermitencia posterior, sin corte nuevo

Una referencia siguiente falló y se suspendió el nuevo corte. La RPi capturó cuatro solicitudes a sí misma y cuatro a internet desde la PC, con sus respuestas saliendo. Después, sin cambiar rutas ni resetear por comando, ping.exe y el monitor respondieron. La ubicación de la intermitencia de retorno sigue sin demostrarse.

### Después de instalar coordinación v2, 11/10

Los tres nodos quedaron actualizados y la telemetría mostró conexiones principales y lateral admitidos. Luego hubo pérdidas de admisión en Root y fallas de broadcast en Ramona. La referencia de PC `10.148.0.2` dio gateway 5/5, RPi 0/5 e internet 0/5; no se ejecutó el corte previsto.

Root respondió INFO en sus cinco placas; Ramona falló al consultar el primer periférico, con `broadcast_ok=0`. Trazas de norte/este de Ramona mostraron `token_request ... sent=0` y un `grant_forward ... replay=1 sent=0`. El central también informó reintentos y estado sin default/salidas (`255`). Esto orienta la revisión hacia transporte/coordinación, sin demostrar un cable o una causa eléctrica concreta.

Algunas capturas UART tuvieron fragmentos repetidos, datos ilegibles y, en una captura del este, únicamente ceros. Una consola ilegible no permite concluir que el ESP está muerto. Correlacionar con alimentación, adaptador, baud, cable y otras evidencias.

### Última referencia sin USB de PC en Ramona

Se reinició Ramona para retirar los USB de PC y conservar alimentación externa. La captura compartida entre 19:55:48 y 19:57:41 muestra los tres enlaces admitidos en ambos extremos, muestras recientes y contadores que aumentan. El teléfono es ahora `10.52.0.4`; el usuario informó pings respondidos.

Esto es una referencia favorable y una nueva topología. No es una comparación controlada que pruebe que quitar USB corrigió el defecto: hubo reinicio y rearmados previos. Todavía no se midió recuperación bajo corte con la imagen v2 y este montaje.

## 15. Topología vigente, con identidades y rutas esperadas

No reutilizar los mapas anteriores sin revisar telemetría. Hubo rearmados y varias placas físicas cambiaron de nodo/rol; las MAC periféricas no permanecen asociadas siempre al mismo nodo.

| Nodo | UUID central | Bloque original | Hotspot / gateway |
| --- | --- | --- | --- |
| Root | `20E7C8599A25` | `10.0.0.0/8` | `Internet4All_Root`, hacia RPi |
| Ramona | `20E7C85AA245` | `10.32.0.0/11` | `ComNetAR - Ramona`, `10.52.0.1` |
| Grillo | `142B2FC05E7D` | `10.128.0.0/11` | `ComNetAR - Grillo`, `10.148.0.1` |

Los SSID laterales del Root usan sufijo lógico `000000000000`; su MAC central sigue siendo la indicada arriba.

| Enlace vigente | Estado en el extracto recibido |
| --- | --- |
| Root norte `4022D83D8489` – Ramona sur `841FE8271CB9` | Ambos admitidos; RSSI alrededor de −40/−32 dBm |
| Root oeste `A842E34BF829` – Grillo este `841FE82715A1` | Ambos admitidos; RSSI alrededor de −11/−13 dBm |
| Grillo oeste `841FE8272039` – Ramona este `841FE8271E99` | Ambos admitidos; RSSI alrededor de −34/−31 dBm |

Ramona: norte `10.36.0.0/14`, sur `10.40.0.0/14`, este `10.44.0.0/14`, oeste `10.48.0.0/14`, central `10.52.0.0/14`. La telemetría sale por transporte `10.32.0.2`.

Grillo: norte `10.132.0.0/14`, sur `10.136.0.0/14`, este `10.140.0.0/14`, oeste `10.144.0.0/14`, central `10.148.0.0/14`. La telemetría sale por transporte `10.128.0.2`.

El extracto muestra Ramona este como Station del AP Grillo oeste. La ruta esperada tras cortar Ramona sur es:

```text
Cliente 10.52.0.4 -> Ramona central -> Ramona este
-> Grillo oeste -> Grillo este -> Root oeste -> Root central -> RPi
```

Retorno esperado: RPi → Root → Root oeste → Grillo → Grillo oeste → Ramona este → hotspot de Ramona. El Root debe retirar/actualizar la ruta hacia el bloque original de Ramona `10.32.0.0/11`, pasando del norte al oeste cuando corresponda.

Son rutas esperadas para comprobar; la telemetría de radio no acredita que ya estén instaladas.

## 16. Alimentación y acceso físico: cuestión pendiente

El usuario informó:

- Los 5 V y GND de todos los USB están unidos y también llegan por UTP al central.
- La alimentación externa entra por USB a la placa de periféricos.
- Para diagnóstico se habían conectado también USB de PC al central y a un periférico.
- No tiene adaptadores que bloqueen únicamente VBUS y conserven datos/GND.

Hay GND común; no está demostrado un problema por falta de masa. Unir VBUS de USB y una alimentación externa puede introducir alimentación cruzada o modificar las condiciones eléctricas, pero no se midió ni demostró que cause los fallos de SPI. No convertir esa hipótesis en diagnóstico definitivo.

El usuario quiere evitar otra espera larga y continuar sin reiniciar Ramona. La propuesta de apagar solo Root norte cortaría el enlace principal sin apagar Ramona, **pero todavía no confirmó que esa placa tenga alimentación independiente**. Apagar o retirar una placa completa puede interrumpir el anillo SPI del Root; no equivale a cortar solo la radio y puede invalidar la prueba. No ejecutar esa alternativa sin comprobar que conserva Root oeste, central y el anillo operativo.

Para un ensayo limpio, preferir el corte temporal por firmware cuando se pueda acceder al central con una solución de consola/alimentación adecuada. Otras opciones a evaluar: UART externo a 3,3 V sin conectar VCC, bloqueo específico de VBUS, o aislamiento RF real. Verificar pinout y diseño de la placa antes de conectar un adaptador; este documento no autoriza cortar cables o modificar hardware a ciegas.

Las últimas asignaciones conocidas fueron COM5 central Ramona, COM8 norte Ramona y COM7 este Ramona, pero el usuario desconectó USB para la referencia actual. **No hay COM vigente confirmado para central y sur.** En la nueva PC volver a identificarlos. No afirmar que se cambió una placa de orientación por solo cambiar de COM.

## 17. Pruebas de software y límites

Resultados registrados antes del cambio de tiempos rápidos:

- 37 pruebas Python de herramientas OTA/bundles/corte aprobadas.
- Pruebas nativas de ofertas, búsqueda, portal/RSSI, STARTUP, vecinos y entrada netif aprobadas en sus revisiones documentadas.
- Simulación con el código real de `sync` y `shared_state`: pérdidas en cada uno de los cinco saltos, entrega parcial con resultado de envío fallido, grants duplicados/antiguos, tabla fallida que retiene turno, solicitudes agrupadas y mantenimiento.
- Prueba del servidor real: falla inicial de bind, reintento de listener y parada solicitada.
- Suite nativa OTA/protocolo aprobada; incluye pruebas de control de corte.
- Windows bloqueó la carga de la DLL de ruteo con `WinError 4551` (Control de aplicaciones). No se desactivó ni eludió esa política.
- El usuario ejecutó en la RPi el paquete corregido `i4a_validacion_coord_v2_20261011_v2.zip`: aprobaron coordinación, servidor, 14 casos de recuperación y la prueba del hook sin ruta.

Los ZIP sin el sufijo final `_v2` tuvieron problemas de empaquetado; utilizar el paquete corregido. Algunos documentos anteriores todavía mencionan suites pendientes en Linux; quedaron superados por esa ejecución.

Las pruebas nativas sustituyen SPI/Wi-Fi por proveedores simulados. Verifican decisiones y recuperación del protocolo bajo fallas inyectadas; no certifican caudal, alimentación, DHCP, DNS, NAT ni entrega física en placas. No repetir pruebas extensas para una edición documental ni presentar el cambio de tiempos como compilado.

Comandos de referencia desde la raíz del proyecto en Linux con GCC/Clang:

```bash
python3 -m unittest discover -s tests -v
python3 tests/run_native_coordination.py --cc cc
python3 tests/run_native_server.py --cc cc
python3 tests/run_native_routing.py --cc cc
python3 tests/run_native_ota.py --cc cc
```

Consultar los argumentos de cada script para TinyCC; sus opciones no son idénticas. No asumir que un comando para GCC se puede copiar sin ajustes a Windows.

## 18. Herramientas de compilación y reproducción

Versión de ESP-IDF usada: **5.4.1**, objetivo ESP32, particiones OTA de 4 MiB según `app/partitions.csv`. En la PC original:

```text
IDF: C:\esp\v5.4.1\esp-idf
Python: C:\Espressif\tools\python\v5.4.1\venv\Scripts\python.exe
ESP_ROM_ELF_DIR: C:\Espressif\tools\esp-rom-elfs\20241011
```

Se usaron CMake 3.30.2, Ninja 1.12.1, toolchain Xtensa esp-14.2.0_20241119 y TinyCC local para determinadas suites. Esas rutas son información de la PC original; instalar/activar las herramientas propias en otra PC.

En una consola con el entorno ESP-IDF correctamente activado, desde la raíz del proyecto:

```powershell
# Revisar opción de tiempos, flash y configuración antes de construir.
idf.py -C app -B build/firmware menuconfig

# Ejemplo para una nueva imagen de desarrollo de Ramona.
idf.py -C app -B build/firmware -DPROJECT_VER=dev-fast-20261011-ramona '-DI4A_HOUSE_SSID=ComNetAR - Ramona' build

# Exportar a un destino nuevo; el exportador no sobrescribe bundles.
python auxiliar/export_firmware_bundle.py --build-dir build/firmware --output firmware_bundle_dev_fast_ramona_20261011
```

Para Grillo/Root usar su variante/versión explícitas y exportar a otra carpeta. Conservar los bundles instalados como referencia. No reutilizar el mismo directorio de exportación. Comprobar la opción efectiva en sdkconfig y en el header generado, la versión y el tamaño antes de flashear.

Dependencia de las herramientas de serial: `gateway/requirements-ota.txt` (pyserial). El PDF se generó con ReportLab y fuentes Unicode embebidas; esos paquetes no son dependencias del firmware.

## 19. Riesgos técnicos que siguen abiertos

1. **Transporte del anillo:** los nuevos diagnósticos muestran fallos informados de broadcast en Ramona. No se identificó el tramo ni se separó definitivamente hardware de bloqueo de tareas/colas.
2. **Confirmación de broadcast:** `components/internal/ring_link_internal/broadcast.c` despierta al emisor al regresar un broadcast del mismo dispositivo, sin correlacionarlo explícitamente con el ID del envío pendiente. Además compara el contador de `ulTaskNotifyTake` con `pdTRUE` (1). Son puntos para auditar con pérdidas/duplicados/retornos tardíos; no se corrigieron ni se probó su ocurrencia en esta falla física.
3. **Presión en las colas:** el reenvío previo al procesamiento ayuda, pero el posterior encolado puede bloquear la tarea receptora. Verificar bajo carga y evitar aumentar colas o generar nuevos tokens sin justificarlo.
4. **Sesión lateral:** confirmar admisión/renovación activa y cambios de tablas en ambos extremos durante todo el corte, no solo una muestra previa.
5. **Retorno:** medir si el Root y Grillo actualizan la salida hacia `10.32.0.0/11`; conservar captura de respuestas y recepción del cliente.
6. **Reinicios parciales:** versión/sesión del token presupone arranque coordinado del nodo. Una pérdida de energía de una sola placa puede dejar sesiones distintas o romper SPI; no tratarla como un corte puramente inalámbrico.
7. **Carga de desarrollo:** publicar más frecuentemente aumenta trabajo SPI/UART/UDP. Validar que acelera observación sin introducir nuevas fallas. Los tiempos rápidos no se instalaron todavía.
8. **Calidad documental:** varios textos históricos tienen acentos sustituidos por `?` o conclusiones anteriores a coordinación v2. No reconstruir resultados actuales a partir de esos textos sin fecharlos.

## 20. Próximos pasos y criterio de aceptación

1. Reconfirmar montaje, energía, firmware y los tres enlaces admitidos, conservando la IP del teléfono y datos móviles desactivados. Obtener el archivo completo de la última telemetría de la RPi.
2. Resolver el acceso al central y a la placa del enlace principal sin unir fuentes de alimentación ni reiniciar como parte del corte. El usuario prefiere evitar otra espera; no dar por disponible un corte físico independiente.
3. Antes de cortar, verificar referencia: gateway `10.52.0.1`, RPi `10.255.255.254` e internet `1.1.1.1`. Si falla la referencia, diagnosticarla y no producir otro corte.
4. Capturar en paralelo pings del cliente, ICMP/telemetría en RPi y trazas de pérdida, token, gateway y tablas. Si se usa PC, configurar/verificar rutas solo en el adaptador de prueba y no alterar la conexión administrativa.
5. Cortar únicamente Ramona sur por un intervalo conocido. Verificar la caída efectiva y que Root oeste–Grillo este y Grillo oeste–Ramona este siguen operativos.
6. Medir interrupción y recuperación de ida/retorno por el lateral **antes de restaurar el principal**. Confirmar IP y gateway del cliente iguales, sin reconectar Wi-Fi ni renovar DHCP; comprobar también HTTP/DNS cuando el ICMP se estabilice.
7. Restaurar el enlace, observar readmisión y repetir en condiciones controladas. Registrar un caso sin candidato: intento vence, radio vuelve a AP y otra interfaz puede obtener turno.
8. Compilar y validar el modo rápido para próximos flasheos, con versión nueva. Documentar sus resultados aparte de la imagen actualmente instalada.
9. Después de validar recuperación, estudiar rendimiento local RPi y WAN por separado, clientes individuales y carga concurrente.
10. Antes de entrega final, volver a tiempos normales, validar, ordenar los cambios y preparar commit/revisión con aprobación según el flujo del usuario.

Criterio de aprobación: existe un único enlace por vecino lógico; una sola interfaz libre busca por nodo; los intentos fallidos vencen sin dejar turnos indefinidos; se recuperan tráfico interno e internet con ida y retorno; todas las IP/subredes pertinentes se conservan; el resultado se observa durante el corte y no solo al volver el principal. La demora se mide, no se promete a partir de simulaciones. Las prioridades de gateways no forman parte de este criterio ni de la implementación actual.

## 21. Documentos y evidencias para consultar

| Archivo / ubicación | Alcance y vigencia |
| --- | --- |
| `docs/continuidad-proyecto-20261011.md` | Estado consolidado y pendientes de este traslado |
| `docs/correcciones-coordinacion-v2.md` | Implementación v2, hashes y despliegue; contiene indicaciones Linux antiguas seguidas por la actualización de aprobación |
| `docs/tiempos-desarrollo.md` | Opción rápida nueva; aún sin build ni instalación |
| `docs/prueba-recuperacion-pc-20261010.md` | Ensayo controlado anterior: failover falló |
| `docs/revision-sesion-lateral-rutas-20261010.md` | Defectos identificados antes de v2 y evidencia de intermitencia; varias limitaciones se corrigieron después |
| `docs/redundant-link-discovery.md` | Diseño/política inicial del 8/10; contiene estado de pruebas/red anterior y errores de acentos |
| `docs/gateway-route-recovery.md` | Modelo de rutas, HAG/DTR, hook de descarte y pruebas |
| `docs/neighbor-link-protection.md` | Reserva/admisión por vecino lógico |
| `docs/portal-rssi-integration.md` y `docs/portal-http-headers.md` | Portal, RSSI, STARTUP y headers |
| `docs/corte-temporal-enlace.md` | Operación de corte; interfaces del ejemplo pertenecen a un montaje anterior |
| `docs/actualizacion_firmware.md` | Bootstrap USB y OTA; verificar hardware/tamaño antes de usar |
| `docs/prueba-mesa-sin-direccionales.md` | Ensayo de laboratorio, sin validación de lóbulos reales |

En `build/firmware/` original están los CSV/logs: `pc-ping-baseline-20261010.csv`, `pc-failover-20261010.csv`, `pc-test-north-20261010.log`, `pc-test-cut-20261010.log`, `pc-route-review-baseline-20261010.csv`, `pc-monitor-crosscheck-20261010.csv`, `coord-v2-baseline-20261011.csv`, `coord-v2-east-before-cut-20261011.log`, `root-session-loss-20261011.log`, `ramona-east-session-loss-20261011.log`, `ramona-north-spi-20261011.log` y los tres registros OTA v2. El paquete incluye una selección en `evidencia/`; el índice señala qué archivos entraron.

Archivos de Downloads originales:

- `Chat_completo_enlaces_redundantes_2026-10-09.pdf`: exportación del chat hasta ese corte, anterior a v2.
- `Informe_pruebas_enlaces_redundantes_2026-10-10_corregido.pdf`: informe de pruebas anterior a v2, con acentos corregidos. El PDF sin `_corregido` tenía caracteres dañados.
- `i4a_validacion_coord_v2_20261011_v2.zip`: paquete Linux corregido y ejecutado por el usuario.
- Exportaciones `resultado_ping*.csv`: capturas históricas que deben asociarse a fecha, cliente y topología antes de comparar.

`build/firmware/validation.json` contiene información vieja del 8/10, no la validación acumulada actual. El paquete no usa ese archivo como certificado.

## 22. Texto para iniciar la nueva sesión

```text
Continuar el proyecto I4A de enlaces redundantes y estabilidad a partir de
docs/continuidad-proyecto-20261011.md y del paquete adjunto. Primero leer
el documento y comprobar el estado del árbol; no flashear ni resetear al
empezar. Hay cambios sin commit respecto de a0ccdf36e06638e1490fab3ced176abf6632f168.

Coord v2 está instalada en Root, Grillo y Ramona. La búsqueda rápida está
solo en fuentes/sdkconfig y pendiente de compilar. La formación lateral
se observó, pero el failover físico completo sigue pendiente.

Último montaje: Root norte-Ramona sur; Root oeste-Grillo este;
Ramona este-Grillo oeste. Teléfono en Ramona 10.52.0.4, gateway 10.52.0.1.
No reutilizar COM ni rutas de la PC anterior sin identificar hardware.

Conservar IP, portal/RSSI, reglas de oferta y un solo gateway activo.
Prioridad: referencia estable y ensayo de corte de Ramona sur con
diagnósticos de ida/retorno, resolviendo antes alimentación y acceso a UART.
Restaurar los tiempos normales antes de la versión final. Distinguir
hechos observados, hipótesis y pruebas simuladas; no declarar failover
aprobado solo por peer_admitted o por pasar suites nativas.
```
