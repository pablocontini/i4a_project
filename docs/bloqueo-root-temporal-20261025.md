# Bloqueo temporal de nuevos directos a Root, 25/10/2026

El usuario pidió repetir el caso tardío impidiendo el atajo que invalidó
el [intento anterior](alternativo-tardio-v3-20261025.md). Se implementó un
control diagnóstico limitado a la admisión de nuevos vecinos Root.

## Alcance

`NODE_OTA_ROOT_GUARD` (op 10) recibe cuatro bytes little-endian con una
duración entre 0 y 900 segundos. Exige origen host, offset cero y todos
los roles del nodo (`0x1f`); solo se aplica con aplicación lista, fuera
del arranque coordinado y de mantenimiento/recepción OTA. Cero cancela.
Las respuestas exitosas se cachean separadamente del corte de radio para
que un reintento no reinicie el temporizador.

Cada placa mantiene el plazo en RAM, con reloj real de uptime. No persiste
en NVS ni congela el plazo durante mantenimiento. Un reinicio lo elimina.
El UUID Root `000000000000` queda filtrado en la selección de candidatos;
el central también rechaza nuevas reservas, confirmaciones y activaciones
para impedir que un candidato en curso eluda el filtro local. Las reservas
previas en trámite no se convierten en enlaces nuevos mientras esté activo.

Los enlaces activos conservan sus renovaciones: el filtro no desconecta
por sí solo el principal. Los otros UUID, incluido Ramona, continúan
permitidos. Los intentos de aplicar este control al propio Root se rechazan.
El bloqueo no cambia direcciones, reservas persistentes ni prioridades.

`I4AGUARD event=applied` registra la aplicación en cada placa. Los snapshots
`I4ACAP event=root_guard remaining_ms=...` permiten comprobar su cuenta
regresiva. Llegar a cero restablece automáticamente la admisión normal,
incluso si el controlador o la PC dejaron de funcionar.

## Herramienta y procedimiento

`gateway/root_guard.py` consulta primero los cinco roles y exige estado
normal y firmware homogéneo. Solo informa éxito si todos confirman el
comando. Si falla una respuesta, algunos roles pueden haberlo aplicado:
no iniciar el corte; cancelar con cero y verificar, o esperar el vencimiento.
El host conserva raw UART, TX, ACK, estados y resultado en una carpeta nueva.

Ejemplo con central de Grillo en COM5 y los monitores de ese COM cerrados:

```powershell
python gateway/root_guard.py --port COM5 --seconds 600 --record-output app/build/guard-aplicado-NUEVA-CARPETA
# Tras terminar el ensayo y cerrar el controlador del central:
python gateway/root_guard.py --port COM5 --seconds 0 --record-output app/build/guard-cancelado-NUEVA-CARPETA
```

El corte de radio conserva su control separado de 1 a 300 segundos. Antes
de aplicarlo hay que confirmar un solo directo a Root, ausencia del lateral,
referencia de pings y suficiente tiempo restante del filtro. Durante el
corte se enciende Ramona y se documentan sus rutas y las del nodo ensayado.
Que exista un filtro no demuestra que el alternativo aparezca a tiempo:
los ciclos AP/STA y la señal siguen afectando el encuentro.

Para esta prueba solo se actualizan las cinco placas de **Grillo**. La
admisión entre nodos continúa en versión 2, compatible con Root y Ramona
ya instalados; el comando diagnóstico nuevo se dirige únicamente al anillo
de Grillo. El bloque de clientes puede cambiar tras el reinicio OTA si el
hogar adopta un proveedor distinto: se verifican las redes e IP actuales
antes del ensayo, sin fijar las del montaje anterior.

## Validación de implementación

Pasaron 108 pruebas C (34 de routing, 38 de vecinos, 7 Wi-Fi, 5 hooks y
24 OTA), compatibilidad de protocolo C/Python y 75 pruebas Python.
Las nuevas pruebas verifican renovación del principal, admisión de otro
hogar, rechazo de Root en trámite, vencimiento por reloj real, cancelación,
límites, rechazo de Root como destino de control, cache de reintentos,
ausencia de mantenimiento y fallo seguro ante ACK parcial o versiones mixtas.

La primera compilación ESP32 produjo `905936` bytes en
`app/build/link-capture/grillo-root-guard-20261025/` (no usar para el ensayo):

- ELF: `5b944dcf5db1bd9acfdac149314cdeadc94c2cfd08207e0f40b02c59064760db`.
- main.bin SHA-256: `256f2fdf3f2191c74f8cc9793547143cf4b30c96bb67e4410d774cc36db41f39`.
- sources.zip SHA-256: `82365399df907a43e53076ab9060a1c837738ffea917b6c51cbd71f7f8701d2a`.

