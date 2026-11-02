# Compresor Huffman Concurrente — Parcial 2 Sistemas Operativos (Alternativa 1)

Módulo de compresión/descompresión Huffman en C con `pthread`, **integrado
como tarea en segundo plano al editor del Parcial 1** (`editor/`).

```
make            # compila huffcli, demo_editor y el editor integrado (editor/editor)
make -C editor test   # pruebas del Parcial 1 + pruebas de concurrencia del editor
make -C editor tsan   # editor con ThreadSanitizer
./editor/editor documento.txt                  # editor con comandos c/x/k/s
make test       # integridad (cmp + md5) y determinismo
make tsan       # ThreadSanitizer  -> condiciones de carrera
make asan       # AddressSanitizer -> memoria inválida y fugas
make valgrind   # memcheck + helgrind
./huffcli -c archivo.txt archivo.huf -t 4      # comprimir con 4 hilos
./huffcli -d archivo.huf restaurado.txt        # descomprimir
./demo_editor documento.txt                    # simulación de la integración
```

---

## 1. Pasos para completar el parcial

| # | Paso | Estado en esta base |
|---|------|---------------------|
| 1 | Formar el equipo (máx. 5) y elegir **Alternativa 1** | — |
| 2 | Entender Huffman secuencial (frecuencias → árbol → códigos) | ✅ `src/huffman.c` |
| 3 | Diseñar el formato de archivo `.huf` por bloques | ✅ (sección 4) |
| 4 | Paralelizar el **conteo de frecuencias** (reducción local) | ✅ `freq_worker` |
| 5 | Paralelizar la **codificación** + **escritura ordenada** | ✅ `pipe_worker` + `ordered_writer` |
| 6 | Descompresión paralela (índice de bloques) | ✅ `do_decompress` |
| 7 | API asíncrona (start / status / cancel / join) | ✅ `include/hf_concurrent.h` |
| 8 | Proteger el archivo ante guardado concurrente | ✅ `file_guard` |
| 9 | Señales (Ctrl+C) y liberación de recursos | ✅ `sigwait` / `signalfd`, `.tmp` + `rename` |
| 10 | Pruebas: md5/cmp, TSan, ASan, Valgrind, Helgrind | ✅ `make test/tsan/asan/valgrind`, `make -C editor test/tsan` |
| 11 | **Integrar al editor del Parcial 1** (progreso real, guardado diferido) | ✅ `editor/` (sección 5) |
| 12 | Medir speedup (1, 2, 4, 8 hilos) y tabularlo | ⏳ |
| 13 | Preparar sustentación (video o presencial en 19-424) | ⏳ ver sección 7 |
| 14 | **Cada integrante** sube las evidencias a EAFIT Interactiva (semana 12) | ⏳ |

Recuerda: asistir a las 3 horas de consulta en clase es obligatorio antes de pedir asesoría por Teams.

---

## 2. Estructura

```
include/huffman.h        núcleo Huffman (sin hilos, reentrante)
include/file_guard.h     cerrojo lectores/escritor del archivo del editor
include/hf_concurrent.h  API pública asíncrona (lo único que usa el editor)
src/huffman.c
src/file_guard.c
src/hf_concurrent.c      motor concurrente (pool, ventana, escritor ordenado)
src/huffcli.c            CLI con barra de progreso y Ctrl+C
src/demo_editor.c        mini-editor de referencia (patrón de integración)
tests/run_tests.sh       pruebas de integridad
editor/                  EDITOR DEL PARCIAL 1 CON EL COMPRESOR INTEGRADO
  src/estructura.c/.h    lista doblemente ligada de líneas (sin cambios)
  src/editor.c/.h        estado del editor + guard + tarea en segundo plano + guardado diferido
  src/comando.c/.h       bucle poll()+signalfd, comandos o/p/a/d/q + c/x/k/s
  src/main.c
  pruebas_editor.sh      pruebas originales del Parcial 1 (23, siguen pasando)
  pruebas_concurrencia.sh pruebas del Parcial 2 (21): carrera, cancelación, señal, md5
```

---

## 3. Arquitectura de hilos

```
 Hilo UI (editor)                 Hilo COORDINADOR                 Pool de N TRABAJADORES
 ────────────────                 ────────────────                 ──────────────────────
 hf_job_start() ──pthread_create──► abre archivos
   (toma file_guard)                │
 ciclo de eventos:                  │ FASE 1: conteo ───crea──────► toma índice i (mutex)
   poll(timeout)                    │                               pread(bloque i)
   hf_job_get_status() ◄──cv/mtx────┤                               histograma LOCAL
   dibuja barra                     │                               suma al global (mutex, 1 vez/bloque)
   el usuario sigue editando        │ join (barrera)
                                    │ construye árbol (1 hilo, luego solo-lectura)
                                    │ FASE 2: ─────────────crea──────► espera espacio en ventana (cv_space)
                                    │ ESCRITOR ORDENADO                codifica bloque i SIN lock
                                    │   espera ranura i (cv_ready) ◄── deposita en slots[i % W]
                                    │   write() SIN lock
                                    │   next_write++ → cv_space ────►
                                    │ join, fsync, close, rename(.tmp → destino)
                                    │ libera file_guard
                                    └─ publica estado final (cv)
 hf_job_join() ◄────────────────────
```

