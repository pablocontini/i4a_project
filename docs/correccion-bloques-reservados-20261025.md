# Reserva de bloques por identidad — 25/10/2026

Estado: corrección implementada, validada con pruebas C y compilación ESP32,
y cargada posteriormente en las quince placas. El [despliegue OTA](despliegue-bloques-20261025.md)
confirmó los hashes y primeras asignaciones distintas. El [corte posterior de Este](prueba-corte-este-v3-20261025.md)
validó la conmutación por Ramona, retorno al principal y conservación de bloques.
La admisión tardía de un alternativo sigue pendiente en hardware.
La etapa de corrección de código se realizó sin acciones serie;
la carga posterior fue solicitada explícitamente y tiene registros separados.

## Defecto observado y regresión

En el [ensayo de alternativa tardía](alternativo-tardio-20261025.md), Grillo
recuperó Internet por un enlace nuevo a Root conservando `10.96.0.0/11`.
Root volvió a ofrecer ese mismo bloque a Ramona por el puerto original. Los
dos centrales derivaron `10.116.0.0/14` para clientes y el retorno a Grillo
fue reemplazado por la ruta hacia Ramona.

`test_moved_block_is_not_redelegated_to_a_new_node` reproduce esa secuencia
con los callbacks C reales. Falló antes de conectar la reserva al handshake;
después verifica bloques diferentes y que Root conserva simultáneamente las
rutas de retorno a ambos clientes. Los registros están en
`app/build/bloques-uuid-20261025/routing-antes.log` y `routing-despues.log`.

## Comportamiento de la corrección

El central de cada nodo es el único escritor del registro de propiedad.
Una radio AP solicita una oferta para el UUID del vecino mediante el mismo
coordinador SPI que administra su admisión. Las solicitudes de todas las
radios se procesan en serie, fuera de los callbacks del anillo.

Una reserva pertenece al UUID lógico, no al puerto ni a la sesión TCP.
Reconexionar al propietario por otra radio devuelve su bloque anterior.
Liberar o expirar la reserva temporal del enlace no libera sus direcciones.
Para un nodo nuevo se prefiere el sector del puerto; si está reservado, se
elige otro sector disponible sin invadir el sector local de clientes.

Las reservas se guardan en NVS del central, namespace `block_leases`, clave
`owners_v1`. La escritura y el commit deben terminar antes de devolver la
oferta. Si una respuesta SPI se pierde, el reintento obtiene la misma reserva.
Si falla una escritura, se suspenden nuevas ofertas durante ese arranque:
un commit fallido puede haber dejado datos persistidos y no se reutiliza
su candidato. Un registro ilegible o de otra identidad también impide nuevas
ofertas sin provocar un reinicio automático ni borrar la información.

La inicialización global de NVS tampoco borra ahora toda la partición cuando
encuentra falta de páginas o un formato incompatible. Ese borrado automático
anterior invalidaría las reservas de hijos todavía activos. Se devuelve el
error de inicialización al arranque para conservar los datos; no se promete
operación normal de Wi-Fi con una partición que no puede inicializarse.

El saludo de admisión comunica el bloque que el vecino ya utiliza. Un
vecino aprovisionado conserva su bloque y no consume otra reserva por el
simple hecho de usar un enlace redundante. Si declara un bloque reservado
a otro UUID, se rechaza antes de ejecutar los callbacks de ruteo.

El handshake de ruteo usa la oferta reservada. Esta puede ser diferente de
la subred física de la radio; los enlaces punto a punto siguen usando `/30`.
También se comprueba que el bloque anunciado después de la admisión coincida
con el aceptado: si cambia, se cierra esa sesión para negociar de nuevo,
sin instalar su ruta. La comprobación cubre handshake y respuesta de gateway.

