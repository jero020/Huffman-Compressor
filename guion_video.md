# Guion para el video de sustentación (~7 min)
### Proyecto: Compresor Huffman concurrente integrado al editor — Sistemas Operativos, Parcial 2

Guion para grabar hablando natural, no leyendo palabra por palabra. Cada bloque trae el tiempo
sugerido, lo que dices entre comillas, y entre corchetes `[...]` lo que se ve en pantalla. Los tiempos
suman unos 7 minutos.

---

## 1. Presentación (0:00 – 0:30)

**[Pantalla: `Readme.md` del repo, en el título]**

"Hola, somos [nombres del equipo] y esta es la sustentación del Parcial 2 de Sistemas Operativos.
Hicimos un programa que comprime archivos, es decir, que los hace más pequeños, usando el algoritmo
de Huffman. Para que sea rápido, el trabajo se reparte entre varios hilos, que son como varios
trabajadores haciendo partes de la tarea al mismo tiempo. Y lo conectamos al editor de texto y al
shell que hicimos en el Parcial 1."

"Teníamos tres metas: que fuera más rápido al usar varios hilos, que el editor no se quedara congelado
mientras comprime, y que no se dañara nada si el usuario sigue escribiendo mientras tanto."

---

## 2. Cómo funciona (0:30 – 1:45)

**[Pantalla: diagrama de la sección "Cómo funciona" del `Readme.md`]**

"Cuando el usuario pide comprimir, el editor no hace el trabajo él mismo: se lo pasa a un hilo
coordinador y sigue atendiendo al usuario. El coordinador reparte el trabajo entre varios hilos
trabajadores."

"El archivo se parte en pedazos de un mega, y cada trabajador toma un pedazo. Primero cuentan cuántas
veces aparece cada letra. Cada uno lleva su propia cuenta y solo al final la suma al total, así no se
estorban entre ellos."

"Con ese conteo se arma el árbol de Huffman, que le asigna un código corto a las letras que más se
repiten. Después cada trabajador comprime su pedazo. El problema es que no todos terminan al mismo
tiempo, pero los pedazos tienen que quedar en orden en el archivo final. Por eso el coordinador los va
guardando en orden, y si el siguiente todavía no está listo, espera sin gastar procesador."

---

## 3. Demo desde el shell (1:45 – 4:00)

**[Pantalla: terminal en `~/Sistemas-Operativos-Trabajos/Compresor_Huffman`]**

```
./shell/sys_shell
editor /tmp/demo.txt
```

"Este es el shell del Parcial 1. Con el comando editor, el shell crea un proceso hijo que abre
nuestro editor. Al editor le agregamos cuatro comandos: c para comprimir, x para descomprimir, k para
cancelar y s para ver cómo va. Voy a comprimir y, al mismo tiempo, escribir una línea nueva."

**[Pegar de una vez las dos líneas]**

```
c
a linea escrita mientras se comprime
```

**[Señalar el `[c 54%] cmd>`, la barra y la palabra PENDIENTE]**

"Aquí se ve el porcentaje de avance. Y la línea que escribí dice que su guardado quedó pendiente.
¿Por qué? Si el editor guardara el archivo mientras lo están comprimiendo, el resultado quedaría
mezclado entre la versión vieja y la nueva. Eso es lo que se llama una condición de carrera. Entonces
el cambio se guarda en memoria y se escribe en el disco apenas termina la compresión… y ahí está:
terminó y se guardó."

```
x /tmp/demo.txt.huf
```

"Ahora descomprimo, también sin bloquear el editor. Y si cancelo a la mitad, no queda ningún archivo
dañado, porque el programa escribe primero en un archivo temporal y solo al final lo pone con su
nombre definitivo."

**[Pegar de una vez las dos líneas]**

```
c
k
```

```
q
```

"Al salir del editor con q, volvemos al shell. Desde el shell también se puede comprimir
directamente, con el comando comprimir. Lo lanzo con un solo hilo para que tarde un poco."

```
comprimir /tmp/grande.txt -t 1
```

**[Presionar Ctrl+C a mitad de la barra, y luego escribir `hora`]**

"Si presiono Control C, la compresión se cancela sin dejar archivos a medias. Y fíjense que el shell
sigue funcionando: le pido la hora y responde. Eso es porque mientras el compresor trabaja, el shell
ignora Control C, igual que hace la terminal normal de Linux."

```
exit
tail -n 1 /tmp/demo.txt
head -n 2000000 /tmp/demo.txt | md5sum
md5sum /tmp/demo.txt.restaurado
```

"Ya fuera del shell compruebo dos cosas. La última línea del documento es la que escribí, así que no
se perdió. Y estos dos códigos son iguales: el MD5 es como una huella digital del archivo, y que
coincidan quiere decir que el archivo descomprimido es idéntico al original."

