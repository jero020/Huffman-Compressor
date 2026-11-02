# Tiempos por fase

> **Nota:** T1 quedo inflado por el ruido de la maquina (2.42 a 2.88 s), asi que los speedups salen por encima de lo ideal (2.30× con 2 hilos, 4.28× con 4) y no deben citarse. Esta tabla sirve para ver el peso de cada fase. Los speedups mas fiables estan en `topologia.md`.

- Comando: `./huffcli -c /tmp/grande.txt /tmp/grande.huf -t N -v` (200000000 bytes, en tmpfs)
- Promedio de 3 repeticiones por cada N
- Conteo incluye abrir el archivo de entrada; Codificacion incluye crear la salida, escribir la cabecera, la escritura ordenada y el join de los trabajadores
- Total: tiempo de reloj que reporta huffcli (incluye ademas fsync, close y rename)
- El arbol lo construye un solo hilo: su "speedup" es solo ruido de medicion
- Descompresion verificada con `./huffcli -d` + `cmp` para cada N: identica

| Hilos | Conteo (s) | Árbol (s) | Codificación (s) | Total (s) | Speedup de cada fase |
|-------|------------|-----------|------------------|-----------|----------------------|
| 1 | 0.141 | 0.000014 | 2.468 | 2.614 | conteo 1.00× · árbol 1.00× · codificación 1.00× · total 1.00× |
| 2 | 0.068 | 0.000013 | 1.064 | 1.139 | conteo 2.08× · árbol 1.08× · codificación 2.32× · total 2.30× |
| 4 | 0.040 | 0.000013 | 0.565 | 0.611 | conteo 3.56× · árbol 1.03× · codificación 4.37× · total 4.28× |
| 8 | 0.032 | 0.000013 | 0.456 | 0.494 | conteo 4.45× · árbol 1.03× · codificación 5.42× · total 5.29× |

## Tiempos individuales (s)

| Hilos | Rep. | Conteo | Árbol | Codificación | Total |
|-------|------|--------|-------|--------------|-------|
| 1 | 1 | 0.1348 | 0.000013 | 2.2837 | 2.419 |
| 1 | 2 | 0.1465 | 0.000015 | 2.7299 | 2.883 |
| 1 | 3 | 0.1427 | 0.000013 | 2.3889 | 2.539 |
| 2 | 1 | 0.0709 | 0.000013 | 1.0832 | 1.160 |
| 2 | 2 | 0.0649 | 0.000012 | 1.0771 | 1.148 |
| 2 | 3 | 0.0686 | 0.000013 | 1.0320 | 1.108 |
| 4 | 1 | 0.0411 | 0.000013 | 0.5606 | 0.608 |
| 4 | 2 | 0.0409 | 0.000013 | 0.5645 | 0.613 |
| 4 | 3 | 0.0370 | 0.000014 | 0.5685 | 0.612 |
| 8 | 1 | 0.0334 | 0.000013 | 0.4510 | 0.491 |
| 8 | 2 | 0.0278 | 0.000012 | 0.4408 | 0.476 |
| 8 | 3 | 0.0340 | 0.000015 | 0.4747 | 0.516 |
