#include "estructura.h"
#include <stdlib.h> // Para asignación y liberación de memoria dinámica (malloc, free)
#include <string.h> // Para manipulación y operaciones con cadenas de texto (strlen, strcpy, etc.)
#include <fcntl.h>  // Para opciones de control de archivos (flags de open como O_WRONLY, O_CREAT, etc.)
#include <unistd.h> // Para acceso a la API POSIX del sistema operativo (system calls: open, read, write, close)
#include <stdio.h>  // Para funciones de entrada y salida formateadas (snprintf, sprintf)

#define SAFE_PRINT(log_out, offset, max_sz, ...) \
    do { \
        if (log_out && offset < max_sz) { \
            int cb = snprintf(log_out + offset, max_sz - offset, __VA_ARGS__); \
            if (cb > 0) offset += cb; \
            if (offset >= max_sz) offset = max_sz - 1; \
        } \
    } while(0)

EstructuraTexto* estructura_crear(void) {
    EstructuraTexto *alm = (EstructuraTexto*)malloc(sizeof(EstructuraTexto));
    alm->cabeza = NULL; alm->cola = NULL; alm->totalLineas = 0;
    estructura_insertar_linea(alm, NULL);
    return alm;
}

static void liberar_linea(NodoLinea *linea) {
    free(linea->texto);
    linea->texto = NULL; linea->longitud = 0;
}

void estructura_destruir(EstructuraTexto *alm) {
    if (!alm) return;
    NodoLinea *actual = alm->cabeza;
    while (actual) {
        NodoLinea *sig = actual->siguiente;
        liberar_linea(actual);
        free(actual);
        actual = sig;
    }
    free(alm);
}

static void enlazar_nueva_linea(EstructuraTexto *alm, NodoLinea *nueva_linea, NodoLinea *despues_de) {
    if (despues_de == NULL) {
        nueva_linea->siguiente = alm->cabeza; nueva_linea->anterior = NULL;
        if (alm->cabeza) alm->cabeza->anterior = nueva_linea;
        alm->cabeza = nueva_linea;
        if (!alm->cola) alm->cola = nueva_linea;
    } else {
        nueva_linea->siguiente = despues_de->siguiente; nueva_linea->anterior = despues_de;
        despues_de->siguiente = nueva_linea;
        if (nueva_linea->siguiente) nueva_linea->siguiente->anterior = nueva_linea;
        else alm->cola = nueva_linea;
    }
}

NodoLinea* estructura_insertar_linea(EstructuraTexto *alm, NodoLinea *despues_de) {
    NodoLinea *nueva_linea = (NodoLinea*)malloc(sizeof(NodoLinea));
    nueva_linea->texto = (char*)malloc(1);
    nueva_linea->texto[0] = '\0';
    nueva_linea->longitud = 0;
    enlazar_nueva_linea(alm, nueva_linea, despues_de);
    alm->totalLineas++;
    return nueva_linea;
}

void estructura_eliminar_linea(EstructuraTexto *alm, NodoLinea *linea) {
    if (!alm || !linea) return;
    if (linea->anterior) linea->anterior->siguiente = linea->siguiente;
    else alm->cabeza = linea->siguiente;
    if (linea->siguiente) linea->siguiente->anterior = linea->anterior;
    else alm->cola = linea->anterior;
    liberar_linea(linea); free(linea); alm->totalLineas--;
}

/* Envoltorios usados por el modo comando (o/p/a/d/q) de comando.c */
void estructura_texto_de_linea(NodoLinea *linea, char *buffer) {
    strcpy(buffer, linea->texto ? linea->texto : "");
}

void estructura_fijar_texto(NodoLinea *linea, const char *texto) {
    free(linea->texto);
    size_t len = strlen(texto);
    linea->texto = (char*)malloc(len + 1);
    strcpy(linea->texto, texto);
    linea->longitud = len;
}

