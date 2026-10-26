# Evidencia del cierre del 26/10/2026

Rama: `feat/config-portal-stability`.

- `resultado-gateway.json`: pings, cambios de rutas y continuidad de uptime
  de la repetición con alternativa tardía. Referencia temporal: ACK del corte,
  no UART físico de Norte; no se observó directamente su hora de restauración.
- `gateway-eventos.json`: eventos locales `I4AGW`, con sus relojes de dispositivo.
  Las secuencias son locales y no identificadores de transacción del protocolo.
- `auditoria-gateway.json`: alcance del ensayo y limitaciones de captura.
- `verificacion-15-placas.json`: consulta reciente de los cinco roles por nodo,
  mediante los centrales Root COM8, Grillo COM5 y Ramona COM9.
- `validacion-nativa.json` y `validacion-python.json`: 110 casos C y 75 Python.
- `*-firmware.json`: identidad ELF, SHA256 del binario y del archivo de fuentes.
- `recibo-evidencia.json`: identifica el ZIP completo conservado localmente.

El control de actualización comprobó que cada nodo ya tenía el paquete
correspondiente y omitió reescribirlo. Root y Grillo ejecutan ELF
`57bb3d29051e763cecb175eb700d6c92894ba8065e4b14f8bf1c01f7499817fc`;
Ramona, `d033c9e5254dc0b5edfcd8bd762773f20a0820cda2641327f792f857d82c1e58`.
Los quince roles están listos, sin verificación, recuperación o pausa pendientes.
Las 170 fuentes archivadas en cada paquete coinciden con las actuales,
normalizando CRLF/LF. La cadena de versión conserva el commit anterior
con sufijo `dirty` porque las imágenes se compilaron antes de este cierre;
la identificación de binarios se hace por ELF SHA256 y manifiesto.

El caso funcional de alternativa tardía quedó comprobado. Siguen siendo
seguimientos recomendados los cortes repetidos, pérdida deliberada de
mensajes de control y observación prolongada. La demora anterior de 46
segundos no se reprodujo; no se declara resuelta su causa específica.

El [informe completo](../../diagnostico-gateway-20261025.md) conserva las
condiciones, horas en Argentina, análisis y resultados. Los binarios,
ELF y capturas UART completas permanecen en `app/build/`, excluido de Git.
