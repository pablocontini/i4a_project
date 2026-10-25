# Repetición del corte del enlace principal — 22 de octubre de 2026

Estado: ensayo del 22/10 completado y capturas cerradas. El corte reprodujo
una pérdida persistente de Internet: Grillo recuperó Root por Norte, pero
quedó sin ruta por defecto. La corrección del 23/10 pasa 27 pruebas de ruteo,
compila y quedó instalada y verificada en las cinco placas de Grillo.
La instalación se cerró en pausa por pedido del usuario. La repetición se
completó el 25/10 con conmutación por Ramona y recuperación de Internet en
16,890 segundos; ver el [informe del corte de Norte](prueba-corte-norte-20261025.md).

## Objetivo y antecedentes

Interrumpir durante 60 segundos la radio que proporciona la salida principal
de Grillo, manteniendo las cinco placas, el anillo SPI y el hotspot encendidos.
Comprobar recuperación por otro enlace mientras la radio sigue cortada,
conservación de la IPv4 del cliente y ausencia de reinicios inesperados.
Al vencer el plazo, verificar la reactivación de la radio y su eventual admisión.

El informe del 12/10, conservado en `feat/redundant-link-discovery`, registra
cuatro cortes temporizados: Este de Ramona y Oeste de Grillo. Esa orientación
no se reutiliza como dato actual: las primeras capturas del 22/10 muestran
Grillo con `rpi_out=3` e `internet_out=3`, correspondientes a Este.

La rama de estabilidad no contenía la operación USB 9 del ensayo anterior.
Se adapta el control a la rama actual, conservando su inicialización, SPI,
protocolo de ruteo y recuperación. No se incorpora el administrador de
descubrimiento de la otra rama. Se añade una copia local del registro de vecinos
a los diagnósticos para identificar los enlaces admitidos sin generar tráfico SPI.

## Montaje e inventario

| Monitor | Puerto confirmado | Firmware antes de la preparación |
| --- | --- | --- |
| Root central | COM5 | `3e6065c8d92a0044f1d8769091ed6de39c5cfe4db9abddee05b2f61b927da60d` |
| Grillo central | COM7 | `d34b614fd0ceb16cac1900ebac4b67d54d43028c3920a62689da1a5888b4cf9e` |
| Ramona central | COM8 | `a9e46318dffe7323fec278bfe58a2b7874eb66274280b5b64eb3716be56f810d` |
| Grillo Este | COM9 | Misma imagen que el central de Grillo |

Las consultas iniciales verificaron los cinco roles de cada nodo: misma imagen
dentro de cada anillo, `ready=1`, `boot_paused=0`, `pending_verify=0` y
`recovery_pending=0`. Los UUID observados son Root `000000000000`,
Grillo `142B2FC05E7D` y Ramona `20E7C85AA245`.

Se utilizan cuatro USB; no se requieren los hubs adicionales ofrecidos.
Wi-Fi 2 fue el adaptador inicial; el ensayo usa su reemplazo Wi-Fi 3 como
interfaz exclusiva de los pings. La Wi-Fi principal del PC sigue
disponible para su conexión habitual. No cortar el cable SPI ni la alimentación
de una placa: eso reproduciría una falla diferente.

## Registro de la preparación

Carpeta local del experimento:
`app/build/capturas/corte-enlace-20261022-101931-283093`.
`app/build/corte-activo.txt` guarda su ruta absoluta.

- `preparacion/`: estados UART, interfaces y rutas iniciales.
- `uart-preparacion/`: captura pasiva inicial de los centrales y luego Este.
- `uart-instalacion/`: Root, Ramona y Este mientras COM7 pertenece al actualizador.
- `ota-grillo-radio-cut.log`: órdenes, ACK, arranques y verificación del central
  durante la instalación.
- `espera-referencia/`: cuatro monitores después de la instalación, mientras
  se comprueba el cliente y la formación de un enlace alternativo.
