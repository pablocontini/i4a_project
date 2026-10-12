# Evidencia de las pruebas del 12 de octubre

Resultados copiados de `build/firmware` para que no dependan de archivos
ignorados por Git. `manifest.json` registra origen, tama?o y SHA-256.
Los resultados son hist?ricos: los prefijos identifican la imagen/ensayo,
y los reportes `*-installed.json` no indican que todas esas im?genes sigan activas.

- `return-*`: recuperaci?n de rutas corregida, dos cortes por nodo.
- `repeat-cut` y `simultaneous`: fallas previas a la correcci?n de retorno.
- `dual-*`, `multi8-*`, `grillo-control`: carga HTTP y an?lisis de PCAP.
- `linkdiag-*`, `west-load-*`, `east-center-*`: contadores por tramo.
- `buf0-*`, `buf8-*`: comparaci?n experimental de reservas SPI.

El A/B de buffers tiene un cambio de asignaci?n/recorrido de Grillo;
no prueba causal de mejora de throughput. Los contadores CRC se interpretan
junto a sus ventanas temporales, no como porcentaje de p?rdida radio.

PCAP completos y UART cruda permanecen locales. Los JSON de an?lisis PCAP
contienen m?tricas/identidades de flujo y limitaciones del analizador, no
payload completo. Consulte los documentos de carga y buffers para los
nombres de los originales. No todos los ensayos incluyen PCAP simult?neo.
