#ifndef ESTRUCTURA_H
#define ESTRUCTURA_H

#include <stddef.h> // Para tipos base como size_t y constantes como NULL
#include <stdio.h>  // Para operaciones de entrada/salida estándar

/**
 * Estructura para representar una línea de texto individual.
 * Cada línea guarda su propio buffer de caracteres en memoria dinámica
 * (una lista doblemente enlazada de líneas; el contenido de cada línea
 * es un simple buffer, sin granularidad de palabra: eso solo hacía falta
 * para el editor visual con cursor, que ya no existe).
 */
typedef struct NodoLinea {
    char *texto;                 // Contenido de la línea (siempre terminado en '\0')
    size_t longitud;             // Longitud actual del texto (sin contar el '\0')
    struct NodoLinea *anterior;  // Puntero a la línea anterior
    struct NodoLinea *siguiente; // Puntero a la línea siguiente
} NodoLinea;

/**
 * Estructura para representar el documento de texto completo.
 */
typedef struct {
    NodoLinea *cabeza;         // Primera línea del texto
    NodoLinea *cola;           // Última línea del texto
    int totalLineas;           // Contador del total de líneas
} EstructuraTexto;

// Funciones
EstructuraTexto* estructura_crear(void);
void estructura_destruir(EstructuraTexto *alm);
NodoLinea* estructura_insertar_linea(EstructuraTexto *alm, NodoLinea *despues_de);
void estructura_eliminar_linea(EstructuraTexto *alm, NodoLinea *linea);
int estructura_guardar_archivo(EstructuraTexto *alm, const char *nombre_archivo, char *log_out);
int estructura_cargar_archivo(EstructuraTexto *alm, const char *nombre_archivo, char *log_out);

// Acceso de lectura/escritura al texto plano de una línea (usado por el modo
// comando o/p/a/d/q de comando.c)
void estructura_texto_de_linea(NodoLinea *linea, char *buffer);
void estructura_fijar_texto(NodoLinea *linea, const char *texto);

#endif
