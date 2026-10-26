# Alternativo tardío con bloqueo de Root, 25/10/2026

El usuario pidió repetir la recuperación tardía impidiendo nuevos directos
a Root. El [control temporizado](bloqueo-root-temporal-20261025.md) quedó
implementado y cargado en las cinco placas de Grillo, ELF
`f384af04c1f5a93ebfc954ad0a0065a5c5ea8edac098750951182f27632b6939`.
Root y Ramona conservaron sus versiones de admisión 2. Horas de Argentina.

## Primera corrida controlada: corte de Este

Evidencia en `app/build/guard-root-20261025-200718/revision2/ensayo-controlado/`.
El puntero de esa corrida es `app/build/guard-ensayo-activo.txt`.
Puertos: COM5 central Grillo, COM7 Este Grillo, COM8 Oeste Grillo y
COM9 Este Ramona, ausente al inicio. La PC mantuvo Wi-Fi 3, `10.148.0.2`.

Se esperó la fase de estación de Oeste (observada a las 20:45:17). Ramona
estaba apagada por el usuario. El único vecino de Grillo era Root por
Este. Se aplicó el filtro por 600 segundos: cinco ACK exitosos en el primer
intento, con cuenta regresiva visible en los monitores. El controlador
exigió una referencia de pings reciente y la cuenta activa y suficiente
del filtro antes de cortar. Su registro es `guard600/`.

### Secuencia

- **20:46:38.767:** corte efectivo de Este por 300 segundos. Grillo pasó de
  `default=3` a `default=0`, conservando solamente su bloque local.
- El usuario encendió Ramona después de la indicación y reconectó COM9.
  Se capturó su arranque y liberación de tráfico. No se considera un
  reinicio espontáneo.
- Ramona obtuvo salida propia por Norte a Root y Este quedó en AP sin
  clientes. Oeste Grillo permaneció como estación sin asociación.
- Durante todo el corte no se observó un vecino nuevo Root ni un lateral
  admitido con Ramona en Grillo. El filtro evitó el atajo, pero no logró
  que las radios descubrieran/asociaran el enlace previsto.
- **20:51:38.863:** comenzó la restauración automática; a las
  **20:51:38.941** se confirmó radio restaurada. Los tiempos de la placa
  indican 300,096 segundos hasta iniciar restauración y 300,174 hasta
  radio restaurada.
- **20:51:54.020:** las cinco placas confirmaron cancelar el filtro.
- **20:51:59.585:** el central ya muestra el bloque de Ramona por salida 3;
  Este Grillo se asoció con Este Ramona tras restaurarse. La salida por
  defecto aún era cero. Este enlace no existió durante la ventana del corte.
- **20:52:** Grillo recuperó la salida por Norte directo a Root, después
  de cancelar el filtro. La primera respuesta de Internet llegó a los
  **340,707 segundos** del corte; RPi a los **340,702 segundos**. La
  recuperación no ocurrió dentro del corte ni acredita el camino por Ramona.
- **20:53:39:** terminaron los 420 segundos de observación posteriores al
  ACK. La consulta final confirmó los cinco roles normales en el primer
  intento y el filtro cancelado. Después se cerraron los monitores pasivos.

### Resultado

| Destino | Enviados | Respuestas | Pérdidas |
|---|---:|---:|---:|
| Gateway | 486 | 486 | 0 |
| RPi | 318 | 148 | 170 |
| Internet | 318 | 148 | 170 |

El ensayo **no recuperó por Ramona durante el corte**. La falta de asociación
entre Oeste Grillo y Este Ramona precede a la selección de gateway; las
capturas sin logs de escaneo no determinan si la causa es visibilidad,
umbral RSSI, selección o rechazo de admisión. No atribuirla directamente
al algoritmo de recuperación de rutas ni considerar validado el caso.

El bloqueo y su cancelación sí se comprobaron en hardware. El controlador
terminó con `root_guard_cleared=true`, sin fallos. Se conservaron bloques
distintos: Grillo `10.148.0.0/14`, Ramona `10.116.0.0/14`. No hubo regresión
de uptime en los cuatro monitores después de sus respectivos arranques.

Al final, Grillo tenía Norte a Root y Este a Ramona. Esa topología permite
otra preparación: apagar Ramona, dejar Este libre como estación y cortar
Norte. Se solicitó mover el cuarto monitor de Oeste a Norte de Grillo y
apagar Ramona; la siguiente corrida queda pendiente de esa acción del usuario.
No hace falta otra carga de firmware para el cambio de montaje.

## Segunda corrida: corte de Norte, alternativa por Este

