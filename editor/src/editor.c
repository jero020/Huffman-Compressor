#include "editor.h"
#include <stdlib.h> // Para funciones estándar del sistema como malloc(), free()
#include <string.h> // Para strlen(), strcpy()
#include <stdio.h>  // Para printf() de los mensajes de la tarea en segundo plano
#include <fcntl.h>  // Para open() al dejar el archivo en 0 bytes
#include <unistd.h> // Para close()

/**
 * Inicializa el estado del editor: crea la estructura de datos en memoria y,
 * si se recibió un nombre de archivo, intenta cargarlo desde disco (open +
 * read + close, vía estructura_cargar_archivo). Si el archivo no existe
 * todavia, se crea vacio de inmediato en disco (open + O_CREAT), igual que
 * exige el comando 'o' del enunciado.
 */
void editor_inicializar(EstadoEditor *e, const char* nombreArchivo) {
    e->estructura = estructura_crear();

    const char *nombre = nombreArchivo ? nombreArchivo : "archivo.txt";
    e->nombreArchivo = malloc(strlen(nombre) + 1);
    strcpy(e->nombreArchivo, nombre);

    /* Parcial 2: estado de la tarea en segundo plano */
    e->tarea = NULL;
    e->tarea_op = HF_OP_COMPRESS;
    e->ultimo_decil = -1;
    e->guardado_pendiente = 0;
    e->pendiente_vacio = 0;
    if (file_guard_init(&e->guard) != 0) {
        fprintf(stderr, "Error: no se pudo inicializar la sincronizacion del archivo.\n");
        exit(1);
    }

    if (nombreArchivo) {
        int res = estructura_cargar_archivo(e->estructura, nombreArchivo, NULL);
        if (res == -1) {
            /* El archivo no existia: lo creamos vacio en disco de inmediato. */
            estructura_guardar_archivo(e->estructura, nombreArchivo, NULL);
        }
    }
}

int editor_guardar(EstadoEditor *e) {
    int vacio = (e->estructura->totalLineas == 0);

    /* Intento NO bloqueante: si hay una compresión leyendo el archivo, la UI
     * no se congela; el guardado se difiere hasta que la tarea termine. */
    if (file_guard_try_write(&e->guard) != 0) {
        e->guardado_pendiente = 1;
        e->pendiente_vacio = vacio;
        return 1;
    }
    int res = estructura_guardar_archivo(e->estructura, e->nombreArchivo, NULL);
    file_guard_write_release(&e->guard);
    if (res == 0) e->guardado_pendiente = 0;
    return res;
}

/* Ejecuta el guardado que quedó diferido mientras el archivo estaba ocupado. */
static void editor_guardar_pendiente(EstadoEditor *e) {
    if (!e->guardado_pendiente) return;

    int res;
    EstructuraTexto *t = e->estructura;
    if (e->pendiente_vacio && t->totalLineas == 1 && t->cabeza->longitud == 0) {
        /* El último cambio fue borrar todas las líneas: el archivo debe
         * quedar en 0 bytes, igual que con el guardado inmediato de 'd'. */
        if (file_guard_try_write(&e->guard) != 0) return;   /* sigue pendiente */
        int fd = open(e->nombreArchivo, O_WRONLY | O_CREAT | O_TRUNC, 0644);
        res = (fd == -1) ? -1 : 0;
        if (fd != -1) close(fd);
        file_guard_write_release(&e->guard);
        if (res == 0) e->guardado_pendiente = 0;
    } else {
        res = editor_guardar(e);
        if (res == 1) return;                                /* sigue pendiente */
    }
    e->pendiente_vacio = 0;
    if (res == 0) printf("  [guardado pendiente] cambios escritos en '%s'.\n", e->nombreArchivo);
    else          printf("  [guardado pendiente] ERROR al escribir '%s'.\n", e->nombreArchivo);
}

void editor_terminar_tarea(EstadoEditor *e, int cancelar) {
    if (!e->tarea) return;
    if (cancelar) hf_job_cancel(e->tarea);

    hf_status_t st;
    hf_state_t fin = hf_job_join(e->tarea, &st);   /* pthread_join + liberar todo */
    e->tarea = NULL;

    const char *op = (e->tarea_op == HF_OP_COMPRESS) ? "compresion" : "descompresion";
    if (fin == HF_STATE_DONE)
        printf("  [%s] TERMINADA: %s\n", op, st.message);
    else if (fin == HF_STATE_CANCELLED)
        printf("  [%s] CANCELADA (no quedo ningun archivo a medias).\n", op);
    else
        printf("  [%s] ERROR: %s\n", op, st.message);

    /* El guard ya fue liberado por el coordinador antes de publicar el
     * estado final, así que aquí el guardado diferido sí puede ejecutarse. */
    editor_guardar_pendiente(e);
    fflush(stdout);
}

void editor_liberar(EstadoEditor *e) {
    /* Nunca salir con hilos vivos: cancelar, unir y escribir lo pendiente. */
    editor_terminar_tarea(e, 1);
    estructura_destruir(e->estructura);
    if (e->nombreArchivo) free(e->nombreArchivo);
    e->estructura = NULL;
    e->nombreArchivo = NULL;
    file_guard_destroy(&e->guard);
}
