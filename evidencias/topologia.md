# Experimento de topologia de CPU

Objetivo: comprobar por que deja de mejorar el speedup con mas hilos.

- Comando: `./huffcli -c /tmp/grande.txt /tmp/grande.huf -t N -v`, con `taskset -c <CPUs>` cuando se indica
- Archivo: 200000000 bytes, muy repetitivo, en `/tmp` (tmpfs)
- CPU segun `lscpu`: 12 CPUs logicas, 6 nucleos, 2 hilos por nucleo, 1 socket (i7-9750H, KVM). `nproc` da 11 (afinidad del proceso `0-10`)
- 10 rondas, cada una con todas las configuraciones en orden aleatorio. Valores: medianas
- Speedup: respecto a N=1 del mismo grupo
- El md5 del `.huf` se verifico al final de cada ronda y siempre coincidio

## Conclusiones

- La ganancia se detiene en 6 hilos: pasar de 4 a 6 siempre mejoro; pasar de 6 a 8 no aporta
- Amdahl con fraccion fija no lo explica: S(6) = 5.06× implica 3.7 % secuencial, lo que predice 6.35× con 8 hilos frente a 5.16× medidos
- El escritor ordenado no es la causa principal: el conteo no lo usa y tampoco mejora de 6 a 8 hilos
- El arbol no influye: unos 13 µs (ver `fases.md`)
- La caida a partir de 6 coincide con los 6 nucleos fisicos, pero la causa **no esta aislada**: tambien pueden influir el ancho de banda de memoria y los limites de potencia o temperatura
- `taskset` no permite probarlo: las CPUs hermanas rinden casi igual que las de nucleos distintos, asi que probablemente la numeracion de la VM no corresponde a los nucleos reales del host

## Sin fijar CPUs

| Hilos | CPUs permitidas | Conteo (s) | Codificación (s) | Total (s) | Mín-máx total (s) | Speedup conteo | Speedup codificación | Speedup total | Eficiencia |
|---|---|---|---|---|---|---|---|---|---|
| 1 | sin fijar | 0.270 | 4.564 | 4.849 | 2.134-7.843 | 1.00× | 1.00× | 1.00× | 1.00 |
| 2 | sin fijar | 0.129 | 2.303 | 2.447 | 1.515-4.211 | 2.09× | 1.98× | 1.98× | 0.99 |
| 3 | sin fijar | 0.092 | 1.592 | 1.700 | 0.770-2.578 | 2.93× | 2.87× | 2.85× | 0.95 |
| 4 | sin fijar | 0.075 | 1.242 | 1.328 | 0.575-2.020 | 3.61× | 3.67× | 3.65× | 0.91 |
| 5 | sin fijar | 0.062 | 1.006 | 1.083 | 0.494-1.615 | 4.36× | 4.54× | 4.48× | 0.90 |
| 6 | sin fijar | 0.056 | 0.885 | 0.959 | 0.463-1.628 | 4.79× | 5.16× | 5.06× | 0.84 |
| 7 | sin fijar | 0.054 | 0.869 | 0.945 | 0.453-1.358 | 4.96× | 5.25× | 5.13× | 0.73 |
| 8 | sin fijar | 0.053 | 0.869 | 0.939 | 0.487-1.511 | 5.11× | 5.26× | 5.16× | 0.65 |

## Limitado a una CPU logica por nucleo segun la VM

| Hilos | CPUs permitidas | Conteo (s) | Codificación (s) | Total (s) | Mín-máx total (s) | Speedup conteo | Speedup codificación | Speedup total | Eficiencia |
|---|---|---|---|---|---|---|---|---|---|
| 1 | 0,2,4,6,8,10 | 0.263 | 4.843 | 5.135 | 2.604-7.668 | 1.00× | 1.00× | 1.00× | 1.00 |
| 2 | 0,2,4,6,8,10 | 0.130 | 2.446 | 2.591 | 1.089-3.538 | 2.02× | 1.98× | 1.98× | 0.99 |
| 3 | 0,2,4,6,8,10 | 0.091 | 1.646 | 1.765 | 0.741-2.458 | 2.90× | 2.94× | 2.91× | 0.97 |
| 4 | 0,2,4,6,8,10 | 0.083 | 1.237 | 1.333 | 0.578-1.936 | 3.17× | 3.92× | 3.85× | 0.96 |
| 5 | 0,2,4,6,8,10 | 0.064 | 0.967 | 1.047 | 0.510-1.475 | 4.13× | 5.01× | 4.90× | 0.98 |
| 6 | 0,2,4,6,8,10 | 0.062 | 0.983 | 1.067 | 0.483-1.609 | 4.27× | 4.93× | 4.81× | 0.80 |
| 8 | 0,2,4,6,8,10 | 0.056 | 0.835 | 0.904 | 0.517-1.389 | 4.70× | 5.80× | 5.68× | 0.71 |

