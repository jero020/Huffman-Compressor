# Compresor Huffman Concurrente

Compresor y descompresor de archivos con codificación de Huffman, escrito en C con `pthread`.
El archivo se procesa por bloques en paralelo y la compresión se integra como tarea en segundo
plano al editor de texto del Parcial 1 (`editor/`), sin congelar la interfaz y sin condiciones de
carrera al editar mientras se comprime. El editor se sigue abriendo desde el shell del Parcial 1
(`shell/`), igual que antes.

Parcial 2 de Sistemas Operativos (Universidad EAFIT), Alternativa 1.

---

## Compilar y usar

```
make                                   # compila huffcli, editor/editor y shell/sys_shell
./shell/sys_shell                      # shell del Parcial 1; ahí: editor documento.txt
./editor/editor documento.txt          # o abrir el editor directamente
./huffcli -c archivo.txt archivo.huf   # comprimir desde la terminal
./huffcli -d archivo.huf salida.txt    # descomprimir
```

Opciones de `huffcli`: `-t N` número de hilos (por defecto, los núcleos disponibles),
`-s KB` tamaño de bloque (por defecto 1024). Ctrl+C cancela sin dejar
archivos a medias.

### Desde el shell

`sys_shell` conserva todos sus comandos del Parcial 1 (`help`, `d_create`, `d_read`, `m_sbrk`,
`p_fork`, `p_exec`, `hora`, …) y suma la categoría `compresion`:

| Comando | Qué hace |
|---|---|
| `editor <archivo>` | Abre el editor (con compresión en segundo plano); con `q` se vuelve al shell |
| `comprimir <archivo> [salida] [-t hilos]` | Comprime con `huffcli` (por defecto `<archivo>.huf`) |
| `descomprimir <archivo.huf> [salida] [-t hilos]` | Descomprime (por defecto `<archivo>.restaurado`) |

Los tres lanzan el programa como proceso hijo con `fork` + `execvp` y esperan con `waitpid`.
`help compresion` y `help edicion` muestran sus detalles.

Mientras el editor o el compresor corren, el shell **ignora Ctrl+C** (`sigaction` con `SIG_IGN`), como
hacen bash o sh con el programa en primer plano. Así Ctrl+C solo cancela la compresión y no
cierra el shell. El hijo restaura el comportamiento por defecto antes del `execvp`,
porque una señal ignorada se hereda a través de exec.

### Comandos del editor

Los comandos del Parcial 1 (`o`, `p`, `a`, `d`, `q`) funcionan igual. Se agregan:

| Comando | Qué hace |
|---|---|
| `c [salida]` | Comprime el documento en segundo plano (por defecto `<doc>.huf`) |
| `x <archivo.huf> [salida]` | Descomprime en segundo plano (por defecto `<archivo>.restaurado`) |
| `k` | Cancela la tarea en curso |
| `s` | Muestra el estado: progreso, fase, bloques y si hay un guardado pendiente |
| Ctrl+C | Con una tarea en curso, la cancela; sin tarea, sale del editor |

```
cmd> c
Comprimiendo 'notas.txt' -> 'notas.txt.huf' en segundo plano.
  [compresion] [################..............]  54%  (codificacion)
[c 54%] cmd> a linea escrita mientras se comprime
Linea agregada al final de 'notas.txt' (ahora 900001 linea(s)).
  El archivo esta siendo leido por la compresion en segundo plano: el cambio queda en memoria
  y el guardado en disco queda PENDIENTE hasta que la tarea termine.
  [compresion] TERMINADA: notas.txt -> notas.txt.huf (43200000 bytes originales, 42 bloques)
  [guardado pendiente] cambios escritos en 'notas.txt'.
```

---

## Estructura

```
include/huffman.h         núcleo Huffman: árbol, códigos, codificar y decodificar (sin hilos)
include/hf_concurrent.h   API asíncrona: iniciar, consultar progreso, cancelar y esperar un trabajo
include/file_guard.h      cerrojo lectores/escritor que protege el archivo del editor
src/hf_concurrent.c       motor concurrente: pool de hilos, ventana y escritor ordenado
src/huffcli.c             programa de terminal con barra de progreso
editor/src/               editor del Parcial 1 con el compresor integrado
shell/                    shell del Parcial 1 (sys_shell) con los comandos editor, comprimir y descomprimir
tests/run_tests.sh        pruebas de integridad del compresor
editor/pruebas_*.sh       pruebas del editor (Parcial 1) y de concurrencia (Parcial 2)
shell/pruebas_shell.sh    pruebas de la cadena shell -> editor -> compresión
evidencias/               salidas de las pruebas
```

