# B?squeda coordinada de enlaces redundantes

Implementaci?n del 8 de octubre de 2026 en `feat/redundant-link-discovery`.
Worktree: `.worktrees/redundant-link-discovery`. Se copiaron deliberadamente
los cambios pendientes de `comnetar-stability-tools`; ese worktree y el repo
`i4a-2` del portal permanecen intactos. El firmware se identifica como
`redundant-link-20261008`; usar tambi?n el SHA-256 del bundle para cada ensayo.

## Admisi?n

La IP candidata es la base de `provided_network`, la subred l?gica ofrecida
por el AP en el primer handshake. Se compara contra la base del bloque que
recibi? el nodo en su primera provisi?n. Ambas condiciones deben cumplirse:

- El prefijo ofrecido es mayor que el prefijo del nodo.
- La base ofrecida es num?ricamente mayor que la base del nodo.

No se compara con el `/30` obtenido por DHCP ni con el `/n+3` de la placa local.
El primer enlace no aplica ese filtro de redundancia; valida una oferta
can?nica y con espacio suficiente para subdividirse. Una reconexi?n por la
interfaz que dio la provisi?n inicial admite exactamente su asignaci?n original:
restaurar el uplink no es una b?squeda de un nuevo enlace redundante.

La Station valida la oferta antes de entregar el handshake al ruteo y antes
de enviar su propia respuesta. Un rechazo libera la sesi?n y la asociaci?n
sin renumerar el nodo. La admisi?n existente por UUID, coordinada por el ESP
central, sigue impidiendo un segundo enlace con el mismo vecino.

El portal y los modos especiales provienen del commit
`9a89e2e5f25728eb6da7f139ef192a1ed84b2dda` del repositorio local `i4a-2`.
Cada interfaz usa su umbral RSSI fijo persistido. El supuesto de eliminar
l?bulos traseros depende de los valores que configure el usuario.
Ver [portal y RSSI](portal-rssi-integration.md).

## Turnos y cambios de modo

`discovery_manager` registra un ?nico turno en el central. Las peticiones
SPI se encolan: el callback no espera ni transmite respuestas. Propietario,
sesi?n de arranque y secuencia identifican cada turno. Mensajes tard?os y
liberaciones viejas no desplazan una b?squeda posterior.

| Operaci?n | Tiempo |
| --- | --- |
| Espera AP antes de buscar redundancia | Aleatoria entre 600 y 700 segundos |
| B?squeda autorizada en la interfaz | Hasta 60 segundos, desde el comienzo de la petici?n |
| Reserva del central | 90 segundos, sin renovaci?n |
| Espera de respuesta SPI | 3 segundos, desde antes del env?o |
| Cuarentena tras arrancar el central | 90 segundos |
| Reintento de un turno ocupado despu?s de la espera AP | Entre 1 y 2 segundos |

La diferencia entre reserva central e intento local deja margen para cerrar
una Station fallida antes de otorgar otro turno. Los plazos usan tiempo real:
no se resucita una b?squeda vencida al terminar OTA. Durante mantenimiento no
se conceden ni se utilizan turnos. La cuarentena de arranque tambi?n protege
frente a un perif?rico que no haya recibido el reinicio del central.

La provisi?n inicial usa el mismo ?rbitro, sin la espera AP de 600?700 segundos.
Una solicitud todav?a no provisionada se deniega si cualquier radio ya tiene
una reserva o un enlace: una provisi?n SPI atrasada no permite una segunda
asignaci?n inicial. Una interfaz ya provisionada puede buscar un tercer vecino
cuando est? libre.

Una tarea `node_control` es la ?nica que cambia AP/STA y maneja el vencimiento.
Los callbacks de ruteo solo encolan la configuraci?n de modo. El worker de
Station obtiene el turno antes de cualquier escaneo, lo verifica al terminar
un escaneo bloqueante y vuelve a comprobarlo antes de asociarse. El callback
Wi-Fi de desconexi?n no reconecta autom?ticamente.

Cancelar la tarea Station es cooperativo: se detiene el escaneo y se espera
al worker, evitando eliminar una tarea que conserva un mutex de RPC. Un intento
fallido vuelve a AP con la configuraci?n actual y despu?s libera el turno.
Un enlace admitido no se cambia por el ciclo de b?squeda. Una asociaci?n AP
sin sesi?n TCP se considera ocupada para no interrumpirla.

`app_main` espera a que el driver y el worker del modo solicitado est?n listos
antes de confirmar el arranque OTA normal. No espera la asociaci?n ni el turno;
la barrera de arranque de las cinco placas puede completarse primero.

## Recuperaci?n y validaci?n

El enlace adicional agrega una ruta al vecino y conserva los bloques del nodo,
los cinco m?dulos y los clientes. Al perder el uplink, se invalida su salida por
defecto y se elige otra mediante el protocolo de gateway. Los pedidos se
reintentan cada 30 segundos y el root responde 10 segundos despu?s del primer
pedido. Una interfaz de descarte evita fallback a SPI mientras falta gateway.
Ver [recuperaci?n de rutas](gateway-route-recovery.md).

