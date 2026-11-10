# Guion para el video de sustentación (~7 min)
### Proyecto: Compresor Huffman concurrente integrado al editor — Sistemas Operativos, Parcial 2

Guion para grabar hablando natural, no leyendo palabra por palabra. Cada bloque trae el tiempo
sugerido, lo que dices entre comillas, y entre corchetes `[...]` lo que se ve en pantalla. Los tiempos
suman unos 6:50.

---

## 1. Presentación (0:00 – 0:30)

**[Pantalla: `Readme.md` del repo, en el título]**

"Hola, somos [nombres del equipo] y esta es la sustentación del Parcial 2 de Sistemas Operativos.
Elegimos la Alternativa 1: un compresor Huffman que trabaja con varios hilos y está integrado al
editor del Parcial 1. El reto era aprovechar varios núcleos, no congelar el editor mientras comprime
y evitar condiciones de carrera si el usuario edita el archivo durante la compresión. Todo está en C
con pthreads: mutex, variables de condición y un cerrojo de lectores y escritor propio."

---

## 2. Arquitectura (0:30 – 1:45)

**[Pantalla: diagrama de la sección "Cómo funciona" del `Readme.md`]**

"Hay tres tipos de hilo: el del editor, un coordinador por trabajo y un grupo de trabajadores. Cuando
pido comprimir, el editor crea el coordinador y vuelve de inmediato a atender comandos."

"El archivo se divide en bloques de un mega. En la primera fase cada trabajador cuenta los caracteres
de su bloque en una tabla propia, sin lock, y la suma a la tabla global una sola vez por bloque. Eso
es una reducción local."

"Después el coordinador construye el árbol, que tarda unos trece microsegundos y desde ahí es de solo
lectura. En la segunda fase los trabajadores codifican en paralelo, pero terminan en cualquier orden.
Por eso usamos un productor–consumidor: dejan cada bloque en una ventana de tamaño fijo y el
coordinador los escribe en orden, durmiendo en una variable de condición si el siguiente aún no está
listo."

---

## 3. Demo en el editor (1:45 – 3:45)

**[Pantalla: terminal en `~/Sistemas-Operativos-Trabajos/Compresor_Huffman`]**

```
./editor/editor /tmp/demo.txt
```

"Este es el editor del Parcial 1 con cuatro comandos nuevos: c comprime, x descomprime, k cancela y s
muestra el estado. Voy a comprimir y, en el mismo instante, agregar una línea."

**[Pegar de una vez las dos líneas]**

```
c
a linea escrita mientras se comprime
```

**[Señalar el `[c 54%] cmd>`, la barra y la palabra PENDIENTE]**

"El prompt muestra el avance en tiempo real, y la línea que agregué quedó con el guardado pendiente.
El editor guarda en disco con cada línea nueva; si lo hiciera mientras la compresión lee el archivo,
el comprimido mezclaría dos versiones. Por eso el cambio se queda en memoria y se escribe al terminar…
ahí está: terminó y se escribió el guardado pendiente."

```
x /tmp/demo.txt.huf
```

"Descomprimo, también en segundo plano. Y si cancelo una compresión a la mitad, no queda ningún archivo
a medias, porque escribimos en un temporal y solo lo renombramos si todo salió bien."

**[Pegar de una vez las dos líneas]**

```
c
k
```

```
q
tail -n 1 /tmp/demo.txt
head -n 2000000 /tmp/demo.txt | md5sum
md5sum /tmp/demo.txt.restaurado
```

"Fuera del editor: la última línea es la que agregué, así que no se perdió, y el descomprimido tiene
el mismo MD5 que el documento sin esa línea. El comprimido corresponde a la versión que había al
empezar."

```
./huffcli -c /tmp/grande.txt /tmp/grande.huf -t 1
```

**[Presionar Ctrl+C a mitad de la barra]**

```
echo $?
ls /tmp/grande.huf*
```

"Con Control C el programa cancela, une los hilos, sale con código 130 y no deja archivos."

---

## 4. Código de sincronización (3:45 – 5:00)

