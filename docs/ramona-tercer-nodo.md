# Variante Ramona para el tercer nodo

El hotspot de esta compilación es `ComNetAR - Ramona`; conserva las correcciones
de fix2. Versión: `redundant-link-20261008-ramona`. Bundle:
`firmware_bundle_ramona_20261008`. La imagen fix2 anterior de Grillo se conserva
en su bundle y no se escribió en el root ni en el primer nodo durante este cambio.

Al consultar el tercer nodo en COM5 se detectó un hash diferente entre central
y periféricos; las cinco placas respondían, pero mantenían `boot_paused=1`.
La barrera de arranque exige imágenes iguales dentro del anillo, por lo que
la actualización OTA normal no puede iniciarse con esa mezcla.

Se instaló por USB en el central COM5 el bundle completo de Ramona para 4 MB,
sin `erase_flash`. Esptool verificó los hashes de las regiones escritas y terminó
correctamente. Registro: `build/firmware/ramona-center-usb-flash.log`.

Falta instalar la misma imagen en norte, sur, este y oeste por USB individual.
No dar por terminado el cambio del nodo hasta que sus cinco placas tengan
el mismo hash y liberen la barrera. COM6 corresponde al root y no es un destino
de esta instalación.

Actualización: se completó la instalación USB individual en central, oeste,
este, sur y norte, usando COM5 según el usuario fue conectando cada placa.
Todas las escrituras finalizaron con código 0 y verificación de hashes.
Los registros de cada periférico están en
`build/firmware/ramona-{west,east,south,north}-usb-flash.log`.
El hash ELF esperado en las cinco placas es
`d08b8c2eb0d8e7bb2a3a2ab1e40770b43d869229990622eca84e684b70e95f7c`.
Queda pendiente verificar el arranque conjunto y la provisión del nodo,
mediante el USB del central. El root y el nodo Grillo no se actualizaron
durante estas escrituras individuales.

```powershell
python auxiliar/esp_rpi_flasher.py flash --port PUERTO_DEL_PERIFERICO --baud 460800 --flash-args firmware_bundle_ramona_20261008/flash_args
```

Los roles continúan definidos por sus pines. Después de instalar las cinco
placas, arrancar el nodo completo y consultar INFO mediante el USB del central.
El nombre del hotspot será visible una vez que el nodo reciba su provisión de red.
