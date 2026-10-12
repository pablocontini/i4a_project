# Prueba de tres nodos en una mesa

N, S, E y W identifican placas; no representan direcciones geográficas durante
esta prueba sin antenas direccionales. Primero comprobar conexión básica y
estabilidad del anillo con fix2 en las cinco placas de cada nodo. Los umbrales
por defecto son -128 dBm. No aplicar la selección de enlaces de esta guía
antes de comprobar esa línea base.

## Topología objetivo

```text
                    ROOT
              N (AP)     E (AP)
                 |         |
          S (STA) A         B W (STA)
                 E ------- E
                  redundante
             A E: STA; B E: AP
```

Root N ofrece 10.32.0.0/11 a A; Root E ofrece 10.96.0.0/11 a B.
B E ofrece 10.108.0.0/14 a A E para el enlace redundante. Así la oferta
es mayor y más específica que 10.32.0.0/11. El intento B E hacia un AP
de A puede ser rechazado por el orden de direcciones; comprobar la orientación
AP/STA efectiva, no solamente la existencia de una asociación Wi-Fi.

Ubicar A y B separados aproximadamente 40–60 cm, y Root más lejos de ambos,
por ejemplo a 1–1,5 m si la mesa lo permite. Son posiciones iniciales para
medir; no garantizan un RSSI específico.

## Potencia y umbrales

| Nodo | Placas | Potencia máxima inicial | Umbral mínimo |
| --- | --- | --- | --- |
| Root | N, E | 11 dBm | No determina admisión: funcionan como AP |
| Root | S, W | 2 dBm | No determina admisión: funcionan como AP |
| A | S | 2 dBm | T_P, para aceptar Root N y rechazar AP débiles |
| A | E | 20 dBm | T_R, para aceptar B E y rechazar Root E al aprovisionarse |
| A | N, W | 2 dBm | -1 dBm, rechazo práctico de candidatos cuando sean STA |
| B | W | 2 dBm | T_P, para aceptar Root E y rechazar AP débiles |
| B | E | 20 dBm | T_R, para evitar usar esta placa como uplink inicial a Root |
| B | N, S | 2 dBm | -1 dBm, rechazo práctico de candidatos cuando sean STA |

Los valores de potencia están admitidos por el portal. 2 dBm es el mínimo;
no significa radio apagada. Un umbral -1 dBm impide en la práctica que una
Station elija candidatos, pero no desactiva la radio ni evita que su AP
reciba conexiones. El umbral afecta al escaneo, no retira un enlace existente.

T_P y T_R deben obtenerse de medidas con esos mismos valores de potencia:

- En S de A y W de B: RSSI del AP Root débil < T_P <= RSSI del AP Root deseado.
- En E de A y E de B: RSSI de Root E < T_R <= RSSI de la E del otro nodo.

Por ejemplo, si se mide -45 dBm del Root deseado y -58 dBm de sus AP débiles,
T_P = -52 dBm permite separarlos. Si en la E se mide Root a -44 dBm y el otro
nodo a -25 dBm, T_R = -35 dBm permite favorecer el enlace redundante. Son
ejemplos condicionados a mediciones, no predicciones para estas placas.

Si los rangos se superponen, cambiar distancias o potencias y volver a medir.
Potencia y RSSI no garantizan una topología exacta entre radios omnidireccionales
muy próximas. Registrar siempre los UUID y las placas que realmente conectaron.

El portal persiste los valores y los aplica al reiniciar. En esta implementación
el portal de configuración normal se habilita en el central no root. Para
configurar un equipo destinado a Root, guardar primero sus valores con ese
central en modo home/configuración, y luego volver a su modo Root. Cambiar el
modo y la configuración antes del ensayo, no durante la prueba de failover.

## Secuencia y corte de enlace

1. Medir y guardar todos los valores antes de formar la topología; reiniciar.
2. Comprobar Root N–A S y Root E–B W, junto con las subredes esperadas.
3. Esperar la búsqueda redundante de 600–700 s y comprobar A E–B E.
4. Registrar IP de los módulos/clientes, tráfico a la RPi e internet y sus
   respuestas antes de cortar el enlace.
5. Interrumpir solo Root N–A S. Deben quedar operativos Root E–B W y A E–B E.
   El tráfico de A debe recuperarse por A–B–Root, con las mismas IP.

El portal de potencia/RSSI reinicia todo el nodo; usarlo para provocar el
corte no demuestra failover sin reinicio. Tampoco sirve apagar Root completo
ni retirar alimentación de una placa, porque se pierde la salida de B o se
interrumpe el anillo SPI. El ensayo necesita deshabilitar una sola radio en
tiempo de ejecución o atenuar únicamente su enlace, por ejemplo encerrando
la antena/placa S de A en un blindaje eléctricamente aislado mientras conserva
alimentación y SPI. La versión actual no ofrece un botón de portal para ese
corte individual.