**[Pantalla: VS Code, `src/hf_concurrent.c`]**

**[Mostrar `pipe_worker`]** "Si la ventana está llena, el trabajador duerme en `pthread_cond_wait`,
dentro de un `while` por los despertares espurios. No hay espera activa, y el trabajo pesado se hace
fuera del mutex."

**[Mostrar `ordered_writer`]** "El escritor espera la ranura del siguiente bloque, suelta el mutex,
escribe a disco y despierta a los trabajadores que esperaban espacio."

**[Mostrar `pipe_abort`]** "Si hay un error o una cancelación, un broadcast en las dos variables de
condición despierta a todos: ningún hilo queda dormido para siempre."

**[Mostrar `editor_guardar` en `editor/src/editor.c`]** "En el editor, guardar intenta tomar el
cerrojo del archivo sin bloquear; si la compresión lo tiene, el guardado queda pendiente."

"Y no puede haber interbloqueo: ningún hilo tiene dos mutex tomados a la vez, así que no hay espera
circular."

---

## 5. Pruebas (5:00 – 5:45)

**[Pantalla: carpeta `evidencias/`, abriendo `test.txt`, `editor_test.txt`, `tsan.txt` y `valgrind.txt`]**

"La prueba de integridad comprime y descomprime archivos vacíos, de un byte, de un solo carácter,
texto y datos aleatorios, con uno y ocho hilos, y compara con `cmp` y MD5. En el editor pasan las 23
pruebas del Parcial 1 y 21 nuevas de concurrencia. ThreadSanitizer y Helgrind no reportan
condiciones de carrera, y Valgrind reporta cero errores y cero bytes en uso al salir."

---

## 6. Rendimiento (5:45 – 6:30)

**[Pantalla: tabla de la sección "Rendimiento" del `Readme.md`]**

"Con un archivo de 200 megas y la mediana de diez rondas: 3,65 veces más rápido con cuatro hilos,
5,06 con seis y 5,16 con ocho. La ganancia se detiene en seis hilos. Medimos cada fase: el árbol no
influye y el conteo también se frena, así que el escritor ordenado no es la causa principal. Coincide
con los seis núcleos físicos de la máquina virtual, pero no logramos aislar la causa, así que lo
presentamos como hipótesis."

---

## 7. Cierre (6:30 – 6:50)

**[Pantalla: repositorio en GitHub]**

"No usamos `pthread_rwlock` porque el cerrojo se toma en el hilo del editor y se libera en el
coordinador, y POSIX no permite eso. Las limitaciones: un solo trabajo a la vez y mediciones con un
archivo muy repetitivo. El código, las pruebas y las evidencias están en el repositorio. Gracias."

---

## Checklist antes de grabar

- [ ] Trabajar solo desde la VM, nunca desde la carpeta de la Mac (`~/OrbStack/...`).
- [ ] `make clean && make` sin warnings.
- [ ] Crear los archivos de la demo:
  ```
  yes "Sistemas Operativos EAFIT compresion Huffman concurrente" | head -n 2000000 | cat -n > /tmp/demo.txt
  yes "Sistemas Operativos EAFIT compresion Huffman concurrente" | head -c 200000000 > /tmp/grande.txt
  rm -f /tmp/demo.txt.huf /tmp/demo.txt.restaurado /tmp/grande.huf*
  ```
- [ ] Abrir una vez `./editor/editor /tmp/demo.txt` para ver cuánto tarda en cargar, y salir con `q`.
  Después volver a crear `/tmp/demo.txt` para que no tenga la línea de prueba.
- [ ] Tener copiados los bloques de dos líneas (`c` + `a …` y `c` + `k`) para pegarlos.
- [ ] No usar `q` justo después de `c`: cancela la compresión.
- [ ] Cerrar programas pesados para que la compresión no se vea lenta.
- [ ] Terminal con fuente grande; VS Code abierto en `src/hf_concurrent.c` y `editor/src/editor.c`.
- [ ] No mostrar `~/.config/gh/hosts.yml` ni nada con el token de GitHub.
- [ ] Practicar una vez en voz alta con cronómetro.
