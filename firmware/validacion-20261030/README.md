# Firmware de validación: estabilidad con cuatro nodos

Rama `fix/stable-links-neighbor-restart`. Versión para validar en equipos los
cambios descritos en el [informe](../../docs/cuarto-nodo-20261030.md).

| Imagen | Uso | Manifiesto |
|---|---|---|
| [grillo/main.bin](grillo/main.bin) | Raíz o casa con SSID `ComNetAR - Grillo` | [hashes y compilación](grillo/capture-manifest.json) |
| [ramona/main.bin](ramona/main.bin) | Casa con SSID `ComNetAR - Ramona` | [hashes y compilación](ramona/capture-manifest.json) |

Son **binarios de aplicación OTA** para ESP32 y flash de 4 MiB con el mapa
`partitions.csv`, slots de 2 031 616 bytes y rollback. No contienen bootloader,
tabla de particiones ni borrado de NVS. El nombre de la casa está compilado;
para otros nombres generar una variante con `--ssid`.

Seguir los [comandos OTA y el procedimiento de validación](../../docs/ota-validacion-cuarto-nodo-20261030.md).
Actualizar los cinco roles de cada nodo juntos. No cargar solamente el central:
el formato interno de routing SPI cambió de 132 a 144 bytes.

Los manifiestos registran SHA-256 del archivo y la identidad ELF esperada en
las cinco placas, además del commit de fuentes empleado para compilar.
El identificador de la aplicación corresponde a ese commit de código; el
commit siguiente agrega estos artefactos y la documentación de entrega.

Las pruebas automáticas y la compilación no sustituyen el ensayo físico de
incorporar o reiniciar el cuarto nodo. No se ha realizado ese ensayo en esta
sesión ni se ha flasheado hardware desde ella.