### Protocolo de concurrencia (qué protege cada primitiva)

| Primitiva | Protege / señala | Quién espera | Quién señala |
|-----------|------------------|--------------|--------------|
| `job->mtx` | `state, phase, done, total, cancel, message` | UI en `hf_job_wait_update` | coordinador / trabajadores al avanzar |
| `job->cv` | "cambió el progreso o el estado" | UI | `job_progress_add`, fin del trabajo |
| `pipe->mtx` | `next_task, next_write, slots[], freq[], abort` | — | — |
| `pipe->cv_space` | "hay ranura libre en la ventana" | trabajadores | escritor tras escribir un bloque |
| `pipe->cv_ready` | "la ranura `next_write` está lista" | escritor | trabajador que deposita el bloque `next_write` |
| `file_guard` (mutex+cv) | archivo del documento en disco | `write_acquire` | `read_release` / `write_release` |

**Invariante de la ventana:** un trabajador solo toma el bloque `i` si
`i < next_write + W`. Por eso, cuando deposita en `slots[i % W]`, el bloque
`i - W` ya fue escrito y esa ranura está vacía. Esto da **memoria acotada**
(como máximo `W = 2N` bloques en RAM) y es el problema clásico
**productor-consumidor** con buffer acotado.

---

## 4. Formato `.huf`

```
"HUF1" | u64 tamaño_original | u32 tamaño_bloque | u32 num_bloques | u64 freq[256]
por cada bloque: u32 bytes_originales | u32 bytes_codificados | datos...
```

* Todos los enteros en little-endian (portables entre máquinas).
* Se guarda la **tabla de frecuencias** (no el árbol): el descompresor
  reconstruye exactamente el mismo árbol porque la construcción es
  determinista (desempate por índice de nodo).
* Cada bloque empieza alineado a byte → los bloques se **decodifican de
  forma independiente y en paralelo**.
* La salida es **idéntica sin importar el número de hilos** (se verifica en `make test`).

---

## 5. Editor integrado (`editor/`)

Es una copia del editor del Parcial 1 (`editorEAFIT`) con el compresor integrado.
Los comandos originales `o/p/a/d/q` funcionan igual (sus 23 pruebas siguen pasando).

| Comando | Qué hace |
|---|---|
| `c [salida]` | Comprime el documento en segundo plano (por defecto `<doc>.huf`) |
| `x <archivo.huf> [salida]` | Descomprime en segundo plano (por defecto `<archivo>.restaurado`) |
| `k` | Cancela la tarea en curso (no deja archivos a medias) |
| `s` | Estado: barra, fase, bloques, guardado pendiente |
| Ctrl+C | Con tarea: la cancela. Sin tarea: sale limpiamente |

Ejemplo real:

```
cmd> c
Comprimiendo 'notas.txt' -> 'notas.txt.huf' en segundo plano.
  [compresion] [################..............]  54%  (codificacion)
[c 54%] cmd> a linea escrita mientras se comprime
Linea agregada al final de 'notas.txt' (ahora 900001 linea(s)).
  El archivo esta siendo leido por la compresion en segundo plano: el cambio queda en memoria
  y el guardado en disco queda PENDIENTE hasta que la tarea termine.
  [compresion] [############################..]  94%  (codificacion)
  [compresion] TERMINADA: notas.txt -> notas.txt.huf (43200000 bytes originales, 42 bloques)
  [guardado pendiente] cambios escritos en 'notas.txt'.
cmd>
```

### Cambios respecto al editor del Parcial 1 y por qué

1. **Bucle principal con `poll()` + `read()` en vez de `fgets()`.** `fgets` bloquea hasta que el
   usuario presiona Enter, así que el progreso no podría actualizarse solo. `poll()` con timeout de
   100 ms (solo mientras hay tarea; si no, espera indefinidamente) refresca el progreso sin espera
   activa. Se usa `read()` y no stdio porque stdio puede guardar líneas en su buffer interno que
   `poll()` no ve.
2. **Guardado con `editor_guardar()`.** El editor guarda en disco en cada `a`/`d`. Mientras una
   compresión lee el archivo, ese guardado lo corrompería. Ahora el guardado intenta tomar el
   `file_guard` como escritor **sin bloquear**; si la compresión lo tiene como lectora, el cambio
   queda en memoria y el guardado se **difiere** hasta que la tarea termina. Así:
   * la UI nunca se congela;
   * el `.huf` corresponde exactamente a la versión que había al iniciar (verificado con md5);
   * ningún cambio del usuario se pierde.
3. **La lista de líneas solo la toca el hilo de UI.** Los hilos del compresor trabajan sobre el
   archivo en disco, nunca sobre la estructura en memoria → no necesita mutex.
4. **Señales con `signalfd`.** `SIGINT/SIGTERM` se bloquean antes de crear hilos y se leen como un
   descriptor más en el mismo `poll()`.