## Control: CPUs hermanas frente a nucleos distintos

| Hilos | CPUs permitidas | Conteo (s) | Codificación (s) | Total (s) | Mín-máx total (s) | Speedup conteo | Speedup codificación | Speedup total | Eficiencia |
|---|---|---|---|---|---|---|---|---|---|
| 1 | 0 | 0.260 | 4.770 | 5.051 | 2.298-7.328 | 1.00× | 1.00× | 1.00× | 1.00 |
| 2 | 0,1 | 0.135 | 2.357 | 2.507 | 1.521-3.458 | 1.92× | 2.02× | 2.01× | 1.01 |
| 2 | 0,2 | 0.138 | 2.465 | 2.622 | 1.087-3.698 | 1.89× | 1.93× | 1.93× | 0.96 |
| 4 | 0,1,2,3 | 0.079 | 1.308 | 1.402 | 0.595-2.116 | 3.30× | 3.65× | 3.60× | 0.90 |
| 4 | 0,2,4,6 | 0.071 | 1.202 | 1.292 | 0.640-2.666 | 3.67× | 3.97× | 3.91× | 0.98 |

## Comparaciones ronda a ronda (razon de tiempos totales)

| Comparacion | Mediana | Min | Max | Rondas con razon > 1 |
|---|---|---|---|---|
| Sin fijar: T4/T6 | 1.286 | 1.241 | 1.694 | 10/10 |
| Sin fijar: T5/T6 | 1.102 | 0.952 | 1.356 | 8/10 |
| Sin fijar: T6/T7 | 1.005 | 0.740 | 1.872 | 5/10 |
| Sin fijar: T6/T8 | 1.020 | 0.777 | 1.154 | 5/10 |
| Conteo sin fijar: T6/T8 | 0.994 | 0.637 | 1.169 | 5/10 |
| Hermanas/distintas, N=2 | 1.092 | 0.824 | 1.399 | 6/10 |
| Hermanas/distintas, N=4 | 1.027 | 0.737 | 1.626 | 6/10 |
| Sin fijar/una por nucleo, N=4 | 0.995 | 0.836 | 1.236 | 4/10 |
| Sin fijar/una por nucleo, N=5 | 1.046 | 0.737 | 1.154 | 7/10 |
| Sin fijar/una por nucleo, N=6 | 0.936 | 0.595 | 1.297 | 3/10 |

## Ruido de la medicion

La maquina se fue haciendo mas lenta durante el experimento: todas las configuraciones se frenaron a la vez, hasta unas 3 veces. Apunta a algo externo al programa (temperatura o carga del host, sin verificar). Por eso los tiempos absolutos no son citables y las diferencias menores de un 10 % no son concluyentes.

| Ronda | 1 | 2 | 3 | 4 | 5 | 6 | 7 | 8 | 9 | 10 |
|---|---|---|---|---|---|---|---|---|---|---|
| Total N=1 (s) | 2.13 | 2.53 | 3.46 | 4.44 | 4.80 | 5.85 | 4.90 | 6.55 | 7.84 | 5.40 |
| Total N=4 (s) | 0.57 | 0.69 | 1.31 | 1.07 | 1.36 | 1.37 | 1.26 | 1.99 | 2.02 | 1.34 |
| Total N=6 (s) | 0.46 | 0.46 | 0.77 | 0.84 | 0.96 | 1.06 | 0.99 | 1.58 | 1.63 | 0.96 |
| Total N=8 (s) | 0.60 | 0.49 | 0.85 | 0.77 | 0.97 | 1.01 | 0.91 | 1.37 | 1.51 | 0.98 |