---

## Cómo funciona

```
 Hilo del editor                 Hilo COORDINADOR                 N hilos TRABAJADORES
 ───────────────                 ────────────────                 ────────────────────
 hf_job_start() ──crea──────────► abre los archivos
   (toma file_guard)               │
 ciclo de eventos:                 │ FASE 1: conteo ───crea──────► toma el índice i (mutex)
   poll(timeout)                   │                               pread(bloque i)
   hf_job_get_status() ◄──cv/mtx───┤                               histograma LOCAL
   dibuja el progreso              │                               suma al global (mutex, 1 vez/bloque)
   el usuario sigue editando       │ join (barrera)
                                   │ construye el árbol (1 hilo; luego solo lectura)
                                   │ FASE 2: ─────────────crea──────► espera espacio en la ventana (cv_space)
                                   │ ESCRITOR ORDENADO                codifica el bloque i SIN lock
                                   │   espera la ranura i (cv_ready) ◄ deposita en slots[i % W]
                                   │   write() SIN lock
                                   │   next_write++ → cv_space ──────►
                                   │ join, fsync, close, rename(.tmp → destino)
                                   │ libera file_guard
                                   └─ publica el estado final (cv)
 hf_job_join() ◄───────────────────
```

1. **Conteo de frecuencias en paralelo.** Cada hilo cuenta su bloque en un histograma propio, sin
   lock, y lo suma al global dentro del mutex una sola vez por bloque (reducción local).
2. **Árbol.** Lo construye el coordinador a partir de las frecuencias globales. Desde ahí es de
   solo lectura, así que los hilos lo comparten sin protegerlo.
3. **Codificación en paralelo con salida ordenada.** Es un productor–consumidor con una ventana
   de `W = 2N` ranuras: los trabajadores codifican y depositan, y el coordinador escribe los bloques
   en orden `0, 1, 2…`. Un trabajador solo toma el bloque `i` si `i < next_write + W`, lo que acota
   la memoria a `W` bloques.
4. **Descompresión en paralelo.** Cada bloque comprimido empieza alineado a byte, así que se
   decodifica de forma independiente con el mismo esquema de ventana.

### Primitivas de sincronización

| Primitiva | Protege o señala | Quién espera | Quién señala |
|---|---|---|---|
| `job->mtx` | estado, fase, progreso, cancelación y mensaje del trabajo | el editor, en `hf_job_wait_update` | coordinador y trabajadores al avanzar |
| `job->cv` | "cambió el progreso o el estado" | el editor | `job_progress_add`, fin del trabajo |
| `pipe->mtx` | `next_task`, `next_write`, `slots[]`, `freq[]`, `abort` | — | — |
| `pipe->cv_space` | "hay una ranura libre en la ventana" | trabajadores | el escritor, tras escribir un bloque |
| `pipe->cv_ready` | "la ranura `next_write` está lista" | el escritor | el trabajador que deposita ese bloque |
| `file_guard` (mutex + contadores) | el archivo del documento en disco | nadie: `try_read`/`try_write` no bloquean | — |

- **Sin espera activa:** los hilos duermen en `pthread_cond_wait`, el editor en `poll()` y el
  progreso se consulta con `pthread_cond_timedwait`. Toda espera está dentro de un `while`.
- **Sin interbloqueo:** ningún hilo tiene dos mutex tomados a la vez, y una cancelación o un error
  despiertan a todos con `broadcast` en las dos variables de condición.
- **Señales:** los hilos del motor bloquean todas las señales. El editor las recibe con `signalfd`
  dentro de su `poll()` y `huffcli` con un hilo dedicado a `sigwait`; ambos piden la cancelación.
- **Archivos a medias:** la salida se escribe en `<salida>.tmp` y se renombra solo si todo salió bien.

### Decisiones de diseño

- **`pread` en vez de `read`:** lee en una posición dada sin usar el offset compartido del
  descriptor, así que varios hilos leen el mismo archivo sin lock.