La captura preparatoria de las 20:58 terminó por duración a las 21:13.
Después de la confirmación del usuario de que Ramona estaba desconectada,
se abrió una captura nueva a las 21:52, sin usar los registros anteriores
como evidencia del estado actual. Carpeta:
`app/build/guard-root-20261025-200718/revision2/ensayo-norte-20261025-215205`.

Se confirmó COM7 como Este Grillo, libre en modo estación; COM8 como Norte
Grillo, asociado a Root con salida por defecto 1; COM9 ausente. Los pings
se originan en Wi-Fi 3, IP `10.148.0.2`. El central permanece en COM5 para
el controlador. Los cinco roles verificaron el hash `f384af04…32b6939`,
listos y sin pausa de arranque, y confirmaron el filtro de 600 segundos.

A las **21:53:07.474** el monitor de Norte registró `cut_applied` por 300
segundos (`t_ms=4715626`, restauración programada `5015626`). El controlador
exige referencia de conectividad y contador vivo del filtro antes del corte.
Se solicitó encender Ramona después de comprobar ese evento físico, con
restauración prevista alrededor de las **21:58:08**. A las 21:53:25 Grillo
tenía salida por defecto cero y Este seguía libre, con filtro activo.

El usuario confirmó el encendido a las 21:55:09; COM9 reapareció a las
21:55:00 y la identidad posterior confirmó Este Ramona, `board=3`, UUID
`20E7C85AA245`. Las primeras líneas de arranque contenían `role=2`; se usa
la identidad de diagnóstico para identificar la placa, no esa enumeración.

### Recuperación tardía comprobada

- **21:56:04–06:** Grillo incorporó el bloque de Ramona `10.96.0.0/11`
  por Este. El monitor confirmó asociación con BSSID `841FE8271E99`, canal 8.
  La salida por defecto aún era cero: asociación y recuperación de gateway
  son eventos distintos.
- **21:56:14.545:** Este Ramona ya mostraba salida por Norte y ruta
  `10.0.0.0/8` por esa salida. Había tenido una transición previa a salida
  cero al incorporar el enlace lateral.
- **21:56:51.955:** primera respuesta de RPi e Internet desde Wi-Fi 3,
  **224,481 segundos después del corte**. La siguiente muestra de rutas
  del central, a las 21:56:52.205, confirmó `default=3`, `internet_out=3`.
  El registro de vecinos identifica al par de salida 3 como Ramona,
  UUID `20E7C85AA245`, no Root.
- **21:58:07.649:** Norte registró `radio_restored`, tras **300,174 segundos**
  de corte medidos con el reloj del dispositivo. Internet había recuperado
  aproximadamente **75,7 segundos antes**. El filtro seguía activo durante
  la recuperación.
- **21:58:22.580:** los cinco roles confirmaron la cancelación del filtro.

Esta corrida sí verifica una alternativa que apareció después de perder
el principal, sin usar un nuevo directo a Root. La recuperación tardó casi
cuatro minutos desde el corte e incluye la espera de encendido, arranque,
descubrimiento y propagación de gateway. No interpretar ese total como
tiempo exclusivo del algoritmo de rutas. Entre incorporar el bloque lateral
y recuperar Internet transcurrieron unos 45–47 segundos: queda como dato
para analizar la latencia de propagación en ensayos posteriores.

Hasta las 21:58:36 no hubo regresiones de uptime en los tres ESP de Grillo
monitorizados ni en Este Ramona después del encendido solicitado. Esto no
equivale a monitorizar individualmente las quince placas.

### Cierre de la segunda corrida

El controlador terminó a las **22:00:07.880**, sin fallos, con
`root_guard_controlled=true` y `root_guard_cleared=true`. La consulta final
confirmó los cinco roles de Grillo listos, sin pausa ni recuperación pendiente.
Se cerraron también los monitores pasivos y se recalculó `resultado.json`.
No hubo regresiones de uptime en los cuatro monitores durante toda la corrida.

| Destino | Enviados | Respuestas | Pérdidas |
|---|---:|---:|---:|
| Gateway | 493 | 493 | 0 |
| RPi | 381 | 268 | 113 |
| Internet | 381 | 268 | 113 |

Además de la interrupción principal hubo una pérdida aislada posterior por
cada destino remoto. Las últimas cinco sondas de cada destino respondieron.
No describir el ensayo como ausencia total de pérdidas. Los tiempos de
recuperación son los de la primera respuesta observada por sondas periódicas.

A las **21:59:25** Grillo volvió a salida Norte y conservó el bloque de
Ramona por Este. Los bloques de clientes siguieron distintos: Grillo
`10.148.0.0/14`, Ramona `10.116.0.0/14`. La captura terminó y los USB quedaron
liberados; los nodos permanecen encendidos, sin corte ni filtro activo.
