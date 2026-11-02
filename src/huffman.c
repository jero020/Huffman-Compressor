/*
 * huffman.c - Construcción del árbol, codificación y decodificación.
 */
#include "huffman.h"

#include <string.h>

/* ¿El nodo a es "menor" que el b? Desempate por índice => determinismo. */
static int node_less(const hf_tree_t *t, int a, int b)
{
    if (t->nodes[a].freq != t->nodes[b].freq)
        return t->nodes[a].freq < t->nodes[b].freq;
    return a < b;
}

static void set_bit(uint8_t *bits, unsigned pos, int value)
{
    uint8_t mask = (uint8_t)(0x80u >> (pos & 7u));
    if (value) bits[pos >> 3] |= mask;
    else       bits[pos >> 3] &= (uint8_t)~mask;
}

static void assign_codes(hf_tree_t *t, int node, hf_code_t *cur)
{
    const hf_node_t *nd = &t->nodes[node];
    if (nd->left < 0 && nd->right < 0) {          /* hoja */
        t->codes[nd->symbol] = *cur;
        return;
    }
    if (nd->left >= 0) {                          /* izquierda = 0 */
        set_bit(cur->bits, cur->len, 0);
        cur->len++;
        assign_codes(t, nd->left, cur);
        cur->len--;
    }
    if (nd->right >= 0) {                         /* derecha = 1 */
        set_bit(cur->bits, cur->len, 1);
        cur->len++;
        assign_codes(t, nd->right, cur);
        cur->len--;
    }
}

void hf_tree_build(hf_tree_t *t, const uint64_t freq[HF_SYMBOLS])
{
    int active[HF_MAX_NODES];
    int nact = 0;

    memset(t, 0, sizeof *t);
    t->root = -1;

    for (int s = 0; s < HF_SYMBOLS; s++) {
        if (freq[s] == 0) continue;
        t->nodes[t->nnodes] = (hf_node_t){ -1, -1, s, freq[s] };
        active[nact++] = t->nnodes++;
    }
    if (nact == 0) return;                        /* archivo vacío */

    if (nact == 1) {                              /* un solo símbolo: código "0" */
        int leaf = active[0];
        t->nodes[t->nnodes] = (hf_node_t){ leaf, -1, -1, t->nodes[leaf].freq };
        t->root = t->nnodes++;
    } else {
        /* Selección de los dos mínimos: O(n^2) con n <= 256, simple y suficiente. */
        while (nact > 1) {
            int a = 0, b = 1;
            if (node_less(t, active[b], active[a])) { a = 1; b = 0; }
            for (int i = 2; i < nact; i++) {
                if (node_less(t, active[i], active[a]))      { b = a; a = i; }
                else if (node_less(t, active[i], active[b])) { b = i; }
            }
            int na = active[a], nb = active[b];
            t->nodes[t->nnodes] = (hf_node_t){ na, nb, -1,
                                               t->nodes[na].freq + t->nodes[nb].freq };
            /* quitar a y b (primero el índice mayor) y agregar el nuevo nodo */
            int hi = a > b ? a : b, lo = a > b ? b : a;
            active[hi] = active[--nact];
            active[lo] = active[--nact];
            active[nact++] = t->nnodes++;
        }
        t->root = active[0];
    }

    hf_code_t cur;
    memset(&cur, 0, sizeof cur);
    assign_codes(t, t->root, &cur);
}

uint64_t hf_encoded_bits(const hf_tree_t *t, const uint8_t *in, size_t n)
{
    uint64_t bits = 0;
    for (size_t i = 0; i < n; i++) bits += t->codes[in[i]].len;
    return bits;
}

int hf_encode(const hf_tree_t *t, const uint8_t *in, size_t n,
              uint8_t *out, size_t out_cap)
{
    uint64_t pos = 0;
    memset(out, 0, out_cap);
    for (size_t i = 0; i < n; i++) {
        const hf_code_t *c = &t->codes[in[i]];
        if (((pos + c->len + 7) >> 3) > out_cap) return -1;
        for (unsigned k = 0; k < c->len; k++) {
            if (c->bits[k >> 3] & (0x80u >> (k & 7u)))
                out[pos >> 3] |= (uint8_t)(0x80u >> (pos & 7u));
            pos++;
        }
    }
    return 0;
}

int hf_decode(const hf_tree_t *t, const uint8_t *in, size_t in_len,
              uint8_t *out, size_t out_len)
{
    if (out_len == 0) return 0;
    if (t->root < 0) return -1;

    uint64_t pos = 0, total = (uint64_t)in_len * 8u;
    for (size_t i = 0; i < out_len; i++) {
        int node = t->root;
        while (t->nodes[node].left >= 0 || t->nodes[node].right >= 0) {
            if (pos >= total) return -1;
            int bit = (in[pos >> 3] >> (7u - (pos & 7u))) & 1;
            pos++;
            node = bit ? t->nodes[node].right : t->nodes[node].left;
            if (node < 0) return -1;
        }
        out[i] = (uint8_t)t->nodes[node].symbol;
    }
    return 0;
}
