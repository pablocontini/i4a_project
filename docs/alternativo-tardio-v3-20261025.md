# Intento de alternativa tardía con bloques reservados, 25/10/2026

Estado: ensayo completado y capturas cerradas. El usuario eligió usar el firmware
actual y clasificar el resultado por el camino real, sin instalar exclusión
de nuevos directos a Root. Todas las horas indicadas son de Argentina.

## Preparación y control

Ramona se apagó completamente, desconectando también su USB. Root y Grillo
permanecieron encendidos. La [referencia previa](preparacion-alternativo-tardio-v3-20261025.md)
confirmó únicamente Este Grillo a Root y 60/60 respuestas por destino.

La nueva evidencia se guarda en
`app/build/capturas/tardio-v3-20261025-195435-6459/`, con puntero
`app/build/tardio-v3-activo.txt`. `monitores/` registra Este Grillo COM7,
Oeste Grillo COM8, Este Ramona COM9 (ausente al inicio) y pings ligados
a Wi-Fi 3, origen fijo `10.148.0.2`. `control/` registra el central de
Grillo COM5 y todos los comandos/ACK. Los cuatro puertos son exclusivos.

Las cinco placas de Grillo respondieron en el primer intento, listas,
fase 0, sin recuperación ni pausa y con ELF
`4aec92bb1bc1781973a70eb544af0455a840102fc2cffe80e6824843f1c9ae97`.
El controlador registró otros 35 segundos de referencia y comprobó al
menos diez muestras por destino, últimas cinco correctas y un solo
origen/interfaz antes de ordenar el corte. No modificó OTA ni NVS.

## Secuencia observada

- **19:55:25.782:** corte efectivo de Este durante 180 segundos,
  `I4ACUT event=cut_applied`. La restauración es automática y acotada.
- **19:55:26.544:** central con `default=0`, sin rutas remotas, solamente
  su bloque local `10.148.0.0/14`.
- **19:56:** el usuario confirmó Ramona encendida. COM9 volvió a entregar
  diagnósticos de su arranque. El aviso no es el instante exacto de
  alimentación. A las 19:56:29 se recibió `boot_traffic_resumed` en Este.
- **19:56:48:** Este Ramona ya refleja salida 1, sin asociación local por
  Este; posteriormente ofrece AP sin clientes. No se formó el lateral
  previsto Oeste Grillo ↔ Este Ramona durante la recuperación observada.
- **19:57:21.967:** volvió la primera respuesta de Internet tras las
  pérdidas, unos **116,185 segundos** después del corte aplicado.
- **19:57:22.108:** el central de Grillo mostró `default=1`, con el bloque
  local y una ruta `10.0.0.0/8` por Norte.
- **19:57:34.476:** snapshot del central confirma Norte en fase activa con
  vecino UUID `000000000000`, es decir **Root**; las otras tres salidas
  todavía no tenían vecino. La recuperación fue por un nuevo directo.
- **19:58:25.878:** comenzó la restauración automática de Este; el evento
  `radio_restored` llegó a las **19:58:25.999**. Los relojes de la placa
  indican 180,096 segundos hasta iniciar la restauración y 180,226 hasta
  radio restaurada. Eso todavía no implica asociación Wi-Fi.
- **19:58:46.110:** Este Ramona ya muestra el bloque de Grillo por salida 3.
  Después de restaurarse, Este Grillo se asoció a **Este Ramona**, canal 8,
  BSSID `841fe8271e99`; no volvió al mismo AP de Root. Grillo mantuvo su
  salida por Norte directo a Root. El lateral apareció después de recuperar
  Internet y después de restaurar la radio cortada, por lo que no explica
  la recuperación inicial.

No atribuir esa recuperación a Ramona. El caso tardío de mayor distancia
no queda validado por esta corrida aunque Internet haya vuelto antes de
restaurarse Este. El firmware actual permite el atajo que se había advertido.

## Estado final y límites

La observación del controlador terminó a las **20:01:25.833**. Las cinco
placas de Grillo respondieron en el primer intento a la consulta final:
fase 0, listas, mismo hash, sin recuperación ni pausa de arranque. Los
monitores pasivos terminaron a las 20:01:38 y liberaron sus puertos.

| Destino | Enviados | Respuestas | Pérdidas |
|---|---:|---:|---:|
| Gateway | 416 | 416 | 0 |
| RPi | 357 | 298 | 59 |
| Internet | 357 | 298 | 59 |

Cada destino remoto perdió 58 probes en la interrupción principal. RPi
tuvo además una pérdida aislada a las 19:59:44 e Internet otra a las
20:01:29. Las últimas cinco muestras de todos los destinos fueron correctas.
La primera respuesta de RPi tras el corte llegó a los 116,170 segundos;
Internet a los 116,185. Ambos volvieron antes de restaurarse Este, por
Norte directo a Root. Estos tiempos incluyen la búsqueda y asociación de
un enlace inexistente al cortar y no son comparables directamente con
los 18,577 segundos del ensayo con alternativo previamente asociado.

Grillo terminó con `default=1`, ruta local `0a940000/fffc0000:5`, bloque
Ramona `0a600000/ffe00000:3` y `0a000000/ff000000:1`. Ramona conservó
`default=1`, ruta local `0a740000/fffc0000:5`, bloque Grillo
`0a800000/ffe00000:3` y `0a000000/ff000000:1`. Los bloques de clientes
siguieron distintos, sin la duplicación que causó el fallo anterior.
La incorporación posterior de Ramona no produjo otra pérdida persistente.

Los cuatro monitores mantuvieron uptime creciente durante sus intervalos
de recepción. En Ramona la captura incluye el arranque pedido al usuario;
no hay regresión posterior de uptime. Esto no acredita el uptime de todas
las placas no monitorizadas. Tampoco determina la causa del arranque no
solicitado de la corrida anterior.

El analizador `analizar.py` preserva todos los cambios de rutas, identidades,
interrupciones de pings y regresiones de uptime en `resultado.json`.
Las horas son de recepción del host. Las duraciones exactas de corte a
restauración de radio se calcularon con `t_ms` de Este Grillo; una respuesta
ICMP no mide exactamente el instante interno de convergencia.

Conclusión: **recuperación por un nuevo directo a Root comprobada** y bloques
conservados al reincorporar Ramona. **Alternativo tardío por Ramona no
validado**. Para repetir ese caso de forma controlada hace falta impedir
temporalmente nuevos directos a Root en las radios libres de Grillo,
manteniendo disponible la asociación con Ramona y la restauración automática.
No se implementó ni cargó ese mecanismo en este intento.
