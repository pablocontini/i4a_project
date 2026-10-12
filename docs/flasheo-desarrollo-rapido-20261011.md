# Actualización para ensayos con tiempos rápidos

Estado final: Ramona, Grillo y Root actualizados por OTA con tiempos rápidos.
Las cinco placas de cada nodo confirmaron el hash esperado, ready=1,
boot_paused=0, pending_verify=0 y recovery_pending=0. La actualización
no borró NVS ni cambió la tabla de particiones. Todavía no se realizó
el corte de validación con estas nuevas imágenes.

El bloqueo de Python que interrumpió el proceso se resolvió instalando un
entorno oficial firmado y permitido por Windows; Smart App Control sigue
activo. Ver [reparación del entorno](reparacion-python-20261012.md).

## Montaje confirmado por el usuario

- COM5: central Ramona.
- COM7: central Grillo.
- COM8: central Root.
- El usuario encontró desconectada la alimentación habitual del Root:
  dependía del USB conectado por un hub a la PC. Confirmó que volvió a
  conectar la alimentación habitual. Registrar este hallazgo sin atribuirle
  automáticamente todos los defectos de coordinación o los timeouts.

Antes de compilar, Ramona respondió INFO en las cinco placas con la imagen
anterior `05f036c4...`, `ready=1` y `boot_paused=0`. La consulta previa de
Grillo recibió el norte y terminó sin ACK del sur, este, oeste y central.
Esa consulta ocurrió antes de escribir. Después de reconectar la alimentación
del Root y reparar el entorno, las cinco placas de Grillo y las cinco de Root
respondieron INFO listas. Se actualizó un nodo a la vez y se verificó después.

## Imagen nueva de Ramona

- Bundle: `firmware_bundle_dev_fast_ramona_20261011`.
- Versión: `dev-fast-20261011-ramona`.
- Tamaño de main.bin: 904960 bytes; cabe en el slot `0x1f0000`.
- ELF SHA-256: `60b476dad292a526d38957c006e1692628b9a1941da0611076c02edcde9f19cb`.
- BIN SHA-256: `da0c17cc5c66b40a7e0102f275323820942ba1288f3736fcaed1e72504f26304`.
- Header generado: `CONFIG_I4A_FAST_TEST_TIMING=1`.
- Las 37 pruebas Python aprobaron antes del bloqueo del intérprete.

El bundle conserva bootloader, tabla de particiones, ota_data_initial,
flash_args y manifiesto, además de la aplicación. Se verificaron versión,
descriptor de aplicación, tamaño y hashes mediante PowerShell. Se exportó
por copia nativa porque Windows bloqueó el inicio del exportador Python.
Para OTA se utiliza únicamente main.bin con el actualizador coordinado;
no escribir los otros segmentos como parte de una actualización normal.

## Bloqueo de la ejecución

Este apartado describe el bloqueo inicial, ya resuelto; no es el estado
actual de la instalación ni del firmware.

Después de finalizar la compilación, Windows Control de aplicaciones empezó
a bloquear `C:\Espressif\tools\python\v5.4.1\venv\Scripts\python.exe`,
incluso con `-V`. Se confirmó el rechazo en el registro CodeIntegrity,
evento 3077: el ejecutable no cumple el nivel de firma de la política.
No se desactivó la política, no se modificó el ejecutable y no se escribió
firmware para intentar eludir el bloqueo.

La compilación inicial también seleccionó el entorno automático de
`C:\Users\jp\.espressif`, bloqueado. Seleccionar el entorno ESP-IDF existente
con IDF_PYTHON_ENV_PATH permitió completar el build y ejecutar las pruebas;
posteriormente ese entorno también quedó bloqueado al iniciar procesos nuevos.

Registros: `build/firmware/dev-fast-ramona-build.log`,
`dev-fast-python-tests.log` y `dev-fast-validation.json`.

## Variante de Grillo y Root

- Bundle: `firmware_bundle_dev_fast_grillo_root_20261012`.
- Versión: `dev-fast-20261012-grillo`.
- Tamaño de main.bin: 904960 bytes.
- ELF SHA-256: `0b50f08556a2c69b0ae3d72d654be58b46b913a94c59ab6dbe33b46949fd5d77`.
- BIN SHA-256: `fd8d202a42122b750ca187b751736f54e87bdf4fc9bc3a82eb0df10178238491`.
- Header generado: `CONFIG_I4A_FAST_TEST_TIMING=1`.

Root identifica su función por hardware y conserva Internet4All_Root aunque
use la variante doméstica de Grillo. Durante cada actualización se observó
la recuperación automática prevista del boot OTA antes de confirmar estados
finales; no se acortaron sus plazos para acelerar la prueba.

## Evidencia de instalación

En `build/firmware/`:

- `dev-fast-ramona-ota.log` y `dev-fast-ramona-installed.json`.
- `dev-fast-grillo-ota.log` y `dev-fast-grillo-installed.json`.
- `dev-fast-root-ota.log` y `dev-fast-root-installed.json`.
- `dev-fast-grillo-root-build.log` y `repaired-python-tests.log`.
- `dev-fast-validation.json`, actualizado con los tres resultados finales.

Cada log termina en verificación completa y FINAL_VERIFIED. Las 37 pruebas
Python también aprobaron en el entorno reparado. El build genérico actual
es Grillo/Root; usar el bundle de Ramona para ese nodo.

## Continuación de la prueba

Primero comprobar la referencia, obtener telemetría nueva e identificar
los enlaces principales y redundantes actuales. La topología y las IP pueden
cambiar al reiniciar por OTA; no reutilizar el sur de Ramona como objetivo
de corte sin confirmar que sigue siendo su Station principal.

La telemetría empieza un segundo después de iniciar la tarea, publica cada
cinco segundos y actualiza muestras cada cinco segundos. La espera aleatoria
de búsqueda redundante es de cinco a diez segundos. Esto no promete formar
el redundante cinco segundos después de encender: se mantienen cuarentena
inicial, provisión, asociación y límites de turnos.

Antes del corte comprobar gateway, RPi e internet; peer_admitted=1 en ambos
extremos de los tres enlaces; y anotar IP/gateway del cliente. Capturar
el cambio de rutas de ida y retorno durante un corte controlado.

Conservar tiempos normales como opción predeterminada y desactivar el modo
rápido antes de la entrega final. Los límites de turnos, la cuarentena de
90 segundos y las reglas de admisión no se redujeron.