Los mensajes de distancia y ganador de gateway no sustituyen la provisión
inicial: se ignoran hasta tener un bloque válido. Una provisión retrasada
por SPI tampoco puede renumerar un nodo que conserva su bloque mientras
busca otra salida. Dos regresiones adicionales fallaron antes de estos
guardas y pasan después (`reintentos-antes.log` y `routing-despues.log`).

Con el registro inicial vacío, si Grillo recibe primero `10.96.0.0/11` por
Este de Root, seguirá siendo suyo al volver por otro radio. Una Ramona nueva
que llegue a ese mismo AP puede recibir, por ejemplo, `10.32.0.0/11`, si está
libre. El resultado depende de las reservas previas; no se fija una IP a un
nombre de hotspot.

Los centrales registran `I4ABLOCK ... event=offer accepted=... uuid=...
parent=... network=... durable_ready=...`. Un rechazo por almacenamiento
no debe interpretarse como fallo SPI ni motivar reinicios repetidos.

## Límites de reserva

Se conservan hasta 16 reservas históricas por coordinador. Para un mismo
bloque padre se usan sus sectores 1–4, 6 y 7; el 5 queda para clientes y
el 0 no se delega. Una tabla o espacio agotados rechazan nuevas ofertas y
mantienen las asignaciones existentes. No hay reciclado automático de UUIDs
ausentes, porque un nodo desconectado puede seguir usando sus direcciones.

La reutilización administrativa de reservas antiguas queda fuera de este
cambio: requiere comprobar que sus propietarios dejaron de utilizar esos
bloques. No borrar NVS ni `block_leases` para intentar reparar una falla
transitoria. La reserva persiste al actualizar firmware sin borrar NVS.

## Validación automatizada

Las suites ejercitan código C real con mocks de transporte o NVS:

- Movilidad conservando bloque, incorporación al AP original y retorno a
  ambos nodos; anuncios que intentan cambiar el bloque admitido.
- Persistencia y recarga, propietario incompatible, registro corrupto,
  fallos de apertura/escritura/commit y resultado incierto del commit.
- Respuesta SPI perdida, reintento idempotente y conservación de propiedad
  después de liberar o expirar la sesión.
- Solicitudes de dos radios en ambos órdenes, sin concesiones superpuestas.
- Agotamiento, prefijos inválidos, sector de clientes protegido y cambio de
  padre que no permite delegar el bloque propio ni la red de sus clientes.
- Saludo incompatible, oferta antes de activar la sesión, cambio de anuncio
  posterior, recuperación de gateway y prevención de ciclos anteriores.

```powershell
python tests/run_native_routing.py --cc app/build/tools/tcc/tcc.exe
python tests/run_native_neighbors.py --cc app/build/tools/tcc/tcc.exe
python tests/run_native_wireless.py --cc app/build/tools/tcc/tcc.exe
python tests/run_native_routing_hooks.py --cc app/build/tools/tcc/tcc.exe
python tests/run_native_ota.py --cc app/build/tools/tcc/tcc.exe
```

Las salidas quedan en `app/build/bloques-uuid-20261025/`. Una suite nativa
correcta no acredita todavía funcionamiento físico ni ausencia de pérdidas
SPI en la instalación.

Resultado: 34 pruebas de ruteo, 36 de vecinos/reservas/persistencia, 7 de
Wi-Fi, 5 de selección de interfaz y 22 de OTA: **104 pruebas C correctas**,
más la comprobación de compatibilidad del protocolo OTA entre C y Python.
Las compilaciones ESP32 conservan sus registros y la identidad del ELF en
el manifiesto de cada paquete.

## Paquetes y migración antes del próximo ensayo

La admisión pasa de `I4AN` versión 1 a **versión 2**. La versión anterior no
declara el bloque antes de la provisión y se rechaza; no hay una negociación
que permita volver silenciosamente al mecanismo inseguro. El nuevo RPC del
coordinador también requiere la misma versión en las cinco placas del nodo.

