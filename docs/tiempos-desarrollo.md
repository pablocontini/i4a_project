# Tiempos para pruebas de desarrollo

En `idf.py menuconfig`, activar `I4A development` ->
`Use short telemetry and redundant discovery delays` para los próximos
firmwares de laboratorio. La opción es `CONFIG_I4A_FAST_TEST_TIMING` y está
desactivada por defecto. `app/sdkconfig.development` sirve como fragmento
de defaults para configuraciones nuevas; los defaults no reemplazan una
opción ya guardada en un sdkconfig existente.

| Tiempo | Normal | Desarrollo |
| --- | --- | --- |
| Espera aleatoria de búsqueda redundante | 600–700 s | 5–10 s |
| Espera inicial del emisor UDP | 300 s | 1 s |
| Intervalo de publicaciones UDP | 60 s | 5 s |
| Intervalo de muestras por SPI | 30 s | 5 s |
| Separación inicial por orientación | 60 s | 0,2 s |

Los tiempos comienzan cuando se inicia la tarea correspondiente: no prometen
telemetría un segundo después de encender el nodo. La red, la provisión y
las muestras deben estar disponibles; los primeros mensajes pueden tener
campos pendientes. Los turnos siguen serializados y sus vencimientos,
las reglas de admisión y la recuperación OTA conservan sus tiempos.

Antes de la versión final, desactivar la opción en menuconfig y recompilar
todos los nodos. Verificar `# CONFIG_I4A_FAST_TEST_TIMING is not set` en el
sdkconfig usado. Esta modificación no cambia el firmware ya flasheado.
