/*
 * huffman.h - Núcleo secuencial del algoritmo de Huffman.
 *
 * Este módulo NO tiene estado global ni usa hilos: todas sus funciones son
 * reentrantes y seguras para llamarse desde varios hilos a la vez siempre que
 * cada hilo use sus propios buffers. El árbol (hf_tree_t) es de SOLO LECTURA
 * una vez construido, por eso los hilos codificadores/decodificadores lo
 * comparten sin necesidad de mutex.
 */
#ifndef HUFFMAN_H
#define HUFFMAN_H

#include <stddef.h>
#include <stdint.h>

#define HF_SYMBOLS   256
#define HF_MAX_NODES (2 * HF_SYMBOLS - 1)

typedef struct {
    int      left, right;   /* índices de hijos, -1 si no hay */
    int      symbol;        /* byte 0..255 en hojas, -1 en nodos internos */
    uint64_t freq;
} hf_node_t;

typedef struct {
    uint16_t len;           /* longitud del código en bits (<= 255) */
    uint8_t  bits[32];      /* bits del código, MSB primero */
} hf_code_t;

typedef struct {
    hf_node_t nodes[HF_MAX_NODES];
    int       nnodes;
    int       root;         /* -1 si el archivo está vacío */
    hf_code_t codes[HF_SYMBOLS];
} hf_tree_t;

/* Construye el árbol de forma DETERMINISTA a partir de la tabla de
 * frecuencias (mismo resultado en compresor y descompresor). */
void hf_tree_build(hf_tree_t *t, const uint64_t freq[HF_SYMBOLS]);

/* Número de bits que ocupará la codificación de in[0..n). */
uint64_t hf_encoded_bits(const hf_tree_t *t, const uint8_t *in, size_t n);

/* Codifica in[0..n) en out (out_cap bytes, se pone en cero internamente).
 * Devuelve 0 si todo va bien, -1 si out_cap es insuficiente. */
int hf_encode(const hf_tree_t *t, const uint8_t *in, size_t n,
              uint8_t *out, size_t out_cap);

/* Decodifica exactamente out_len símbolos a partir de in[0..in_len).
 * Devuelve 0 si todo va bien, -1 si los datos están corruptos. */
int hf_decode(const hf_tree_t *t, const uint8_t *in, size_t in_len,
              uint8_t *out, size_t out_len);

#endif