int estructura_guardar_archivo(EstructuraTexto *alm, const char *nombre_archivo, char *log_out) {
    /* --- CALL SYSTEM: open() --- */
    int fd = open(nombre_archivo, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd == -1) return -1;

    size_t off = 0;
    size_t max_sz = 65000;
    SAFE_PRINT(log_out, off, max_sz, "==== GUARDANDO A DISCO ====\r\n");
    SAFE_PRINT(log_out, off, max_sz, "1. open(\"%s\", O_WRONLY|O_CREAT|O_TRUNC, 0644) => fd: %d\r\n", nombre_archivo, fd);

    char buffer[4096];
    int n_linea = 1;
    NodoLinea *actual = alm->cabeza;
    while (actual != NULL) {
        strcpy(buffer, actual->texto ? actual->texto : "");
        size_t len = strlen(buffer);
        buffer[len] = '\n';

        SAFE_PRINT(log_out, off, max_sz, "2.%d write(fd: %d, \"%s\", %zu bytes)\r\n", n_linea, fd, actual->texto, len + 1);

        /* --- CALL SYSTEM: write() --- */
        write(fd, buffer, len + 1);
        n_linea++;
        actual = actual->siguiente;
    }

    /* --- CALL SYSTEM: close() --- */
    close(fd);
    SAFE_PRINT(log_out, off, max_sz, "3. close(%d)\r\n==== GUARDADO COMPLETADO ====\r\n", fd);

    return 0;
}

static void limpiar_estructura_para_cargar(EstructuraTexto *alm) {
    NodoLinea *actual = alm->cabeza;
    while (actual != NULL) {
        NodoLinea *siguiente = actual->siguiente;
        liberar_linea(actual);
        free(actual);
        actual = siguiente;
    }
    alm->cabeza = NULL; alm->cola = NULL; alm->totalLineas = 0;
}

int estructura_cargar_archivo(EstructuraTexto *alm, const char *nombre_archivo, char *log_out) {
    /* --- CALL SYSTEM: open() --- */
    int fd = open(nombre_archivo, O_RDONLY);
    if (fd == -1) {
        if (log_out) sprintf(log_out, "open(\"%s\", O_RDONLY) => Fallo (el archivo no existe)\r\n", nombre_archivo);
        return -1;
    }
    limpiar_estructura_para_cargar(alm);

    size_t off = 0;
    size_t max_sz = 65000;
    SAFE_PRINT(log_out, off, max_sz, "==== CARGANDO ARCHIVO A MEMORIA ====\r\n");
    SAFE_PRINT(log_out, off, max_sz, "1. open(\"%s\", O_RDONLY) => fd: %d\r\n", nombre_archivo, fd);

    NodoLinea *linea_cargando = estructura_insertar_linea(alm, NULL);
    char buffer_leido[4096];
    char linea_buf[4096];
    size_t len = 0;
    ssize_t bytes_leidos;
    int n_linea = 1;

    /* --- CALL SYSTEM: read() --- */
    while ((bytes_leidos = read(fd, buffer_leido, sizeof(buffer_leido))) > 0) {
        for (ssize_t i = 0; i < bytes_leidos; i++) {
            char c = buffer_leido[i];
            if (c == '\n') {
                linea_buf[len] = '\0';
                SAFE_PRINT(log_out, off, max_sz, "2.%d linea detectada: \"%s\"\r\n", n_linea, linea_buf);
                estructura_fijar_texto(linea_cargando, linea_buf);
                linea_cargando = estructura_insertar_linea(alm, linea_cargando);
                len = 0;
                n_linea++;
            } else if (c != '\r') {
                if (len < 4095) linea_buf[len++] = c;
            }
        }
    }
    linea_buf[len] = '\0';
    if (len > 0) {
        SAFE_PRINT(log_out, off, max_sz, "2.%d linea final (sin salto): \"%s\"\r\n", n_linea, linea_buf);
        estructura_fijar_texto(linea_cargando, linea_buf);
    }

    if (linea_cargando->longitud == 0 && alm->totalLineas > 1) {
        estructura_eliminar_linea(alm, linea_cargando);
    }

    /* --- CALL SYSTEM: close() --- */
    close(fd);
    SAFE_PRINT(log_out, off, max_sz, "3. close(%d)\r\n==== CARGA COMPLETADA ====\r\n", fd);

    return 0;
}