Las cinco placas de Grillo verificaron la imagen antes de seleccionar el
arranque, a las 20:15:24. El arranque y recuperación coordinada finalizaron
a las 20:18:31, con una consulta adicional de los cinco roles: listos,
sin pausa ni recuperación, ELF esperado y partición `0x210000`.
La carga conservó las reservas NVS. Solo se actualizó Grillo; Ramona quedó
apagada por el usuario. `grillo/control/result.json` acredita la carga y
`ota-monitores/` captura los reinicios esperados de Este y Oeste.

Después se abrió `espera-descubrimiento/`, con los tres monitores de Grillo
y pings de Wi-Fi 3. El hogar conservó `10.148.0.0/14`; la PC, `10.148.0.2`.
Este está asociado a Root y Oeste en AP sin clientes. Se espera el paso
de Oeste a estación antes de aplicar el filtro y ordenar el corte.

### Fallo de validación dirigido detectado en hardware

La prueba breve de aplicación por 60 segundos no obtuvo ACK. El filtro
siguió en cero y no se provocó un corte. La evidencia queda en
`guard-prueba60/` y `guard-prueba60-console.log`. La causa fue exigir máscara
`0x1f` también al recibir en las radios, aunque el coordinador convierte
la solicitud en cuatro envíos con máscara central+radio individual.
La prueba anterior del handler no cubría esa transformación del transporte.

La corrección conserva la obligación de seleccionar todos los roles en la
entrada host y acepta las máscaras individuales en el receptor SPI. Una
prueba nueva pasa por el coordinador, encode/decode, callback real de
ingreso y handler, con estados de cache independientes para cada placa;
verifica cinco ACK, reintento idempotente y cancelación. No se consideró
exitoso el control por haber verificado el firmware ni por emitir el comando.

La imagen corregida está separada en
`app/build/link-capture/grillo-root-guard-v2-20261025/`, `906000` bytes:

- ELF: `f384af04c1f5a93ebfc954ad0a0065a5c5ea8edac098750951182f27632b6939`.
- main.bin SHA-256: `6c3d8f959099e24cefe63c6db69ffbe708138600491978dda5e0b536fd927c36`.
- sources.zip SHA-256: `21897a731797bd2c2c667161f0a8c0137d2cfc989e34642c9a5eec31f7eb9bfc`.

La segunda carga de las cinco placas de Grillo comenzó a las 20:25:47,
con registros independientes en `revision2/`. El ensayo queda condicionado
a verificar esa carga y los ACK de aplicación del filtro corregido.

La revisión corregida verificó las cinco imágenes antes de seleccionar el
arranque a las 20:32:17. La consulta independiente final de las 20:35:26
confirmó los cinco roles listos, sin pausa/recuperación, partición
`0x20000` y ELF `f384af04…32b6939`. Su `result.json` marca `verified=true`.

### Prueba breve corregida, sin corte de radio

`revision2/guard-prueba60/` conserva cinco ACK exitosos de op 10 en el
primer intento, aplicando 60 segundos. Los snapshots de Este y Oeste
mostraron la cuenta regresiva y luego cero automáticamente; el directo
de Este a Root se conservó. En la ventana 20:35:48–20:36:48 respondieron
60/60 probes al gateway, 60/60 a RPi y 58/59 a Internet. No hubo una
interrupción persistente. La pérdida aislada no se atribuye al filtro.

El controlador `cut_link.py` incorpora `--require-root-guard CARPETA`:
verifica cinco ACK documentados, plazo suficiente y cuenta regresiva
reciente en el central antes de cortar. Exige observación mayor al corte
para cancelar el filtro 15 segundos después de la restauración programada
de la radio y observar la reconexión posterior. Si aborta, intenta también
cancelarlo; el plazo en RAM sigue siendo respaldo si no logra comunicar.
Las pruebas verifican que una cuenta vencida, borrada, futura, insuficiente
o sin confirmaciones impide el corte, y que la cancelación se ejecuta tanto
en la secuencia normal como al rechazar las precondiciones.

`revision2/espera-ventana/` registra el modo AP libre de Oeste y los pings
mientras se espera su siguiente fase de estación. Ramona permanece apagada;
el corte controlado todavía no se envió en esta etapa.

Actualización posterior: el [ensayo con bloqueo](alternativo-tardio-con-bloqueo-20261025.md)
se completó. Los cinco roles confirmaron aplicar 600 segundos y cancelar
después del corte. No apareció un directo nuevo mientras el filtro estuvo
activo, pero Oeste no admitió el lateral durante el corte; la recuperación
por Ramona continúa sin validar. La siguiente preparación cambia el
principal ensayado a Norte y deja Este como radio de descubrimiento.

Captura del ensayo todavía en preparación. La evidencia de compilación,
actualización y monitores se conserva en la ruta indicada por
`app/build/guard-root-activo.txt`. No considerar validado el caso tardío
hasta observar la recuperación antes de restaurar el directo, por un
vecino y recorrido que efectivamente pasan por Ramona.
