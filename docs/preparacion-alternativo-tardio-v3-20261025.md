# Preparación de alternativa tardía con bloques reservados, 25/10/2026

El usuario apagó las cinco placas de Ramona, incluyendo la desconexión del
USB de su monitor. No se modificó la alimentación de Root ni Grillo.
Puertos previstos: COM5 central Grillo, COM7 Este Grillo, COM8 Oeste Grillo;
COM9 Este Ramona ausente mientras el nodo está apagado.

## Referencia de 19:52–19:53, hora de Argentina

Captura pasiva de 60 segundos:
`app/build/capturas/tardio-v3-previo-20261025-195231-4388/`.
El puntero es `app/build/tardio-v3-previo-activo.txt`.
COM5, COM7 y COM8 recibieron datos; la ausencia de COM9 fue esperada.
La captura terminó y liberó los puertos.

El central de Grillo mostró solamente Este en fase activa, vecino Root
`000000000000`; Norte, Sur y Oeste no tenían vecino. Su salida por defecto
fue 3. Se conservó el bloque local `10.148.0.0/14`; ya no estaba la ruta
específica al bloque de Ramona. Oeste figuró como AP sin clientes.

Desde Wi-Fi 3, con origen fijo `10.148.0.2`, respondieron 60/60 probes
de cada destino: gateway `10.148.0.1`, RPi `10.255.255.254`, Internet
`1.1.1.1`. No se envió ningún comando de corte ni se encendió Ramona
durante esta referencia.

## Condición aún por controlar

El comando instalado corta una estación ya asociada, por tiempo limitado.
No impide que otra radio libre de Grillo se conecte a Root. Por tanto,
cortar Este y encender Ramona después no garantiza probar un camino tardío
de mayor distancia. Un nuevo directo a Root invalidaría esa conclusión,
como ocurrió en el ensayo anterior.

Se consultó al usuario si prefiere preparar una exclusión temporal de nuevos
directos a Root, que requiere una actualización de Grillo, o intentar con
el firmware actual y clasificar la corrida según las rutas efectivamente
observadas. Eligió intentar con el firmware actual. El
[resultado del ensayo](alternativo-tardio-v3-20261025.md) confirma recuperación
por otro directo a Root; no valida el camino tardío por Ramona.