- Las carpetas de ensayo contienen UART cruda, registros de líneas, eventos
  JSONL, metadatos, pings y marcas. Conservarlas completas.

El perfil de Windows de Grillo estaba guardado como red abierta, pero el AP
emitía WPA2. Se actualizó el perfil de Wi-Fi 2 a WPA2/CCMP con la clave indicada
por el usuario; el archivo temporal de configuración se eliminó. La clave no
forma parte de las capturas ni de este documento. Se observaron asociaciones
transitorias y una IPv4 automática `169.254.194.154`, sin gateway. Esa dirección
no es una referencia válida para el ensayo; se exige obtener una IPv4 del nodo
y comprobar los destinos antes de cortar.

El usuario confirmó que un teléfono conectado a Grillo, con datos móviles
apagados, recibe IP y tiene Internet. La reconexión física de Wi-Fi 2 no
resolvió su DHCP. Esto permite continuar el diagnóstico del adaptador, sin
concluir que el DHCP funciona para todos los clientes.

Se preparó `auxiliar/prepare_link_test_network.ps1`: guarda la configuración
original y permite usar temporalmente `10.116.0.250/14`, gateway `10.116.0.1`,
en Wi-Fi 2. La subred coincide con la ruta local observada de Grillo
(`0a740000/fffc0000`); no reutilizar estos valores si cambia la provisión.
El script rechaza sustituir una IPv4 válida o rutas preexistentes y comprueba
que la restauración corresponde al mismo adaptador.

Windows exige una consola como Administrador para modificar IP y rutas; la
sesión de ejecución actual carece de ese permiso. Desde la raíz del proyecto,
en una consola elevada:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File auxiliar/prepare_link_test_network.ps1 -Action Set
```

Se añaden rutas específicas a RPi e Internet por Wi-Fi 2. La ruta por defecto
del ensayo tiene métrica alta para conservar la preferencia por la conexión
habitual de la PC. Los pings fijan además su origen y exigen `WeakHostSend`
deshabilitado. No se modifica DNS. Antes de iniciar, comprobar dirección
`Preferred`, ausencia de conflicto de IP y respuestas de los tres destinos.
Una prueba con IP fija evalúa el tráfico y su recuperación; no acredita una
solución del problema DHCP del adaptador.

Al terminar, ejecutar en la misma consola elevada:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File auxiliar/prepare_link_test_network.ps1 -Action Restore
```

El respaldo está en `app/build/wifi2-link-test-backup.json`; conservarlo hasta
completar la restauración. El script retira únicamente sus rutas/IP y recupera
DHCP y métricas originales.

### Alternativa: reemplazar el adaptador USB de la PC

A las 11:18 (Argentina), el usuario ofreció otro adaptador para reemplazar
Wi-Fi 2. La inspección previa encontró DHCP y métrica automática habilitados,
la dirección `169.254.194.154` y ningún respaldo de configuración temporal.
La IP fija propuesta no está aplicada. Se prioriza probar el adaptador nuevo
con DHCP, conservando los nodos y los cuatro monitores encendidos.

Retirar solamente el adaptador Wi-Fi USB actual, conectar el nuevo y asociarlo
a `ComNetAR - Grillo`. Registrar su nombre real en Windows: puede aparecer
como Wi-Fi 3 u otro alias. Actualizar `--interface` con ese nombre y verificar
IP, gateway, `WeakHostSend` y respuestas antes de iniciar la referencia.
No ejecutar el script de IP fija sobre el adaptador nuevo sin revisar antes
su configuración. La captura `espera-referencia-2` registra la propuesta;
la propuesta antes del reemplazo, cuyo resultado se detalla a continuación.

### Wi-Fi 3: referencia válida obtenida

A las 11:20 se verificó el reemplazo por un Realtek RTL8188EU, interfaz
`Wi-Fi 3`, índice 25. Recibió por DHCP `10.116.0.5/14`, gateway `10.116.0.1`.
La dirección está en estado `Preferred`, `WeakHostSend` y `WeakHostReceive`
deshabilitados. Conserva su métrica de interfaz 5000 y ruta por defecto con
métrica 5000; no se modificaron mediante el script preparado para Wi-Fi 2.

