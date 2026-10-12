# Prueba de recuperación con corte temporal

La imagen `redundant-link-20261009-radio-cut` agrega la operación USB 9.
Debe instalarse en las cinco placas de Ramona antes de usarla. El comando
no escribe flash ni activa mantenimiento OTA.

Para la topología observada el 9 de octubre, cortar únicamente Ramona sur:

```powershell
python gateway/cut_link.py --port COM5 --role south --seconds 120 --verbose
```

Cancelar el corte antes del vencimiento:

```powershell
python gateway/cut_link.py --port COM5 --role south --restore --verbose
```

El central debe estar en COM5, las cinco placas listas y en modo normal.
El destino debe ser una Station periférica provisionada con vecino admitido.
No se permite cortar el central, una radio AP ni más de una radio por petición.
Duración válida: 1–300 segundos; cero restaura. Reintentos del mismo mensaje
no prolongan el corte. Una petición distinta durante un corte activo se rechaza.
La respuesta confirma que la orden se encoló; verificar la caída real por
telemetría. La ejecución ocurre en node_control, nunca en el callback SPI.

Se cancela la búsqueda, se cierra la sesión TCP y se detiene WiFi únicamente
en la placa elegida. SPI, el hotspot central y las otras radios siguen activos.
El vencimiento se cuenta desde que termina la detención; el controlador
reactiva WiFi y permite una búsqueda inmediata con turno. Restaurar la radio
no garantiza asociación inmediata: siguen vigentes los turnos y la admisión.
La asignación lógica no se modifica; al restaurar puede renovarse el DHCP
de transporte de la radio. El hotspot y las IP de sus clientes deben conservarse.

Antes del corte registrar IP del teléfono, gateway y telemetría del triángulo.
Mantener datos móviles apagados y pings hacia gateway, `10.255.255.254` y
`1.1.1.1`. Capturar telemetría en la RPi durante toda la prueba. Tras la caída,
Ramona debe alcanzar la RPi por oeste → Grillo → root, sin reconectar el teléfono
ni recibir una nueva provisión. Registrar pérdidas y tiempo hasta recuperar
respuestas; comprobar navegación y la misma IP. Al vencer, comprobar que la
radio se reactiva y registrar qué enlaces se admiten. Prueba física pendiente.
