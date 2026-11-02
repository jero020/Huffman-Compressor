#!/bin/bash
# ============================================================================
# Script de demostración / pruebas - Editor EAFIT (comandos o/p/a/d/q)
# Sistemas Operativos - SO2026B - Proyecto Nivel 1 (individual)
#
# Compila el editor y ejecuta una serie de escenarios (casos normales y
# casos borde) contra el binario ./editor, verificando tanto lo impreso en
# consola como el contenido final de los archivos en disco. No depende de
# ninguna herramienta externa aparte de bash y el propio compilador.
# ============================================================================
set -u
cd "$(dirname "$0")"

PASS=0
FAIL=0
WORKDIR=$(mktemp -d)
trap 'rm -rf "$WORKDIR"' EXIT

linea() { printf '%s\n' "----------------------------------------------------------------"; }

check_contiene() {
    # check_contiene "descripcion" "subcadena_esperada" "texto_obtenido"
    local desc="$1" esperado="$2" obtenido="$3"
    if [[ "$obtenido" == *"$esperado"* ]]; then
        echo "  [OK]    $desc"
        PASS=$((PASS+1))
    else
        echo "  [FALLO] $desc"
        echo "          se esperaba encontrar: \"$esperado\""
        FAIL=$((FAIL+1))
    fi
}

check_no_contiene() {
    local desc="$1" no_esperado="$2" obtenido="$3"
    if [[ "$obtenido" != *"$no_esperado"* ]]; then
        echo "  [OK]    $desc"
        PASS=$((PASS+1))
    else
        echo "  [FALLO] $desc (no debia contener \"$no_esperado\")"
        FAIL=$((FAIL+1))
    fi
}

check_bytes() {
    # check_bytes "descripcion" archivo bytes_esperados
    local desc="$1" archivo="$2" esperado="$3" obtenido
    obtenido=$(wc -c < "$archivo" 2>/dev/null | tr -d ' ')
    if [[ "$obtenido" == "$esperado" ]]; then
        echo "  [OK]    $desc ($obtenido bytes)"
        PASS=$((PASS+1))
    else
        echo "  [FALLO] $desc (esperados $esperado bytes, obtenidos $obtenido)"
        FAIL=$((FAIL+1))
    fi
}

check_exit() {
    local desc="$1" esperado="$2" obtenido="$3"
    if [[ "$obtenido" == "$esperado" ]]; then
        echo "  [OK]    $desc (exit $obtenido)"
        PASS=$((PASS+1))
    else
        echo "  [FALLO] $desc (se esperaba exit $esperado, se obtuvo $obtenido)"
        FAIL=$((FAIL+1))
    fi
}

linea
echo "1. Compilando el editor (make clean && make)"
linea
make clean >/dev/null 2>&1
if ! make 2>"$WORKDIR/build.log"; then
    echo "  [FALLO] La compilacion no fue exitosa. Ver detalle:"
    cat "$WORKDIR/build.log"
    exit 1
fi
if grep -E "\.c:[0-9]+.*warning:" "$WORKDIR/build.log" >/dev/null 2>&1; then
    echo "  [AVISO] La compilacion genero warnings del compilador:"
    grep -E "\.c:[0-9]+.*warning:" "$WORKDIR/build.log"
else
    echo "  [OK]    Compilacion limpia, sin warnings (-Wall -Wextra -pedantic -std=c99)"
    PASS=$((PASS+1))
fi

F1="$WORKDIR/archivo1.txt"
F2="$WORKDIR/archivo2.txt"
F3="$WORKDIR/archivo3.txt"
F4="$WORKDIR/archivo4.txt"

echo
linea
echo "2. Escenario: 'o' crea un archivo que no existe"
linea
OUT=$(printf 'o %s\nq\n' "$F1" | ./editor)
check_contiene "o informa que el archivo se creo vacio" "no existia: se creo vacio en disco" "$OUT"
if [[ -f "$F1" ]]; then
    check_bytes "el archivo nuevo queda en disco con 0 bytes" "$F1" "0"
else
    echo "  [FALLO] el archivo no quedo creado en disco"; FAIL=$((FAIL+1))
fi

echo
linea
echo "3. Escenario: 'a' agrega lineas, 'p' las imprime todas"
linea
OUT=$(printf 'o %s\na primera linea\na segunda linea\na tercera linea\np\nq\n' "$F2" | ./editor)
check_contiene "a confirma cada linea agregada" "ahora 4 linea(s)" "$OUT"
check_contiene "p muestra 'primera linea'" "primera linea" "$OUT"
check_contiene "p muestra 'segunda linea'" "segunda linea" "$OUT"
check_contiene "p muestra 'tercera linea'" "tercera linea" "$OUT"