Las comprobaciones con `ping -4 -S 10.116.0.5` obtuvieron cuatro respuestas
de cuatro intentos hacia cada destino: gateway, RPi `10.255.255.254` e Internet
`1.1.1.1`. `Find-NetRoute` con ese origen confirmó Wi-Fi 3 y gateway
`10.116.0.1` para los dos destinos externos. El nuevo adaptador resuelve el
impedimento de medición de la PC; no se aplicó IP fija.

La captura pasiva terminó ordenadamente a las 11:21. La captura del ensayo
`ensayo-grillo-1` comenzó a las 11:21:27 y el controlador `control-grillo-1`
inició la referencia a las 11:21:38. Ambos usan UTC del mismo PC.
El controlador comprobó los cinco roles listos y sin pausa de arranque.
Hasta esta referencia Grillo solo registra Root por Este. Se realizará el
corte para observar la reacción y recuperación; si no aparece otro enlace,
el resultado no acreditará conmutación por una ruta alternativa.

## Control de corte e instalación

Imagen diagnóstica de Grillo:
`app/build/link-capture/grillo-radio-cut-observado-20261022`, 901776 bytes,
ELF `89278f58071a28c01f74233ab3a57c4eaa7190d3f8082482154d3fedded8bd52`.
El paquete conserva el portal y el límite de cabeceras HTTP de 4096 bytes.
Se verifican el binario y las fuentes archivadas antes de cargar por OTA.

La instalación OTA concluyó correctamente. Una consulta independiente guardada
en `preparacion/grillo-post-ota-status.log` confirmó el hash `89278f58...` en
los cinco roles, `running=0x210000`, `ready=1`, `boot_paused=0`,
`pending_verify=0` y `recovery_pending=0`. Los reinicios registrados durante
esta fase pertenecen al procedimiento OTA y no al ensayo de corte.

Validación del cambio: 61 pruebas Python, la suite nativa OTA con el caso nuevo
de control y sus pruebas de protocolo, 24 casos de vecinos y seis de Wi-Fi
aprobados. La imagen completa compila con ESP-IDF 5.4.1.

En la referencia posterior a OTA, Grillo Este está asociado a Root Este,
BSSID `20e7c85aa46d`, con un RSSI observado de -19 dBm. El registro de Grillo
marca `output=3 phase=3 peer=000000000000`; las otras tres interfaces todavía
aparecen libres. Wi-Fi 2 llegó a asociarse al hotspot, pero continúa con IPv4
automática y sin gateway. La renovación de DHCP posterior a OTA agotó el plazo
sin contactar con el servidor. Se solicitó reconectar solamente el adaptador
Wi-Fi 2, manteniendo los nodos y sus cuatro USB. No se envió ninguna orden
de corte en estas condiciones.

La operación 9 acepta una única periférica y el central, duración de 1 a 300
segundos o cero para restaurar. Rechaza arranque incompleto, recuperación,
mantenimiento y modos especiales. La periférica exige Station provisionada,
asociación vigente y vecino admitido. El callback SPI solamente encola trabajo;
una tarea detiene normalmente el cliente, la búsqueda y Wi-Fi de esa radio.
El plazo se cuenta desde que termina la detención. Los reintentos idénticos
devuelven el ACK guardado y no prolongan el corte.

`I4ACUT event=cut_begin`, `cut_applied`, `restore_begin` y `radio_restored`
marcan la ejecución. El ACK confirma encolado; por sí solo no acredita la caída.
Restaurar Wi-Fi no garantiza asociación inmediata: la búsqueda y la admisión
mantienen sus reglas. Durante el ensayo no ejecutar OTA ni pulsar BOOT.

## Secuencia del ensayo