Se compil? con ESP-IDF 5.4.1 y la configuraci?n de hardware de la rama original.
La imagen cabe en ambas particiones OTA de 4 MB. No se cambi? la tabla de
particiones. STARTUP versi?n 2 y los componentes nuevos requieren el mismo
firmware en las cinco placas de cada nodo que participe en la prueba.

Comprobaciones locales de la versi?n actual:

- 34 pruebas Python de bundles y actualizaci?n: aprobadas.
- 10 pruebas nativas del ?rbitro de b?squeda: aprobadas.
- 10 pruebas nativas de STARTUP, portal, persistencia RSSI y escaneo: aprobadas.
- Prueba nativa de ofertas: aprobada, incluidos rechazo y restauraci?n del uplink.
- Los 13 casos actuales de recuperaci?n/hook compilan con `-Wall -Werror`.
  11 casos de una revisi?n anterior se ejecutaron correctamente; Windows
  bloque? la carga de la DLL posterior con `WinError 4551`. La suite actual
  no se considera ejecutada ni aprobada. No se eludi? esa restricci?n.

Ejecutar desde el worktree:

```powershell
python -m unittest discover -s tests -v
python tests/run_native_discovery.py --cc RUTA_TCC --run-in-memory
python tests/run_native_portal_rssi.py --cc RUTA_TCC --run
python tests/run_native_offers.py --cc RUTA_TCC --run
python tests/run_native_routing.py --cc RUTA_TCC --compile-only
idf.py -C app -B build/firmware build
python auxiliar/export_firmware_bundle.py --build-dir build/firmware --output firmware_bundle_redundant_20261008
```

El resultado de compilaci?n y los tests no acredita entrega de paquetes en
hardware ni caudal. El protocolo de sincronizaci?n de ruteo preexistente tampoco
recupera por s? solo un token concedido y perdido; los reintentos cubren una
solicitud de token perdida, no todos los fallos posibles del anillo.

## Ensayo f?sico con los tres nodos disponibles

Acceso previsto a la RPi: `rpi@192.168.0.113`, por su red de administraci?n.
Al terminar el trabajo la RPi y los nodos estaban apagados. No se realiz? SSH,
no se actualiz? firmware y no se provoc? ninguna desconexi?n f?sica.

1. Encender la RPi y consultar interfaces, puertos USB, firmware instalado,
   UUID y rol de cada nodo. Identificar root y los otros dos nodos A y B.
   Comprobar si admiten OTA antes de elegir el m?todo de actualizaci?n.
2. Instalar la misma imagen en las cinco placas de cada nodo de prueba mediante
   el procedimiento que corresponda a su firmware actual. Conservar las
   im?genes anteriores y sus configuraciones para comparar. STARTUP v2 exige
   actualizaci?n coordinada; no atribuir resultados a un conjunto de versiones.
3. Configurar los umbrales y orientar las antenas para permitir Root?A,
   Root?B y A?B por los l?bulos principales. Registrar RSSI, interfaces y UUID.
   Formar los enlaces iniciales y esperar la b?squeda redundante de 600?700 s.
4. Registrar antes del corte las subredes de los cinco m?dulos de A y B, las
   IP/gateway de sus clientes y el enlace que proporciona la salida principal.
   Verificar que A?B est? admitido y que cada par tiene un solo enlace.
5. Desde un cliente de A mantener un ping a la RPi y otro a una direcci?n
   p?blica conocida que responda ICMP. Descargar un archivo servido por la RPi
   y comprobar HTTP/DNS externo. Capturar paquetes en la RPi y logs seriales
   con marcas de tiempo; hacer primero una l?nea base sin cortar enlaces.
6. Interrumpir ?nicamente Root?A, conservando Root?B y A?B. Verificar recuperaci?n
   A?B?Root y sus respuestas, sin reconectar el cliente ni renovar DHCP.
   Registrar el intervalo sin conectividad, p?rdidas y la salida seleccionada.
   El tr?fico a la RPi por s? solo no demuestra recuperaci?n de internet.
7. Comparar todas las IP/subredes con el registro inicial. Restablecer el enlace
   original y repetir el corte desde B, adem?s de un intento sin candidato:
   la interfaz libre debe retornar AP y permitir que otra obtenga el turno.

Criterio de aprobaci?n: ambas clases de tr?fico se recuperan de ida y retorno,
las direcciones permanecen iguales, solo una interfaz libre busca por nodo y
no queda un turno indefinido. La demora de recuperaci?n se mide; no se fija una
promesa de tiempo con pruebas simuladas. Si WAN no responde antes del corte,
resolver esa l?nea base antes de atribuir el resultado al failover.
