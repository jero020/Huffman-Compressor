#ifndef EDITOR_H
#define EDITOR_H

#include "estructura.h"
#include "file_guard.h"
#include "hf_concurrent.h"

/**
 * Módulo: Editor
 * Propósito: mantener el estado del documento abierto (estructura de datos +
 * nombre de archivo) y, desde el Parcial 2, la tarea de compresión/
 * descompresión que corre en segundo plano.
 *
 * PROTOCOLO DE CONCURRENCIA (Parcial 2):
 *  - 'estructura' (la lista de líneas en memoria) la toca SOLO el hilo
 *    principal (UI). Los hilos del compresor nunca la leen, así que no
 *    necesita mutex y el usuario puede seguir editando durante la tarea.
 *  - El recurso compartido es el ARCHIVO en disco 'nombreArchivo'. Lo
 *    protege 'guard' (lectores/escritor): la compresión lo toma como
 *    lectora durante todo el trabajo; cada guardado del editor intenta
 *    tomarlo como escritor SIN bloquear. Si está ocupado, el guardado se
 *    DIFIERE (guardado_pendiente) y se ejecuta cuando la tarea termina.
 *  - Resultado: el .huf corresponde exactamente a la versión del archivo
 *    que había al iniciar la compresión, y ningún cambio del usuario se
 *    pierde.
 */
typedef struct {
    EstructuraTexto *estructura;    // Documento en memoria (lista ligada de líneas)
    char *nombreArchivo;            // Ruta del archivo abierto actualmente

    /* --- Parcial 2: tarea en segundo plano --- */
    file_guard_t guard;             // Protege el archivo 'nombreArchivo' en disco
    hf_job_t    *tarea;             // Tarea de compresión/descompresión (NULL si no hay)
    hf_op_t      tarea_op;          // Qué está haciendo la tarea
    int          ultimo_decil;      // Último 10% de progreso mostrado
    int          guardado_pendiente;// Hubo cambios que no se pudieron escribir aún
    int          pendiente_vacio;   // El guardado pendiente debe dejar el archivo en 0 bytes
} EstadoEditor;

// Ciclo de vida
void editor_inicializar(EstadoEditor *e, const char* nombreArchivo);
void editor_liberar(EstadoEditor *e);

/* Guarda el documento en disco respetando el guard.
 * Retorna 0 si guardó, 1 si el guardado quedó DIFERIDO, -1 si hubo error. */
int  editor_guardar(EstadoEditor *e);

/* Espera (o cancela, si cancelar != 0) la tarea en segundo plano, hace
 * join, libera sus recursos, informa el resultado y ejecuta el guardado
 * pendiente si lo hay. No hace nada si no hay tarea. */
void editor_terminar_tarea(EstadoEditor *e, int cancelar);

#endif