1. Verificar la instalación de las cinco placas de Grillo y el estado normal
   de los tres nodos. Separar sus reinicios OTA de la ventana de evaluación.
2. Conectar Wi-Fi 2 a Grillo, obtener IP/gateway y verificar que `WeakHostSend`
   está deshabilitado. Registrar IPv4, máscara, BSSID, gateway y rutas.
3. Registrar si existe otro enlace admitido hacia Root, directamente o por
   Ramona, y las tablas antes de elegir la radio. Si falta, clasificar el
   ensayo como corte y recuperación del principal; no atribuir a conmutación
   una recuperación que ocurra únicamente al volver la radio.
4. Iniciar los pings con origen fijo por Wi-Fi 2 hacia gateway, RPi
   `10.255.255.254` e Internet `1.1.1.1`, con timeout de 2000 ms. Verificar el
   recorrido del PC para impedir que la Wi-Fi habitual falsee el resultado.
5. Registrar al menos 120 segundos de referencia. El controlador exige diez
   muestras por destino y las últimas cinco satisfactorias antes de enviar corte.
   Rechaza errores recientes de ping, muestras con más de diez segundos de
   antigüedad, orígenes automáticos y cambios de interfaz o de IPv4.
6. Enviar el corte de 60 segundos; conservar 240 segundos de observación desde
   el ACK para abarcar caída, vencimiento y reconexión. Marcar cualquier acción
   manual. No intervenir durante la recuperación salvo para restablecer la radio.
7. Consultar los cinco estados al finalizar, cerrar las capturas ordenadamente,
   guardar resúmenes y analizar pérdidas, rutas, vecinos, IP y reinicios.

COM7 tiene un único propietario. Durante el ensayo, `capture_links.py` captura
COM5, COM8 y COM9 y los pings. `cut_link.py` abre COM7, lo registra completo y
ejecuta el control. Son cuatro monitores simultáneos, sin competir por el puerto.
Las dos líneas de tiempo se correlacionan por UTC del mismo PC; sus tiempos
monotónicos son propios de cada proceso. La UART registra recepción en el PC,
no el instante exacto del evento en la placa; usar también `t_ms` del firmware.

Comandos de referencia; las carpetas de salida deben ser nuevas:

```powershell
$Ensayo = (Get-Content app/build/corte-activo.txt -Raw).Trim()
python auxiliar/capture_links.py start --interface "Wi-Fi 3" --port root_center=COM5 --port ramona_center=COM8 --port grillo_east=COM9 --ping gateway=auto --ping rpi=10.255.255.254 --ping internet=1.1.1.1 --ping-timeout-ms 2000 --output "$Ensayo\ensayo-grillo-1" --notes "Grillo: corte temporizado de salida principal, referencia de 120 s"
```

En otro terminal, después de comprobar la captura y la interfaz:

```powershell
$Ensayo = (Get-Content app/build/corte-activo.txt -Raw).Trim()
python gateway/cut_link.py --port COM7 --role east --seconds 60 --baseline 120 --watch 240 --require-reference --record-output "$Ensayo\control-grillo-1" --marker-output "$Ensayo\ensayo-grillo-1" --verbose
python auxiliar/capture_links.py stop --output "$Ensayo\ensayo-grillo-1"
```

## Criterios de interpretación

Recuperación válida: gateway local accesible, respuestas de RPi e Internet
antes de reactivar la radio cortada y evidencia de un camino admitido diferente.
La misma IPv4 del cliente debe conservarse y no debe haber reinicios inesperados.
Que cambie la tabla local no acredita por sí solo salida y retorno de tráfico.

Informar intentos/respuestas por destino y fase; primer fallo, primera respuesta
posterior y primera serie de cinco respuestas. Distinguir el tiempo desde la
orden, desde `cut_applied` y desde el primer timeout. Registrar la reactivación
de la radio aparte de su asociación y de un eventual cambio de gateway.
Un retorno de respuestas solamente después de restaurar la radio no demuestra
conmutación al alternativo. No usar las cifras del 12/10 como resultado de hoy.