---

## 4. El código que evita los problemas (4:00 – 5:10)

**[Pantalla: VS Code, `src/hf_concurrent.c`]**

"Cuando varios hilos comparten información, hay que poner reglas para que no la dañen al tocarla al
mismo tiempo. Para eso usamos un mutex, que funciona como un candado: solo un hilo puede tenerlo a la
vez."

**[Mostrar `pipe_worker`]** "Aquí, si no hay espacio para dejar un pedazo terminado, el trabajador se
pone a dormir hasta que lo avisen. No se queda preguntando una y otra vez, que gastaría procesador sin
hacer nada útil."

**[Mostrar `ordered_writer`]** "Aquí el coordinador espera el siguiente pedazo, lo guarda en el archivo
y avisa a los trabajadores que ya hay espacio."

**[Mostrar `pipe_abort`]** "Si algo falla o el usuario cancela, esta parte despierta a todos los hilos
para que terminen. Así ninguno se queda esperando para siempre."

**[Mostrar `editor_guardar` en `editor/src/editor.c`]** "Y en el editor, antes de guardar se revisa si
el archivo está ocupado. Si lo está, el guardado queda pendiente en vez de esperar, por eso el editor
nunca se congela."

"Además, ningún hilo tiene dos candados al mismo tiempo. Eso evita que dos hilos se queden esperando
el uno al otro sin fin, lo que se llama un interbloqueo."

---

## 5. Pruebas (5:10 – 5:50)

**[Pantalla: carpeta `evidencias/`, abriendo `test.txt`, `editor_test.txt`, `tsan.txt` y `valgrind.txt`]**

"Para comprobar que todo funciona, comprimimos y descomprimimos muchos tipos de archivo: vacíos, de
un solo carácter, de texto y con datos al azar. Siempre salen idénticos al original. En el editor
pasan las 23 pruebas del Parcial 1 y 21 pruebas nuevas, como escribir mientras se comprime o cancelar
a la mitad. También usamos herramientas que revisan el programa mientras corre: no encontraron
condiciones de carrera ni memoria sin liberar."

---

## 6. Qué tan rápido es (5:50 – 6:35)

**[Pantalla: tabla de la sección "Rendimiento" del `Readme.md`]**

"Medimos con un archivo de 200 megas. Con cuatro hilos es 3,65 veces más rápido que con uno solo, con
seis hilos 5 veces, y con ocho casi lo mismo que con seis. O sea, después de seis hilos ya no mejora.
Medimos cada parte del programa por separado y descartamos las dos causas que sospechábamos en nuestro
código. Coincide con que la máquina virtual tiene seis núcleos reales, pero no pudimos comprobarlo del
todo, así que lo dejamos como una posible explicación."

---

## 7. Cierre (6:35 – 7:00)

**[Pantalla: repositorio en GitHub]**

"En resumen: el compresor reparte el trabajo entre varios hilos, se puede usar desde el shell y desde
el editor, el editor sigue respondiendo mientras
comprime y los cambios del usuario nunca se pierden ni dañan el archivo. Como límites, solo se puede
hacer una compresión a la vez, y medimos con un archivo de prueba muy repetitivo. Todo el código y las
pruebas están en el repositorio. Gracias."

---

## Checklist antes de grabar

- [ ] Trabajar solo desde la VM, nunca desde la carpeta de la Mac (`~/OrbStack/...`).
- [ ] `make clean && make` sin warnings (compila `huffcli`, el editor y `shell/sys_shell`).
- [ ] Crear los archivos de la demo:
  ```
  yes "Sistemas Operativos EAFIT compresion Huffman concurrente" | head -n 2000000 | cat -n > /tmp/demo.txt
  yes "Sistemas Operativos EAFIT compresion Huffman concurrente" | head -c 200000000 > /tmp/grande.txt
  rm -f /tmp/demo.txt.huf /tmp/demo.txt.restaurado /tmp/grande.huf*
  ```
- [ ] Abrir una vez `./editor/editor /tmp/demo.txt` para ver cuánto tarda en cargar y salir con `q`.
  Después volver a crear `/tmp/demo.txt` para que no tenga líneas de prueba.
- [ ] Tener copiados los bloques de dos líneas (`c` + `a …` y `c` + `k`) para pegarlos.
- [ ] No usar `q` justo después de `c`: cancela la compresión.
- [ ] Cerrar programas pesados para que la compresión no se vea lenta.
- [ ] Terminal con fuente grande; VS Code abierto en `src/hf_concurrent.c` y `editor/src/editor.c`.
- [ ] No mostrar `~/.config/gh/hosts.yml` ni nada con el token de GitHub.
- [ ] Practicar una vez en voz alta con cronómetro.