5. **Salida limpia.** `q`, EOF o una señal: se cancela/espera la tarea, `pthread_join`, se escribe
   el guardado pendiente y se libera todo (Valgrind: 0 bytes en uso al salir).
6. **Protecciones:** no se puede abrir otro archivo (`o`) ni lanzar otra tarea mientras hay una en
   curso, ni comprimir/descomprimir sobre el documento abierto.

### Integrar en otro proyecto

Copia `include/` y `src/huffman.c, src/hf_concurrent.c, src/file_guard.c`, compila con `-pthread`
y sigue el patrón de `editor/src/comando.c` (`revisar_tarea`, `imprimir_prompt`) y
`editor/src/editor.c` (`editor_guardar`, `editor_terminar_tarea`).

---

## 6. Evidencias para la rúbrica

| Criterio (pts) | Evidencia en esta base |
|---|---|
| Concurrencia en Huffman (25) | Fase 1 con reducción local, fase 2 con pool, salida ordenada. Medir speedup con `huffcli -t 1/2/4/8` (en una prueba con 2 núcleos: 2.09 s → 1.14 s, 1.8×). |
| Sincronización (25) | Todos los `wait` dentro de `while`; cero espera activa (`cond_wait`, `cond_timedwait`, `poll`). `make tsan` y Helgrind sin reportes. Sin interbloqueo: nunca se tienen dos mutex a la vez (sin orden circular posible). |
| Integración con el editor (20) | `editor/`: editor del Parcial 1 con `c/x/k/s`, progreso en el prompt y barra cada 10%, edición durante la compresión con guardado diferido, snapshot consistente (md5). `make -C editor test`: 23 pruebas del P1 + 21 de concurrencia; `make -C editor tsan` sin reportes. |
| Corrección algorítmica (15) | `make test`: `cmp` + `md5sum` en vacío, 1 byte, 1 símbolo, texto, binario aleatorio, múltiplo exacto de bloque, bloques de 4 KB; archivo corrupto rechazado. |
| Errores y recursos (15) | `make asan` / `make valgrind` sin fugas; `.tmp` + `rename()` atómico; `fsync`/`close` verificados; `pthread_join` de todos los hilos; cancelación por Ctrl+C (exit 130) sin dejar archivos a medias. |

---

## 7. Preguntas probables en la sustentación (y respuestas)

* **¿Por qué reducción local y no un mutex por byte?** Un lock por byte serializa todo y la contención mata el paralelismo. Con histograma local hay 1 lock por bloque (256 sumas), así que la contención es despreciable.
* **¿Por qué no usar atomics para las frecuencias?** Funcionaría, pero aún habría *false sharing* y tráfico de caché por cada byte; la reducción local es más rápida y además es lo que pide el enunciado.
* **¿Cómo garantizan el orden de salida?** El coordinador escribe `0,1,2...` esperando con `cv_ready` a que la ranura `next_write` esté lista; los trabajadores pueden terminar en cualquier orden.
* **¿Por qué la ventana acotada?** Sin ella, si el bloque 0 es lento, los demás se acumularían en RAM sin límite. Con `W = 2N` la memoria queda acotada y los trabajadores esperan (sin busy-wait) con `cv_space`.
* **¿Por qué `pread` y no `read`?** `pread` no usa el offset compartido del descriptor: varios hilos leen el mismo `fd` sin lock.
* **¿Por qué el guard no es `pthread_rwlock`?** Se adquiere en el hilo UI y se libera en el coordinador; desbloquear un `rwlock` desde otro hilo es comportamiento indefinido en POSIX. Con mutex + cv sí es válido.
* **¿Por qué se libera el guard ANTES de publicar "terminado"?** Para que cuando la UI vea el estado final, el guardado pendiente pueda ejecutarse sin encontrar el archivo ocupado.
* **¿Qué pasa si llega Ctrl+C con un hilo dentro de un mutex?** No puede pasar: los hilos del motor bloquean todas las señales; solo un hilo dedicado (`sigwait`) o `signalfd` las recibe y llaman `hf_job_cancel()`.
* **¿Por qué `while` y no `if` alrededor de `pthread_cond_wait`?** Despertares espurios, y porque otro hilo puede cambiar la condición entre la señal y la readquisición del mutex.
* **¿Por qué el editor no bloquea la lista de líneas con un mutex?** Porque solo un hilo (la UI) la toca. El recurso compartido real es el archivo en disco, y ese sí está protegido por `file_guard`.
* **¿Qué pasa si el usuario guarda mientras se comprime?** `file_guard_try_write` devuelve `EBUSY`, el cambio queda en memoria y se marca `guardado_pendiente`; al terminar la tarea (después del `join`) se escribe.
* **¿Por qué `poll` y no `fgets`?** `fgets` bloquea la UI hasta Enter; `poll` con timeout permite refrescar el progreso sin espera activa.
* **¿Puede haber deadlock?** Ningún hilo sostiene dos mutex a la vez (`job->mtx` y `pipe->mtx` nunca se anidan), así que no hay espera circular. `abort` hace `broadcast` en ambas cv para que nadie quede dormido.