## Resultado medido del 22/10

Horarios de Argentina (UTC−3). La referencia del controlador comenzó a las
11:21:38.313 y la orden se envió a las 11:23:38.513. El corte quedó aplicado
a las 11:23:38.662; `restore_begin` apareció a las 11:24:38.747 y
`radio_restored` a las 11:24:38.837. El reloj de Este registra 60,096 segundos
entre corte aplicado y comienzo de la restauración. La reactivación de la
radio no equivale a una asociación: Este no volvió a admitir un vecino en
la ventana observada, mientras Norte ya estaba unido a Root.

A las 11:24:01.008 volvió la primera respuesta de la RPi: 22,346 segundos
después de `cut_applied` y 37,829 segundos antes de `radio_restored`.
La primera secuencia de cinco respuestas comienza en ese mismo instante.
El intervalo entre el primer timeout registrado y la primera respuesta fue
20,078 segundos; los timeouts se registran al terminar cada intento.

| Destino | Antes del corte | Radio detenida | Después de reactivar Este |
| --- | ---: | ---: | ---: |
| Gateway `10.116.0.1` | 131/131 | 60/60 | 196/196 |
| RPi `10.255.255.254` | 131/131 | 38/49 | 195/196 |
| Internet `1.1.1.1` | 131/131 | 0/29 | 0/100 |

Los valores son respuestas/intentos, clasificados por la finalización del
ping en el PC. La fase previa incluye unos once segundos de captura antes
del inicio de la referencia de 120 segundos. Los intentos fallidos tardan
más, por lo que producen menos muestras que los satisfactorios.

El controlador terminó su observación a las 11:27:38.579, consultó los cinco
roles y cerró COM7. Los otros tres monitores y pings se cerraron ordenadamente
a las 11:27:57.508, conservando unos 18 segundos adicionales. Internet no
respondió durante toda esa ventana posterior. No hubo intervención manual
en los nodos durante el corte o su restauración.

### Cambio de enlace y pérdida de la ruta por defecto

En Grillo se observa esta secuencia, coincidente en central y Este:

```text
Antes:           default=3 rpi_out=3 internet_out=3
Tras perder E:   default=0 rpi_out=0 internet_out=0
Con Root por N:  default=0 rpi_out=1 internet_out=0
Rutas finales:   10.116.0.0/14 -> central; 10.0.0.0/8 -> Norte
```

El registro de vecinos cambia de Este activo con Root a Norte activo con
Root (`output=1 phase=3 peer=000000000000`). La tabla de Root cambia su ruta
de retorno del bloque de Grillo `10.96.0.0/11` de Este a Norte. Esto respalda
la recuperación de ida y vuelta hacia la RPi por otro enlace físico.
No hubo un alternativo admitido antes del corte: se formó después de perder
el principal. No se demostró conmutación por Ramona.

La ruta `10.0.0.0/8` permite alcanzar la RPi; no cubre `1.1.1.1`. La falta
de `0.0.0.0/0` explica por qué vuelve la comunicación interna y sigue sin
salida a Internet. La IPv4 del cliente permaneció en `10.116.0.5/14`, con
gateway `10.116.0.1`; las pruebas de conectividad usaron siempre ese origen
y Wi-Fi 3. La configuración temporal de IP fija no se aplicó.

### Estabilidad y pérdidas SPI

No se detectaron retrocesos de uptime ni reinicios en los cuatro ESP
monitorizados. Los contadores CRC aumentaron en seis en el central de
Grillo y diez en el de Ramona; no aumentaron en Este de Grillo ni en el
central de Root. No aumentaron tramas cortas, falta de buffers, colas llenas
ni fallos de transmisión en los cuatro monitores durante este ensayo.
No se atribuye la falta de ruta por defecto a un CRC concreto.

