# Tolerancia a pérdidas SPI, 19/10/2026

Diagnóstico posterior con las tres variantes de control instaladas: se encontró
que a Ramona le faltaba la ruta hacia sus clientes. La corrección, evidencia y
paquete más reciente están en
[ramona-ruta-clientes-20261019.md](ramona-ruta-clientes-20261019.md).

El usuario establece que el enlace SPI sin acondicionamiento sobre cable largo
perderá paquetes. La aceptación no exige CRC cero: se busca reducir descartes
y, sobre todo, que las pérdidas no reinicien placas ni dejen coordinación,
provisión o conectividad bloqueadas permanentemente. La identificación del
cableado ya está resuelta: `Central → Sur → Oeste → Norte → Este → Central`.
No se requiere cambiar ese cable para evaluar la recuperación del software.

## Evidencia disponible

En `grillo-integridad-estabilidad-20261019-113312-660`, con Root y Grillo
encendidos y Ramona apagada según el procedimiento, se capturaron 180,7 s:

| Destino | Perdidos / intentos, espera de 800 ms |
| --- | --- |
| Gateway | 16 / 180 |
| RPi | 54 / 180 |
| Internet | 53 / 180 |

No se observaron reinicios en los cuatro monitores ni cambios en las rutas.
El central de Grillo sumó 23 CRC incorrectos al copiar la recepción SPI, y
23 descartes posteriores. El tipo observado en esos descartes fue IP en 17
y control interno en 6; el encabezado de una trama dañada tampoco es confiable.
Este no sumó CRC y sí una trama corta. No aumentaron los descartes por buffers
agotados o colas llenas. Son datos de las placas monitoreadas, no del anillo
completo. Estos conteos no identifican cada ping perdido ni explican toda la
pérdida de extremo a extremo.

## Defectos reproducidos y corregidos

1. **Root abandonaba una provisión fallida.** `root.c:on_start` llamaba a
   `os_panic` y retornaba si `rs_broadcast` no confirmaba el anuncio inicial.
   En la implementación actual `os_panic` solo registra un error: no reinicia
   el ESP. Por tanto, este defecto no demuestra una causa de reinicios.
   Se reemplazó el abandono por provisión pendiente y temporizador de reintento
   de 5000 ms. Root conserva su gateway; no vuelve a crear la ruta por intento.
2. **El proveedor dejaba de anunciar una provisión perdida.** El forwarder se
   marcaba provisionado antes de difundir la red y descartaba el resultado.
   Ahora conserva un reintento pendiente hasta recibir el ACK del anillo. Solo
   reintenta mientras tiene peer conectado, red y función de proveedor local.
   Si el uplink cae, espera y retoma después de recuperar un uplink válido,
   usando la red y distancia actuales. No renumera la red en cada intento.
3. **Root olvidaba una respuesta de recuperación perdida.** El anuncio de
   `SIBL_NEW_GATEWAY_WINNER` fallido vuelve a quedar pendiente durante 5000 ms.
   Las solicitudes repetidas no postergan ese plazo.

Los temporizadores se procesan en los turnos existentes de routing. No hay
bucles de retransmisión, colas adicionales ni un intento por cada milisegundo
acumulado: un tick atrasado produce como máximo un intento por anuncio pendiente.
Los receptores ya provisionados ignoran la repetición del anuncio de provisión.

El cambio no modifica el formato SPI ni los mensajes de routing. El ACK
existente acredita el retorno físico por el anillo; no es una confirmación
individual de ejecución del callback de cada placa. Una cola de aplicación
llena sigue siendo un caso a considerar por separado. No se declara fiabilidad
absoluta a partir de estos reintentos.

## Validación

Las cuatro regresiones nuevas fallaron con el comportamiento anterior:

- pérdida de tres anuncios/ACK, incluida entrega parcial y posterior duplicado;
- pérdida de uplink con provisión pendiente y reconexión sin renumerar;
- pérdida de provisión de Root sin panic ni cambio de gateway;
- pérdida de respuesta de recuperación de Root sin postergación indefinida.

Con los cambios pasan los 22 casos nativos de routing, 104 casos nativos en
total y las 54 pruebas Python. Se conservan las pruebas anteriores de
recuperación de tokens, publicación pendiente, saturación, propiedad de buffers
y descarte de tramas corruptas. Ningún ESP se flasheó como parte de esta
modificación. Falta validar esta tolerancia nueva en las placas.

Ambas variantes compilaron con ESP-IDF 5.4.1. Los paquetes finales están bajo
`app/build/link-capture/`:

| Destino | Carpeta | ELF SHA-256 |
| --- | --- | --- |
| Grillo, sus cinco placas | `grillo-control-20261019/` | `1ff9eff34df3a4a6202de84d60cf6b3378582608fc21d7fbb07f3134ff9b6437` |
| Root o Ramona, cinco placas por nodo | `ramona-control-20261019/` | `3e6065c8d92a0044f1d8769091ed6de39c5cfe4db9abddee05b2f61b927da60d` |

Aplicación de 808304 bytes en ambas variantes. Se verificaron los cuatro
segmentos exportados y que el archivo de fuentes coincide con los cambios.
Ambos paquetes comparten el ZIP de fuentes SHA-256
`8d89055514f74cbc23a491fcd4557e3334280b918fe0a1e36ff9991bb177c3b0`.
Las carpetas de integridad anteriores permanecen conservadas.

## Criterio para las próximas pruebas

Medir pérdidas por ventana sin exigir cero. Inyectar pérdidas consecutivas de
control y comprobar que, cuando vuelve a pasar tráfico, la provisión y las
tablas convergen sin reinicio manual. Separar esa recuperación del descarte
normal de paquetes IP. Comprobar tanto salida como retorno hacia el cliente.

No agregar retransmisión ciega a cada trama IP: sin identificación y supresión
de duplicados puede multiplicar tráfico y agravar la congestión. Un mecanismo
de fiabilidad para todo el enlace necesitaría diseño y pruebas propios. Tampoco
se cambia ahora la frecuencia SPI ni se amplían colas sin evidencia de saturación.

La comparación de Ramona debe empezar a contar su ventana posterior desde la
incorporación efectiva en las tablas, no únicamente desde el encendido físico.