## Tiempos totales por ronda, todas las configuraciones (s)

| Hilos | CPUs | 1 | 2 | 3 | 4 | 5 | 6 | 7 | 8 | 9 | 10 |
|---|---|---|---|---|---|---|---|---|---|---|---|
| 1 | sin fijar | 2.134 | 2.531 | 3.463 | 4.437 | 4.797 | 5.854 | 4.902 | 6.552 | 7.843 | 5.404 |
| 2 | sin fijar | 1.515 | 1.567 | 1.916 | 2.153 | 2.972 | 2.655 | 2.240 | 3.427 | 4.211 | 3.121 |
| 3 | sin fijar | 0.770 | 0.928 | 1.256 | 1.515 | 1.726 | 1.800 | 1.675 | 2.266 | 2.578 | 1.903 |
| 4 | sin fijar | 0.575 | 0.688 | 1.311 | 1.069 | 1.360 | 1.366 | 1.265 | 1.992 | 2.020 | 1.344 |
| 5 | sin fijar | 0.494 | 0.514 | 0.982 | 1.053 | 1.050 | 1.123 | 1.112 | 1.508 | 1.615 | 1.303 |
| 6 | sin fijar | 0.463 | 0.464 | 0.774 | 0.840 | 0.957 | 1.058 | 0.987 | 1.584 | 1.628 | 0.961 |
| 7 | sin fijar | 0.453 | 0.627 | 0.656 | 0.965 | 0.924 | 1.100 | 1.000 | 0.846 | 1.358 | 0.979 |
| 8 | sin fijar | 0.596 | 0.487 | 0.848 | 0.773 | 0.967 | 1.007 | 0.911 | 1.373 | 1.511 | 0.984 |
| 1 | 0,2,4,6,8,10 | 2.604 | 2.658 | 3.552 | 4.145 | 5.999 | 5.656 | 4.614 | 7.668 | 7.394 | 6.029 |
| 2 | 0,2,4,6,8,10 | 1.089 | 1.308 | 2.030 | 2.297 | 2.739 | 2.769 | 2.443 | 3.310 | 3.538 | 3.297 |
| 3 | 0,2,4,6,8,10 | 0.741 | 1.108 | 1.375 | 1.732 | 1.805 | 1.798 | 1.658 | 2.124 | 2.458 | 2.027 |
| 4 | 0,2,4,6,8,10 | 0.578 | 0.598 | 1.061 | 1.260 | 1.393 | 1.441 | 1.272 | 1.936 | 1.833 | 1.607 |
| 5 | 0,2,4,6,8,10 | 0.510 | 0.647 | 0.851 | 0.984 | 1.425 | 1.111 | 0.970 | 1.475 | 1.464 | 1.195 |
| 6 | 0,2,4,6,8,10 | 0.483 | 0.593 | 0.811 | 1.086 | 1.609 | 1.409 | 0.844 | 1.316 | 1.255 | 1.048 |
| 8 | 0,2,4,6,8,10 | 0.517 | 0.598 | 0.730 | 0.816 | 1.092 | 0.997 | 0.849 | 1.262 | 1.389 | 0.960 |
| 1 | 0 | 2.298 | 2.952 | 3.434 | 4.478 | 6.532 | 5.723 | 5.246 | 4.856 | 7.328 | 5.724 |
| 2 | 0,1 | 1.521 | 1.571 | 2.157 | 2.279 | 2.689 | 3.261 | 2.325 | 3.403 | 3.458 | 2.895 |
| 2 | 0,2 | 1.087 | 1.210 | 1.801 | 2.155 | 3.264 | 2.894 | 2.350 | 2.907 | 3.698 | 3.330 |
| 4 | 0,1,2,3 | 0.595 | 0.880 | 0.989 | 1.182 | 2.116 | 1.528 | 1.277 | 1.855 | 1.966 | 1.693 |
| 4 | 0,2,4,6 | 0.640 | 0.784 | 0.883 | 1.174 | 1.301 | 1.382 | 1.283 | 2.069 | 2.666 | 1.616 |