Las consultas posteriores de los tres centrales obtuvieron los quince roles
con los hashes esperados, `ready=1`, `boot_paused=0`, `pending_verify=0` y
`recovery_pending=0`. Esas consultas no permiten descartar reinicios breves
en las once placas que no tenían monitor continuo.

## Causa reproducida y corrección preparada el 23/10

Una radio sin vecino conserva su distancia anterior a Root (`DTR=2`).
Cuando se pierde Este, la tabla compartida elimina la ruta por defecto.
La solicitud de buscar otro gateway no reinicia la distancia de Norte si
todavía no tiene un vecino, porque su callback retorna en ese estado.
Si esa solicitud SPI se pierde, también puede quedar la distancia anterior.

Al admitir después a Root por Norte, el handshake instala `10.0.0.0/8` y
ofrece una distancia de un salto: la distancia candidata del nodo vuelve
a ser dos. El código anterior solo instalaba gateway si la distancia local
era cero o la candidata era estrictamente mejor. El empate rechazaba la
instalación, aunque ya faltara la ruta por defecto.

La secuencia se reprodujo con los callbacks C reales, conservando el bloque
de red y obteniendo acceso a la RPi pero ninguna ruta a `1.1.1.1`. El DTR de
Norte no fue medido directamente en hardware: esta explicación es consistente
con las tablas capturadas y está verificada en la reproducción nativa.

La corrección en `components/routing/routing/src/forwarder/peer.c` acepta una
distancia igual cuando la tabla no tiene gateway. Si hay una salida
configurada, conserva la preferencia anterior. Los candidatos de mayor
distancia siguen las reglas previas. La misma decisión se aplica al handshake,
a actualizaciones DTR y a respuestas de gateway. No reprovisiona el bloque,
no reinicia el hotspot y no depende de recibir una solicitud SPI anterior.

Se añadieron tres casos a la suite nativa: radio nueva con distancia retenida
y solicitud SPI recibida o perdida; conservación de un gateway existente;
respuesta de igual distancia cuando falta el gateway, con reintento idempotente.
Antes del cambio fallaron los dos casos de recuperación. Después pasan las
27 pruebas de ruteo, incluidas las de pérdidas y reintentos existentes.

Imagen preparada: `app/build/link-capture/grillo-recuperacion-default-20261023`,
901824 bytes; ELF
`6b3772b240074f33dc41fd3e41a1830ca93d6b471b194edacf4f8fb76807a3d8`.
Compilación completa ESP-IDF aprobada. El paquete conserva las fuentes exactas
en `sources.zip`. Al terminar esa compilación todavía faltaban la instalación
y un nuevo corte; Windows solo mostraba puertos Bluetooth y Wi-Fi 3 estaba
desconectado. La instalación posterior se registra al final del informe;
la validación del corte continúa pendiente.

## Evidencia conservada y siguiente validación

En la carpeta del experimento se conservan:

- `ensayo-grillo-1/` y `control-grillo-1/`: los cuatro monitores, UART cruda,
  pings, marcas, metadatos y resúmenes sin errores del capturador.
- `resultado-ensayo-grillo-1.json` y `analizar-ensayo.py`: resultados y análisis
  reproducible de las dos líneas de tiempo originales.
- `preparacion/wifi3-pre-corte.json`, `wifi3-post-corte.json` y registros WLAN:
  interfaz, direcciones y rutas antes y después.
- `preparacion/*-post-ensayo-status.log`: los quince estados posteriores.
- `repro-ruta-default.py`, `.c` y `.json`: reproducción nativa del defecto
  realizada antes de cambiar el código.
- `preparacion/routing-antes-correccion.log`, `routing-despues-correccion.log`
  y `build-recuperacion-default.log`: pruebas y compilación de la corrección.

Para la validación siguiente, identificar nuevamente puertos y salida activa,
instalar la imagen corregida en las cinco placas de Grillo y verificar su hash.
Conectar Wi-Fi 3, comprobar DHCP y pings con origen fijo y crear carpetas nuevas
para repetir referencia y corte. Exigir respuesta de Internet antes de volver
la radio cortada, ruta por defecto válida y misma IP del cliente. Mantener la
captura anterior como resultado fallido; no sustituirla por la validación nueva.

