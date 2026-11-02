# Compresor Huffman Concurrente — Parcial 2 Sistemas Operativos (Alternativa 1)

Módulo de compresión/descompresión Huffman en C con `pthread`, pensado para
integrarse como tarea en segundo plano al editor del Parcial 1.

```
make            # compila huffcli y demo_editor
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
| 10 | Pruebas: md5/cmp, TSan, ASan, Valgrind, Helgrind | ✅ `make test/tsan/asan/valgrind` |
| 11 | **Integrar a TU editor del Parcial 1** (barra de progreso real) | ⏳ ver sección 5 |
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
src/demo_editor.c        mini-editor que demuestra la integración
tests/run_tests.sh       pruebas de integridad
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

## 5. Cómo integrarlo a tu editor (Parcial 1)

1. Copia `include/` y `src/huffman.c, src/hf_concurrent.c, src/file_guard.c` a tu proyecto y agrégalos al Makefile con `-pthread`.
2. Crea **un** `file_guard_t` por documento abierto (`file_guard_init`).
3. **Guardar** del editor → usa `file_guard_try_write()`. Si devuelve `EBUSY`, marca *guardado pendiente* y muéstralo en la barra de estado.
4. **Comando comprimir** → guarda primero si hay cambios y luego `hf_job_start(HF_OP_COMPRESS, ruta, ruta.huf, 0, 0, &guard)`.
5. En el ciclo principal (sin busy-waiting):
   * **ncurses:** `timeout(100);` y luego `getch()` devuelve `ERR` cada 100 ms → ahí llamas `hf_job_get_status()` y dibujas la barra.
   * **consola:** `poll()` con timeout, como en `demo_editor.c`.
   * **GTK:** `g_timeout_add(100, callback, job)`.
6. Cuando `st.state != HF_STATE_RUNNING` → `hf_job_join()` y, si había guardado pendiente, ejecútalo.
7. Al salir del editor con un trabajo activo → `hf_job_cancel()` + `hf_job_join()`.
8. Señales: bloquea `SIGINT/SIGTERM` al inicio de `main` (antes de crear hilos) y atiéndelas con `signalfd` o un hilo con `sigwait`.

> **Regla de oro:** el buffer en memoria del editor lo toca **solo** el hilo de UI;
> el motor de compresión **solo** toca el archivo en disco. Así el usuario
> puede seguir editando durante la compresión y no se necesita un lock
> sobre el buffer; el único recurso compartido es el archivo, protegido por `file_guard`.

---

## 6. Evidencias para la rúbrica

| Criterio (pts) | Evidencia en esta base |
|---|---|
| Concurrencia en Huffman (25) | Fase 1 con reducción local, fase 2 con pool, salida ordenada. Medir speedup con `huffcli -t 1/2/4/8` (en una prueba con 2 núcleos: 2.09 s → 1.14 s, 1.8×). |
| Sincronización (25) | Todos los `wait` dentro de `while`; cero espera activa (`cond_wait`, `cond_timedwait`, `poll`). `make tsan` y Helgrind sin reportes. Sin interbloqueo: nunca se tienen dos mutex a la vez (sin orden circular posible). |
| Integración con el editor (20) | `demo_editor`: editar durante la compresión, guardado diferido, barra de progreso, snapshot consistente. |
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
* **¿Puede haber deadlock?** Ningún hilo sostiene dos mutex a la vez (`job->mtx` y `pipe->mtx` nunca se anidan), así que no hay espera circular. `abort` hace `broadcast` en ambas cv para que nadie quede dormido.
