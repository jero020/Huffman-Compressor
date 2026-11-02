# Medicion de speedup

- Comando: `./huffcli -c /tmp/grande.txt /tmp/grande.huf -t N`
- Archivo: 200000000 bytes (`yes "Sistemas Operativos EAFIT compresion Huffman concurrente" | head -c 200000000`), en `/tmp` (tmpfs)
- Nucleos de la VM (`nproc`): 11
- Tiempo de reloj (wall clock), promedio de 3 repeticiones
- Descompresion verificada con `./huffcli -d` + `cmp` para cada N: identica

| Hilos | Tiempo promedio (s) | Speedup (T1/TN) | Eficiencia (Speedup/N) |
|-------|---------------------|-----------------|------------------------|
| 1 | 2.085 | 1.00 | 1.00 |
| 2 | 1.026 | 2.03 | 1.02 |
| 4 | 0.545 | 3.83 | 0.96 |
| 8 | 0.413 | 5.04 | 0.63 |

## Tiempos individuales (s)

| Hilos | Rep. 1 | Rep. 2 | Rep. 3 |
|-------|--------|--------|--------|
| 1 | 2.135 | 2.080 | 2.039 |
| 2 | 1.031 | 1.027 | 1.021 |
| 4 | 0.541 | 0.547 | 0.546 |
| 8 | 0.411 | 0.413 | 0.416 |