Con COM7 confirmado como central de Grillo y los monitores liberados, la
actualización preparada se realiza con estos comandos:

```powershell
python gateway/flash_node.py --port COM7 --roles center,north,south,east,west --status
python gateway/flash_node.py --port COM7 --roles center,north,south,east,west --image app/build/link-capture/grillo-recuperacion-default-20261023/main.bin
python gateway/flash_node.py --port COM7 --roles center,north,south,east,west --status
```

La segunda consulta debe mostrar el ELF `6b3772b2...` en los cinco roles.
Estos comandos describen el procedimiento de instalación; la ejecución
real del 23/10 se registra a continuación. La próxima captura debe apuntar a la radio que sea realmente la
salida principal después de la actualización.

La carpeta completa del experimento se empaquetó localmente como
`app/build/capturas/corte-enlace-20261022-101931-283093-evidencia.zip`.
Incluye `archivos-sha256.json`, el informe, los registros y el parche de la
corrección. Los datos crudos permanecen en su carpeta original; no se
incluyen en Git junto al código fuente.

## Validación en hardware retomada el 23/10

El usuario reconectó el montaje con COM5=Root central, COM7=Grillo central,
COM8=Ramona central y COM9=Este de Grillo. Las consultas previas obtuvieron
los quince roles listos. Wi-Fi 3 recibió `10.116.0.2/14` por DHCP, gateway
`10.116.0.1`, con `WeakHostSend` deshabilitado.

Carpeta independiente:
`app/build/capturas/validacion-default-20261023-141527-630200`.
`app/build/validacion-default-activa.txt` conserva su ruta.
La captura `uart-ota` comenzó a las 14:15:44 de Argentina; el actualizador
posee COM7 y guarda sus intercambios en `ota-grillo-default.log`.

Aunque el usuario todavía no había observado el enlace redundante, la captura
anterior al reinicio OTA confirmó en el registro de Grillo dos vecinos activos:
Root por Este (`output=3`) y Ramona por Oeste (`output=4`, Station Este de
Ramona, AP Oeste de Grillo). La tabla incluye el bloque de Ramona por Oeste.
Este dato describe la topología previa: debe comprobarse otra vez después
de actualizar y no acredita todavía el resultado del corte con la corrección.

La carga de los 901824 bytes terminó sin reintentos DATA. Las cinco placas
verificaron la imagen, seleccionaron la nueva partición y reiniciaron.
El primer arranque informó `recovery_pending=1`; se conservó la alimentación
hasta completar el reinicio automático previsto a los 120 segundos y la
barrera de arranque. Estos reinicios pertenecen a la instalación OTA, no
a una prueba de corte.

El actualizador terminó con código cero y confirmó las cinco placas.
Una consulta independiente en `preparacion/grillo-post-ota-status.log`
verificó en todos los roles:

```text
elf_sha256=6b3772b240074f33dc41fd3e41a1830ca93d6b471b194edacf4f8fb76807a3d8
running=0x20000 ready=1 boot_paused=0 pending_verify=0 recovery_pending=0
```

Durante la instalación, el usuario pidió pausar la prueba antes de desconectar
la PC. Se respetó la pausa: no se inició referencia de pings ni se envió ninguna
orden de corte en esta validación. La captura cerró ordenadamente a las 14:25:21
aproximadamente, con resumen sin errores; el estado del experimento queda
`paused_by_user` y `cut_sent=false`.

Al retomar, comprobar puertos, estados y hash instalado antes de decidir si
hace falta otra actualización. Verificar la salida principal y el enlace
alternativo después de este arranque; la topología anterior a OTA no demuestra
que se haya restablecido. Luego iniciar una captura nueva con pings por Wi-Fi 3,
referencia de 120 segundos y corte temporizado de 60 segundos.