**Actualizar las quince placas: cinco de Root, cinco de Grillo y cinco de
Ramona antes de evaluar conectividad entre ellos.** Se puede usar OTA local
por USB al central de cada nodo, siempre que respondan sus cinco roles. La
mezcla de versiones de admisión interrumpe los enlaces Wi-Fi entre nodos
hasta terminar la migración. No usar ese intervalo para juzgar el failover.

Los paquetes finales se conservan separados de las imágenes de ensayos
anteriores, con `main.bin`, ELF, mapa, argumentos USB, `sources.zip` y hashes:

- Root y Grillo: `app/build/link-capture/grillo-bloques-reservados-v3-20261025/`.
  El mismo firmware reconoce el rol Root por la configuración física y
  conserva su AP `Internet4All_Root`; el nombre por defecto de vivienda solo
  corresponde al rol de hogar. Las credenciales guardadas en NVS no se borran.
- Ramona: `app/build/link-capture/ramona-bloques-reservados-v3-20261025/`.

Los `capture-manifest.json` identifican cada binario. Los paquetes de desarrollo
`grillo-bloques-uuid-*` y los paquetes anteriores sin `v3` son previos al
cierre de esta corrección y no son los indicados para el ensayo. El sufijo
`v3` del paquete incluye la conservación de NVS al fallar la inicialización,
además de los guardas de provisión retrasada y el saludo de admisión versión 2.

| Nodos | Tamaño de main.bin | ELF SHA-256 esperado |
| --- | --- | --- |
| Root y Grillo | 905040 bytes | `4aec92bb1bc1781973a70eb544af0455a840102fc2cffe80e6824843f1c9ae97` |
| Ramona | 905040 bytes | `fe09b3f25e85e9bb98bcd46210722b6b0784dd41a079579452b3452c7e01ce03` |

Se verificaron SHA-256 de binarios y archivos de fuentes, identidad del ELF
y correspondencia exacta de las fuentes archivadas con el código del workspace.
`app/build/bloques-uuid-20261025/validacion-final.json` y el archivo
`app/build/bloques-uuid-20261025-validacion.zip` conservan la validación y las
pruebas. `app/build/main.bin` no se regeneró con estas variantes: usar las
rutas de los paquetes finales.

Con los COM utilizados en la última captura y todos los monitores cerrados,
las cargas preparadas para la migración son:

```powershell
$Roles = 'center,north,south,east,west'
$Grillo = 'app/build/link-capture/grillo-bloques-reservados-v3-20261025/main.bin'
$Ramona = 'app/build/link-capture/ramona-bloques-reservados-v3-20261025/main.bin'

python gateway/flash_node.py --port COM5 --roles $Roles --image $Grillo
python gateway/flash_node.py --port COM7 --roles $Roles --image $Grillo
python gateway/flash_node.py --port COM8 --roles $Roles --image $Ramona
```

Los comandos no se ejecutaron durante la corrección. Antes de cada carga,
consultar `--status` en ese COM y confirmar que identifica el nodo correcto
y sus cinco roles listos. No borrar flash ni cambiar selectores. Si hay un
fallo de transferencia, aplicar la recuperación del actualizador y conservar
su salida, sin asumir que otra repetición terminó la carga.

La carga OTA incluye el reinicio coordinado de cada nodo y elimina su
provisión volátil anterior. Conservar su captura y comprobar en `I4ABLOCK`
y las tablas que las redes de clientes son distintas y que Root tiene las
rutas de retorno a ambas. No lanzar otro corte si no hay referencia estable
a gateway, RPi e Internet.

La repetición deberá verificar tanto la llegada tardía como el camino por
Ramona. Para el segundo caso hay que evitar el atajo directo a Root por
otro radio y confirmar que Ramona tiene un uplink independiente de Grillo.
Crear una carpeta de captura y un controlador nuevos que exijan los hashes
anteriores. Los controladores archivados de los ensayos previos comprueban
otras versiones; no modificarlos ni reutilizar sus carpetas como si fueran
la nueva corrida.
