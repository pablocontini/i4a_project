# Arranque sin enlace: Root y Grillo, 19/10/2026

La revisión posterior y una corrección con regresión nativa están en
[correccion-anillo-20261019.md](correccion-anillo-20261019.md). Se confirmó el
defecto de reenvío cuando se atasca la cola de entrega local; queda pendiente
verificar con hardware cuánto explica de las demoras de esta captura.

Actualización tras el cierre: la corrida terminó a las 06:35:59 de Argentina,
sin fallos reportados por el capturador. El central real de Root registró siete
verificaciones completas fallidas entre 06:33:01 y 06:35:56, todas con
`ready_mask=0x10 error=0x107`; sus intercambios terminaron con `ack_mask=0x0`.
La comparación siguiente está en [prueba-root-aislado.md](prueba-root-aislado.md).

Lectura parcial de `app/build/capturas/20261019-062637-751`, todavía en curso.
Se observaron registros entre aproximadamente 06:27:43 y 06:31:51 de Argentina.
No confundir esta corrida con los arranques anteriores no capturados.

## Evidencia

- `grillo_center=COM7` informa `board=5`, UUID `142B2FC05E7D`, hash ELF
  `014a06d3cfa7071bfa196a64d4258f347dabebad1975d2ddf911647e9ab1b36e`.
  Llegó a `app_main.ready`. Su comprobación de arranque alcanzó
  `ready_mask=0x1f` y registró `boot_traffic_resumed` a las 06:27:53.
- Grillo conserva `count=0 default=5`, sin subred propia ni salida aprendida
  hacia Root. Su secuencia de coordinación avanza: no se observa un bloqueo
  permanente del token en ese central durante el intervalo revisado.
- `root_center=COM5` informa `board=2`, es decir orientación Sur de ruteo;
  el central debe informar `board=5`. La etiqueta del monitor no determina
  el rol del firmware. Verificar si se conectó una periférica o si la placa
  física central está configurada con orientación incorrecta.
- COM5 tiene tabla vacía y múltiples `reply_broadcast_failed op=1`.
  Su coordinación avanza mucho más lentamente que la de Grillo y sus clientes
  AP oscilan entre cero y uno. La asociación de un cliente no acredita que
  haya completado sesión y provisión.
- No hay monitor de una periférica de Grillo ni del central lógico de Root
  identificado como `board=5` en esta corrida. Tampoco hay pings configurados.

## Qué permite concluir

Grillo no ha completado la provisión que habilita su subred y AP de usuarios.
En `components/routing/routing/src/home.c`, `on_provision` agrega su subred y
llama a `wl_enable_ap_mode`; eso explica por qué no alcanza con esperar una
conexión del PC a Grillo en este estado.

Los fallos de difusión en Root justifican investigar primero su anillo y su
barrera de arranque. No prueban por sí solos un cable defectuoso, una mezcla de
firmware ni una causa concreta de software. No se identifica aún la causa
raíz de la ausencia de provisión.

## Siguiente captura

Conservar los dos monitores actuales. Agregar el central real de Root con
una etiqueta nueva, por ejemplo `root_center_real`, una vez conocido su COM.
Confirmar `board=5` en su salida. Queda un cuarto monitor disponible para una
periférica de Grillo; no es necesario que ya haya conectado para observarla.
No reiniciar ni reflashear antes de conservar este estado.

## Incorporación del central real de Root

El usuario identificó el central físico de Root como COM8. Se agregó a esta
misma captura a las 06:32:26 como `root_center_real`; informó `board=5` y la
misma sesión de coordinación que la periférica COM5. No se reiniciaron placas.

La lectura posterior muestra:

- Root sigue con tabla vacía, `count=0 default=5`.
- A las 06:32:41 su central registra `request_broadcast_failed`, seguido de
  `ack_timeout`, `peer=0`, `ack_mask=0x0`, `broadcast_ok=0`, `drops=10`.
  Es una consulta interna de arranque, no una actualización solicitada por OTA.
- Al abrir COM8 se recibieron fragmentos repetidos con
  `ready_mask=0x10 error=0x107`. Al no conservar la cabecera completa, no
  asignarles una hora de generación ni tratarlos como muestras independientes.
- La coordinación de Root se mantiene en `sequence=6 completed=5` durante
  el intervalo 06:32:28 a 06:32:44; la de Grillo sí continúa avanzando.

Esto localiza una falla de comunicación interna en Root y una barrera de
arranque sin completar como explicación probable de que no habilite el
descubrimiento/provisión hacia Grillo. Hace falta distinguir bloqueo de
software, presión de colas y problemas físicos del anillo; no hay evidencia
suficiente para escoger una de esas causas ni recomendar otro firmware aún.

