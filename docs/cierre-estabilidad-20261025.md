# Cierre de implementación y ensayos, 25/10/2026

Rama de publicación: **`feat/config-portal-stability`**, repositorio
[pablocontini/i4a_project](https://github.com/pablocontini/i4a_project/tree/feat/config-portal-stability).
Este índice reúne el estado final; cada informe conserva las condiciones
y versiones de su propia corrida. Horas de los ensayos en Argentina (UTC−3).

## Qué cambió

- Recuperación de gateway cuando se perdió la ruta por defecto: solicitudes
  y confirmación del recorrido para admitir un camino alternativo más largo,
  con protección frente a ciclos y respuestas fuera de contexto.
- Reserva persistente de bloques por UUID lógico. El central serializa la
  asignación, guarda la propiedad en NVS antes de ofrecer el bloque y evita
  reasignarlo a otro hogar cuando cambia la radio de conexión.
- Admisión de vecinos versión 2: declara y valida el bloque del nodo antes
  de activar el enlace. Impide reclamaciones que sustituyan rutas ajenas.
- Guardas frente a mensajes de provisión retrasados y eliminación del borrado
  automático de NVS al fallar su inicialización.
- Diagnósticos de vecinos y comando de corte temporizado, con recuperación
  automática y registros de los eventos realmente aplicados. El controlador
  exige una referencia de pings ligada a una interfaz y comprueba los cinco
  roles antes de enviar el corte.

La migración requiere las cinco placas de cada nodo con firmware compatible;
la admisión anterior no ofrece los datos necesarios para asignar con seguridad.
La implementación, límites de las reservas y comandos de migración están en
[corrección de bloques](correccion-bloques-reservados-20261025.md).

## Proceso y resultados

| Etapa | Resultado y evidencia |
|---|---|
| Corte del 22/10 | Se recuperaba un camino a Root pero faltaba la salida a Internet. [Informe](prueba-corte-enlace-20261022.md). |
| Corte Norte del 25/10 | Alternativo ya asociado permitió recuperar Internet. Corresponde a firmware anterior. [Informe](prueba-corte-norte-20261025.md). |
| Alternativo tardío | Un nuevo directo recuperó Internet, pero incorporar Ramona duplicó bloques y cortó el retorno. Fallo reproducido con callbacks C. No validó un alternativo tardío de mayor distancia. [Informe](alternativo-tardio-20261025.md). |
| Corrección y carga | 104 pruebas C, compilaciones ESP32 y carga OTA en las quince placas. Consulta independiente de todos los roles. [Despliegue](despliegue-bloques-20261025.md). |
| Cliente Wi-Fi 3 | DHCP dio `10.148.0.2`, gateway `10.148.0.1`. Pings con origen fijo, sin modificar IP ni rutas de Windows. |
| Búsqueda redundante | Se observaron ciclos AP/STA casi sincronizados. Una captura USB fue incompleta y se descartó para diagnosticar ciclos; al reducir a cuatro cables se verificó continuidad. Los snapshots revelaron un enlace por Norte ya activo. No se modificó el calendario de búsqueda. |
| Nuevo arranque de Ramona | El usuario no lo ordenó, pero indicó que pudo producirse al cambiar cables. Este Ramona informa encendido (`reset=1`); no hay registro anterior al evento para determinar la causa. No se atribuye al firmware. |
| Cambio de topología | Ramona volvió a conectar directamente a Root por Norte y el lateral pasó a Este Ramona ↔ Oeste Grillo. Se conservaron bloques distintos. |
| Corte Este con firmware nuevo | Corte de 60 s; Internet volvió por Ramona en 18,577 s, antes de restaurarse la radio principal. Retorno posterior al directo, bloques conservados y sin regresiones de uptime en cuatro monitores. [Informe final](prueba-corte-este-v3-20261025.md). |
| Intento tardío con firmware nuevo | Corte de 180 s e incorporación de Ramona. Internet volvió en 116,185 s por Norte directo a Root. No validó el camino tardío por Ramona; bloques conservados, sin nuevas pérdidas persistentes al incorporarla. [Informe](alternativo-tardio-v3-20261025.md). |

El detalle de capturas, errores USB, reasignaciones de COM y resultados
intermedios está en el informe de despliegue. No confundir el cierre sin
errores de un proceso de captura con la recepción continua de datos: se
comprobaron también últimos eventos, identidades y progresión de uptime.
Los COM son circunstanciales y deben verificarse en cada montaje.

## Validación antes de publicar

Se repitieron satisfactoriamente estas suites sobre el código a publicar:

| Suite nativa | Casos |
|---|---:|
| Enrutamiento | 36 |
| Vecinos, reservas, persistencia y admisión | 36 |
| Ciclo de vida Wi-Fi | 7 |
| Hooks de red | 5 |
| OTA y control de radio | 24 |
| **Total C** | **110** |

También pasó la compatibilidad del protocolo C/Python y los **75 tests Python**
de `python -m unittest discover -s tests -p 'test_*.py'`. Las suites C se
ejecutan con `tests/run_native_{routing,neighbors,wireless,routing_hooks,ota}.py`
y `--cc app/build/tools/tcc/tcc.exe` en este entorno Windows.
Las compilaciones ESP32 y los hashes de las imágenes realmente instaladas
quedan registrados en el informe de corrección y despliegue; publicar las
fuentes no recompila ni modifica las placas.

## Conservación de evidencia

[Evidencia resumida versionada](evidencias/20261025/README.md) incluye el
resultado estructurado del último ensayo, la verificación de quince roles,
validación previa a las compilaciones, recibos SHA-256 y resultados de tests.
Los archivos de `app/build/` son locales y están excluidos de Git: capturas
completas, raw UART, binarios, ELF y paquetes no se suben implícitamente.
Los recibos identifican los archivos completos conservados en la PC.
Los ocho logs OTA sueltos anteriores a este cierre tampoco forman parte
de la publicación; los informes identifican la evidencia utilizada.

`auxiliar/prepare_link_test_network.ps1` fue una alternativa histórica para
Wi-Fi 2 sin DHCP y contiene direcciones fijas de aquel montaje. No se aplicó
en estos ensayos ni debe usarse con los bloques actuales. Wi-Fi 3 funcionó
con DHCP y pings de origen fijo.

## Qué queda pendiente

- Ensayo válido de un alternativo que aparece después del corte y requiere
  mayor distancia, sin que un nuevo directo a Root resuelva primero la falla.
- Cortes repetidos y observación prolongada de la versión nueva; una corrida
  no descarta acumulación de fallas ni acredita estabilidad de doce horas.
- Determinar la causa del arranque anterior de Ramona si vuelve a ocurrir,
  capturando antes del evento y manteniendo fija la alimentación.
- Medir y reducir la convergencia de unos 18,6 segundos si el requisito de
  servicio exige menos interrupción. No presentar la recuperación actual
  como continuidad sin pérdidas.

La reserva de bloques tiene capacidad finita y no caduca por pérdida del
enlace: agotar el registro rechaza nuevas ofertas de forma segura. No existe
todavía recolección automática de propietarios históricos. La sincronización
de radios libres es una observación, no una causa demostrada de ausencia
de redundancia en esta red.


## Actualizaci?n final del 26/10

Se publican el filtro temporal de nuevos directos a Root, su controlador
con comprobaci?n del plazo y cancelaci?n, y el diagn?stico acotado `I4AGW`.
Los plazos y mensajes de routing no se modificaron por la instrumentaci?n.
El filtro usa RAM, tiene vencimiento y conserva enlaces ya admitidos.

El [ensayo con bloqueo](alternativo-tardio-con-bloqueo-20261025.md) valid?
recuperaci?n por una alternativa aparecida despu?s del corte. La
[repetici?n con diagn?stico](diagnostico-gateway-20261025.md) confirm? el
camino por Ramona y midi? 14,864 segundos entre solicitud y aceptaci?n,
sin reintentos ni desbordes en los monitores. No demuestra la causa de
la demora mayor de la corrida anterior ni estabilidad prolongada de esta versi?n.

La [evidencia del 26](evidencias/20261026/README.md) contiene resultados,
eventos de gateway, recibo del archivo completo y verificaci?n reciente de
los quince roles. Los binarios ya cargados coinciden con las 170 fuentes
archivadas por paquete, normalizando finales de l?nea. Una nueva consulta
confirm? que los tres nodos ya ten?an la ?ltima imagen; no se reescribi?
innecesariamente el mismo binario ni se reiniciaron para esta publicaci?n.
