#include "editor.h"
#include "comando.h"

/**
 * ========================================================
 * PUNTO DE ENTRADA PRINCIPAL
 * ========================================================
 * Editor de texto interactivo por CLI. Parcial 1: comandos base o, p, a, d
 * y q sobre el archivo en disco (open/read/write/lseek/ftruncate/close).
 * Parcial 2: compresion/descompresion Huffman concurrente en segundo plano
 * (comandos c, x, k, s) sin bloquear la edicion.
 */
int main(int argc, char *argv[]) {
    EstadoEditor editor;
    const char *archivo_a_abrir = (argc >= 2) ? argv[1] : NULL;

    editor_inicializar(&editor, archivo_a_abrir);
    comando_bucle_principal(&editor); /* 'q' termina el proceso desde adentro */

    editor_liberar(&editor); /* Solo se alcanza si stdin llega a EOF (Ctrl+D) */
    return 0;
}