A las 06:33:01 llegó una línea completa y nueva del central de Root:
`event=boot_ring_probe ready_mask=0x10 error=0x107`. Confirma que su comprobación
de arranque termina por timeout y solo valida su central; no logra validar a
las cuatro periféricas. El código de `boot_probe()` requiere `0x1f` y éxito
antes de reanudar el tráfico de descubrimiento de vecinos. Grillo sí había
superado esa barrera en esta misma corrida.

## Comparación con Root aislado

Captura `root-aislado-20261019-063829-656`, iniciada a las 06:38:30.
Central COM8 y Sur COM5 confirman UUID Root `000000000000` y el mismo hash ELF
`48221379f6eb0679c640b9da7a4edfe1a4f04c2bd6573b1db099a24731c91061`.
El central terminó `wifi_ap.end` y arrancó sus diagnósticos.

La primera consulta es especialmente útil:

- A las 06:39:27 respondió Norte (`peer=0`), luego Sur (`peer=1`) y Este
  (`peer=2`), cada uno con `error=0x0 broadcast_ok=1` y unos 15 ms.
- A las 06:39:31 faltó Oeste (`peer=3`): timeout con `ack_mask=0x7`,
  `broadcast_ok=1`. La difusión física de esa solicitud sí retornó, pero
  no llegó la respuesta válida de aplicación de Oeste dentro del plazo.
- El resultado fue `ready_mask=0x17 error=0x107`: central, Norte, Sur y Este
  validados, Oeste no validado. No implica que Oeste estuviese apagado.
- En intentos posteriores tampoco se validaron las otras periféricas y
  aparecieron fallos de difusión: `ready_mask=0x10 error=0x107`.

Suponiendo Grillo y Ramona efectivamente apagados como indica el ensayo, el
fallo se reproduce sin incorporarlos. Esto justifica observar primero Oeste
de Root y verificar su arranque/firmware/colas. El primer timeout no prueba
por sí solo que esa placa sea la causa raíz; puede estar recibiendo el efecto
de otro bloqueo o haber entregado una respuesta tardía.

Se solicitó su COM para agregar `root_west` como cuarto monitor sin reiniciar
esta corrida. No se modificó firmware ni se intentó liberar la barrera a mano.

## Cierre de la prueba aislada: recuperación tardía

La captura terminó a las 06:51:21, sin errores del capturador. Grillo no produjo
bytes ni conexiones de monitor en esa corrida. Oeste se incorporó como COM9 a
las 06:46:55 y confirmó `board=4`, la sesión de coordinación de Root y un
uptime continuo de unos 481 segundos: abrir el monitor no registró un reinicio.
Recibía consultas y registraba `reply_broadcast_failed`.

**El último tramo obliga a corregir el diagnóstico de bloqueo permanente:**

- 06:48:22: todavía `ready_mask=0x10 error=0x107`.
- 06:48:24: `ready_mask=0x1f error=0x0`; el central validó las cinco placas.
- 06:48:24: central, Sur y Oeste programaron la reanudación.
- 06:48:29: los tres monitores registraron `boot_traffic_resumed`.
- Al terminar, la coordinación avanzaba con normalidad: secuencias cercanas a
  674–680, frente a las pocas circulaciones durante el atasco.

Root superó la barrera aproximadamente 569 segundos después del arranque de su
central, y reanudó tráfico a los 574 segundos. No cambió su sesión de
coordinación ni se observó un nuevo arranque en los tres monitores. No hay
marcas manuales suficientes para atribuir la recuperación a una acción física.

Conclusión: hay una degradación transitoria severa del intercambio interno,
reproducible en la prueba aislada, seguida de recuperación. La barrera de
arranque explica por qué el descubrimiento permanece suspendido durante ese
intervalo, pero no explica por sí sola qué atasca el transporte. No hay prueba
de que Oeste esté averiado ni de que todos los periféricos tengan firmware
distinto; el éxito `0x1f` exige hashes coincidentes según `collect_boot_status`.
Tampoco hubo clientes/pings de Grillo que permitan afirmar recuperación de
Internet en este ensayo.

En la revisión de código se identificó un riesgo que merece una reproducción
de software antes de cambiar firmware: `node_on_sibling_message` usa
`xQueueSend(..., portMAX_DELAY)` sobre una cola de diez mensajes, desde la tarea
que procesa el anillo. Su consumidor ejecuta callbacks que pueden esperar una
difusión de retorno. Bajo saturación, esto puede frenar las siguientes tramas
del anillo y producir timeouts encadenados. La captura actual no mide ocupación
de esa cola y no demuestra que ese haya sido el mecanismo de esta corrida.
No convertir esa sospecha en una atribución causal sin una prueba.
