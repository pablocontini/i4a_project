# Integración del portal y filtro RSSI

Este documento describe la integración de origen. La adaptación sobre el
firmware de estabilidad está en `feat/config-portal-stability`; sus diferencias,
compatibilidad y validación se documentan en
[portal-estabilidad-20261022.md](portal-estabilidad-20261022.md).

La rama `feat/redundant-link-discovery` integra los componentes de configuración
y orientación existentes en `i4a-2`, sin modificar ese repositorio de origen.
La fuente local estaba en `feat/esp-reset-diagnostics`; los componentes de portal,
orientación, credenciales y filtro de estación no tenían modificaciones locales.

El central conserva en NVS cuatro umbrales mínimos de RSSI, uno por interfaz
direccional. El portal permite establecerlos entre -128 y -1 dBm. El valor por
defecto -128 conserva el comportamiento anterior y no elimina en la práctica los
lóbulos traseros: el supuesto de exclusión requiere configurar y medir los
umbrales adecuados para la instalación.

Los valores se transmiten al arrancar mediante el paquete STARTUP versión 2 del
anillo, junto con el modo de arranque y las potencias de transmisión. Cada ESP
direccional usa su umbral tanto para seleccionar un AP como para medir el mejor
candidato permitido. Una señal igual al umbral se admite; una señal inferior se
descarta. El umbral permanece fijo durante la operación normal. Aplicar cambios
desde el portal guarda la configuración y coordina el reinicio del nodo.

El portal de configuración usa `ComNetAR_Config` y `192.168.4.1`. Se conserva el
modo de orientación y su portal `Orientacion_Antenas` en `192.168.50.1`, además de
las credenciales domésticas que guarda `wifi_credentials`. Los modos especiales
confirman el arranque OTA antes de iniciar el servicio y omiten el routing normal.

Todos los ESP de un anillo deben actualizarse juntos: STARTUP versión 2 no es
compatible con el paquete antiguo de esa rama de estabilidad. El rechazo de un
paquete inválido impide aplicar valores de radio incorrectos.

Para validar en placas: guardar cuatro umbrales diferentes, reiniciar, comprobar
su persistencia y que cada orientación recibe el valor correspondiente. Verificar
que el escaneo ignora una señal inferior al umbral y acepta una igual o mayor;
después comprobar que iniciar y abandonar configuración u orientación devuelve
el nodo a su operación normal. Estas mediciones complementan las pruebas de
admisión, coordinación y recuperación de rutas de la búsqueda redundante.
