# Corte de Este de Grillo con bloques reservados, 25/10/2026

Ensayo autorizado por el usuario, con cuatro monitores y pings ligados a
Wi-Fi 3. Todas las horas indicadas son de Argentina (UTC−3).

## Preparación

Puertos: COM5 central Grillo, COM7 Este Grillo, COM8 Oeste Grillo,
COM9 Este Ramona. Grillo ejecuta ELF
`4aec92bb1bc1781973a70eb544af0455a840102fc2cffe80e6824843f1c9ae97`;
Este Ramona ejecuta
`fe09b3f25e85e9bb98bcd46210722b6b0784dd41a079579452b3452c7e01ce03`.

Principal: Este Grillo a Oeste Root, canal 10. Alternativo ya asociado:
Oeste Grillo como AP y Este Ramona como estación, canal 2. Ramona sale a
Root por Norte. Clientes Grillo `10.148.0.0/14`, Ramona `10.116.0.0/14`.
La PC conserva `10.148.0.2`, gateway `10.148.0.1`, por Wi-Fi 3.

El arranque anterior de Ramona no se considera explicado: el usuario
señala que pudo ocurrir al cambiar cables. Este Ramona informa
`ESP_RST_POWERON` (`reset=1`). No se reprodujo ni se ordenó ese arranque.

Se cerró ordenadamente la vigilancia pasiva y se abrió evidencia nueva:
`app/build/capturas/corte-este-v3-20261025-192458-5867/`.
La ruta queda en `app/build/corte-este-v3-activo.txt`. `monitores/` registra
las tres radios y los pings; `control/` registra el central, los bytes TX,
los ACK y sus diagnósticos. No hay dos lectores del mismo puerto.

El controlador verificó los cinco roles de Grillo listos, sin recuperación
ni pausa de arranque y con un mismo hash. Después registró 35 segundos
de referencia y exigió al menos diez probes por destino y las últimas
cinco respuestas correctas de cada uno. Origen fijo `10.148.0.2` en Wi-Fi 3.

## Corte y observación

Se ordenó únicamente el corte de radio Este durante 60 segundos, sin OTA,
sin apagar placas ni SPI. Los ACK confirman recepción; el corte efectivo
queda acreditado por `I4ACUT event=cut_applied` a las 19:25:46.425.

El central pasó de salida por defecto 3 a 0 y luego a 4, por Oeste hacia
Ramona. La primera respuesta de Internet después de las pérdidas llegó
a las 19:26:05.002: aproximadamente 18,577 segundos después del corte.
RPi respondió a las 19:26:04.956, a los 18,531 segundos. El gateway local
continuó respondiendo. La recuperación ocurrió con el principal todavía
cortado; no depende de su restauración.

La restauración automática comenzó a las 19:26:46.509 y el firmware
confirmó radio restaurada a las 19:26:46.699. Entre corte aplicado y radio
restaurada transcurrieron 60,264 segundos según el reloj de la placa;
restauración iniciada a los 60,096 segundos. La asociación Wi-Fi posterior
se comprueba por separado: radio restaurada no significa enlace admitido.

## Resultado final

La conmutación por el alternativo previamente asociado funcionó. A las
19:27:05.627 el central volvió a `default=3`, con RPi e Internet por Este.
Este se asoció a Este de Root, BSSID `20e7c85aa46d`, canal 4: no volvió a
la misma radio de Root utilizada antes del corte. El cambio completo de
salida observado fue `3 → 0 → 4 → 3`.

No se modificaron los bloques de clientes. Grillo mantuvo su ruta local
`0a940000/fffc0000:5` y la de Ramona `0a600000/ffe00000:4`.

| Destino | Enviados | Respuestas | Pérdidas |
|---|---:|---:|---:|
| Gateway | 233 | 233 | 0 |
| RPi | 224 | 214 | 10 |
| Internet | 222 | 210 | 12 |

La interrupción principal incluyó nueve probes fallidos hacia Internet y
nueve hacia RPi. RPi tuvo además una pérdida antes del corte; Internet
tuvo tres pérdidas aisladas adicionales, a las 19:26:42, 19:27:38 y
19:28:44. No hubo otra interrupción persistente. Los conteos no tienen la
misma cantidad de envíos porque cada timeout prolonga el ciclo del probe.
Las últimas cinco muestras de cada destino fueron respuestas correctas.

La observación del controlador terminó a las 19:28:46. La consulta final
de los cinco roles de Grillo respondió en el primer intento: todos listos,
fase 0, sin recuperación ni pausa, mismo hash de firmware. Las tres radios
pasivas siguieron registrando hasta aproximadamente las 19:28:56.
Los cuatro monitores mantuvieron uptime creciente, sin evidencia de
reinicios durante el intervalo; esto no demuestra el uptime de las once
placas no monitorizadas. Las capturas se cerraron y liberaron los puertos.

El analizador local `analizar.py` produce `resultado.json`, con rutas,
identidades, regresiones de uptime y todas las interrupciones de pings.
Los tiempos son de recepción del host salvo las duraciones calculadas
a partir de `t_ms`. La recuperación de 18,577 segundos se mide hasta
recibir la respuesta ICMP; no representa una medición exacta del instante
interno en que convergió el enrutamiento.

Esta corrida valida un corte de 60 segundos con alternativo ya admitido
y conservación de bloques. No reproduce la incorporación tardía de un
alternativo inexistente al inicio, ni valida cortes repetidos o estabilidad
prolongada. El arranque previo de Ramona queda como incidente no explicado,
posiblemente relacionado con el cambio de cables según el usuario.