- **`file_guard` propio en vez de `pthread_rwlock`:** el cerrojo se toma en el hilo del editor y se
  libera en el coordinador, y POSIX no permite desbloquear un `rwlock` desde otro hilo.
- **El guard se libera antes de publicar "terminado":** así el guardado pendiente del editor
  encuentra el archivo libre.

---

## Integración con el editor

`editor/` es el editor del Parcial 1 con estos cambios:

1. **Bucle principal con `poll()` + `read()` en lugar de `fgets()`.** `fgets` bloquea hasta que el
   usuario presiona Enter, y el progreso no podría actualizarse solo. `poll()` despierta cada 100 ms
   solo mientras hay una tarea en curso. Se usa `read()` porque el buffer de stdio puede guardar
   líneas que `poll()` no ve.
2. **Guardado diferido.** El editor guarda en disco con cada `a` o `d`. Mientras una compresión lee
   el archivo, el guardado intenta tomar el `file_guard` como escritor sin bloquear. Si está
   ocupado, el cambio queda en memoria y se escribe cuando la tarea termina. Así el `.huf`
   corresponde a la versión que había al empezar y ningún cambio se pierde.
3. **La lista de líneas no tiene mutex:** solo la toca el hilo del editor. El recurso compartido es
   el archivo en disco.
4. **Salida limpia:** con `q`, EOF o una señal se cancela o espera la tarea, se hace `pthread_join`,
   se escribe el guardado pendiente y se libera toda la memoria.
5. **Protecciones:** durante una tarea no se puede abrir otro archivo ni iniciar otra tarea, y nunca
   se comprime o descomprime sobre el documento abierto.

---

## Formato `.huf`

```
"HUF1" | u64 tamaño_original | u32 tamaño_bloque | u32 num_bloques | u64 freq[256]
por cada bloque: u32 bytes_originales | u32 bytes_codificados | datos...
```

- Enteros en little-endian.
- Se guarda la tabla de frecuencias y no el árbol: la construcción es determinista (desempate por
  índice de nodo), así que el descompresor reconstruye el mismo árbol.
- El archivo comprimido es idéntico sin importar el número de hilos.

---

## Pruebas

```
make test             # integridad del compresor
make -C editor test   # pruebas del editor (Parcial 1) y de concurrencia
make -C shell test    # shell: editor, comprimir y descomprimir, incluido Ctrl+C
make tsan             # ThreadSanitizer sobre el compresor
make -C editor tsan   # ThreadSanitizer sobre el editor
make asan             # AddressSanitizer
make valgrind         # memcheck y helgrind
```

| Prueba | Resultado |
|---|---|
| Integridad (`cmp` + `md5sum`): archivo vacío, 1 byte, un solo símbolo, texto, binario aleatorio, múltiplo exacto del bloque; con 1 y 8 hilos | Todas idénticas; un archivo corrupto se rechaza sin fallar |
| Editor: pruebas originales del Parcial 1 | 23 de 23 |
| Editor: concurrencia (edición durante la compresión, cancelación, SIGINT, protecciones) | 21 de 21 |
| Shell: comandos del Parcial 1, `editor`, `comprimir` y `descomprimir`; Ctrl+C no cierra el shell | 23 de 23 |
| ThreadSanitizer y Helgrind | 0 reportes |
| AddressSanitizer y Valgrind | 0 errores, 0 bytes en uso al salir |

Las salidas completas están en `evidencias/`.

---

## Rendimiento

Archivo de 200 MB, mediana de 10 rondas en orden aleatorio, en una VM con 12 CPUs lógicas
(6 núcleos × 2 hilos según `lscpu`).

| Hilos | Speedup | Eficiencia |
|---|---|---|
| 1 | 1.00× | 1.00 |
| 2 | 1.98× | 0.99 |
| 4 | 3.65× | 0.91 |
| 6 | 5.06× | 0.84 |
| 8 | 5.16× | 0.65 |

La ganancia se detiene a partir de 6 hilos, también en la fase de conteo, que no usa el escritor
ordenado. El límite coincide con los 6 núcleos
físicos, pero la causa no se aisló: la VM no permite controlar dónde corren los hilos, y también
pueden influir el ancho de banda de memoria o los límites de potencia.

**Limitaciones:** un solo trabajo a la vez, y las mediciones usan un archivo muy repetitivo en
memoria (`tmpfs`).
