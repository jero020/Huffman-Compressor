#ifndef COMANDO_H
#define COMANDO_H

#include "editor.h"

/**
 * Bucle principal de interacción del editor: imprime el prompt "cmd>" y
 * ejecuta los comandos del Parcial 1 (o/p/a/d/q) y del Parcial 2 (c/x/k/s).
 *
 * Desde el Parcial 2 la entrada se espera con poll() sobre stdin y un
 * signalfd (SIGINT/SIGTERM), con un timeout de 100 ms mientras hay una tarea
 * en segundo plano: así el progreso se actualiza solo, sin espera activa y
 * sin bloquear la edición. Termina con 'q' (exit) o con EOF en stdin (si hay
 * una tarea en curso, espera a que termine antes de salir).
 */
void comando_bucle_principal(EstadoEditor *e);

#endif