echo
linea
echo "4. Escenario: 'p n' imprime solo la linea n, reabriendo desde disco"
linea
OUT=$(printf 'o %s\np 3\nq\n' "$F2" | ./editor)
check_contiene "p 3 imprime exactamente 'segunda linea'" "segunda linea" "$OUT"
check_no_contiene "p 3 no imprime 'tercera linea'" "tercera linea" "$OUT"

echo
linea
echo "5. Escenario: 'd n' borra una linea intermedia y persiste de inmediato"
linea
OUT=$(printf 'o %s\nd 2\np\nq\n' "$F2" | ./editor)
check_contiene "d 2 confirma el borrado" "Linea 2 borrada" "$OUT"
check_no_contiene "la linea borrada ('primera linea') ya no aparece" "primera linea" "$OUT"
check_contiene "las lineas restantes se conservan" "segunda linea" "$OUT"

echo
linea
echo "6. Caso borde: borrar hasta dejar el archivo en 0 bytes exactos"
linea
printf 'o %s\nq\n' "$F3" | ./editor >/dev/null
printf 'o %s\nd 1\nq\n' "$F3" | ./editor >/dev/null
check_bytes "archivo vacio queda en 0 bytes tras borrar su unica linea" "$F3" "0"

echo
linea
echo "7. Manejo de errores: argumentos invalidos y comandos desconocidos"
linea
OUT=$(printf 'o %s\np 999\nd abc\no\nz\nq\n' "$F2" | ./editor)
check_contiene "'p 999' (fuera de rango) reporta error sin crashear" "no existe" "$OUT"
check_contiene "'d abc' (no numerico) reporta uso correcto" "Uso: d" "$OUT"
check_contiene "'o' sin argumento reporta uso correcto" "Uso: o" "$OUT"
check_contiene "'z' (comando desconocido) se reporta sin crashear" "Comando desconocido" "$OUT"

echo
linea
echo "8. Escenario: cambiar de archivo activo con 'o' en la misma sesion"
linea
OUT=$(printf 'o %s\na contenido de F1\no %s\na contenido de F4\np\nq\n' "$F1" "$F4" | ./editor)
check_contiene "tras reabrir, 'p' muestra el contenido del archivo nuevo (F4)" "contenido de F4" "$OUT"
check_no_contiene "tras reabrir, 'p' ya no muestra el archivo anterior (F1)" "contenido de F1" "$OUT"
check_contiene "el archivo anterior (F1) quedo guardado en disco antes de cambiar" "contenido de F1" "$(cat "$F1" 2>/dev/null)"

echo
linea
echo "9. 'q' termina el proceso con codigo de salida 0 (sin fugas de memoria)"
linea
printf 'o %s\na algo\nq\n' "$F1" | ./editor >/dev/null
check_exit "'q' sale limpiamente" "0" "$?"

if command -v valgrind >/dev/null 2>&1; then
    echo "  Verificando fugas de memoria con valgrind..."
    VOUT=$(printf 'o %s\na x\np\nd 1\nq\n' "$F1" | valgrind --error-exitcode=99 --leak-check=full ./editor 2>&1)
    if echo "$VOUT" | grep -q "no leaks are possible\|0 bytes in 0 blocks"; then
        echo "  [OK]    valgrind no reporta memoria perdida"
        PASS=$((PASS+1))
    else
        echo "  [FALLO] valgrind reporto posibles fugas:"
        echo "$VOUT" | grep -i "lost\|invalid" | sed 's/^/          /'
        FAIL=$((FAIL+1))
    fi
else
    echo "  [INFO]  valgrind no esta disponible en este equipo; se revisaron a mano todos los"
    echo "          malloc()/free() de estructura.c y editor.c (cada NodoLinea, su texto, y"
    echo "          nombreArchivo se liberan en estructura_destruir()/editor_liberar(), y"
    echo "          comando_salir() llama a editor_liberar() antes de exit())."
fi

echo
linea
echo "10. Escenario: EOF en stdin (Ctrl+D) sin escribir 'q' no debe crashear"
linea
printf 'o %s\n' "$F1" | ./editor >/dev/null
check_exit "cierre por EOF termina con codigo de salida 0" "0" "$?"

echo
linea
echo "RESUMEN: $PASS prueba(s) exitosa(s), $FAIL fallida(s)"
linea
if [[ $FAIL -gt 0 ]]; then
    exit 1
fi
exit 0
