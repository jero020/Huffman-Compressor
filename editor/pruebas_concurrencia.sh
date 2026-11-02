#!/bin/bash
# ============================================================================
# Pruebas del Parcial 2 - Compresion Huffman concurrente integrada al editor
#
# Verifica, contra el binario ./editor:
#   1. c: comprime en segundo plano y el .huf se descomprime IDENTICO (md5).
#   2. Condicion de carrera: editar ('a') DURANTE la compresion no corrompe
#      nada: el .huf corresponde a la version original y el cambio se guarda
#      en disco cuando la tarea termina (guardado diferido).
#   3. x: descompresion en segundo plano desde el editor.
#   4. k: cancelacion limpia (no queda .huf ni .huf.tmp).
#   5. SIGINT durante la compresion cancela la tarea sin matar el editor.
#   6. Protecciones: no se abre otro archivo ni se lanza otra tarea mientras
#      hay una en curso; no se descomprime sobre el documento abierto.
# ============================================================================
set -u
cd "$(dirname "$0")"

PASS=0
FAIL=0
W=$(mktemp -d)
trap 'rm -rf "$W"' EXIT

linea() { printf '%s\n' "----------------------------------------------------------------"; }
ok()    { echo "  [OK]    $1"; PASS=$((PASS+1)); }
fallo() { echo "  [FALLO] $1"; FAIL=$((FAIL+1)); }
contiene() { if [[ "$3" == *"$2"* ]]; then ok "$1"; else fallo "$1 (se esperaba \"$2\")"; fi; }
md5() { if command -v md5sum >/dev/null; then md5sum "$1" | cut -d' ' -f1; else md5 -q "$1"; fi; }

[[ -x ./editor ]] || make >/dev/null || { echo "No compila"; exit 1; }
[[ -x ../huffcli ]] || make -C .. huffcli >/dev/null 2>&1

# Documento grande (~90 MB, 450k lineas) para que la compresion dure lo
# suficiente como para interactuar con el editor mientras corre.
DOC="$W/documento.txt"
yes "Sistemas Operativos EAFIT - compresion Huffman concurrente con hilos, mutex y variables de condicion." \
    | head -n 450000 | cat -n > "$DOC"
cp "$DOC" "$W/original.txt"
echo "Documento de prueba: $(wc -c < "$DOC") bytes, $(wc -l < "$DOC") lineas"

echo; linea
echo "1-2. Compresion en segundo plano + edicion concurrente (condicion de carrera)"
linea
# 'c' y 'a' llegan juntos: la 'a' se ejecuta mientras la compresion tiene el
# archivo tomado como LECTORA. Al cerrar stdin (EOF) el editor espera a que
# la tarea termine y luego ejecuta el guardado pendiente.
OUT=$(printf 'c\na LINEA AGREGADA DURANTE LA COMPRESION\ns\n' | ./editor "$DOC")
contiene "la compresion arranca en segundo plano" "en segundo plano" "$OUT"
contiene "la edicion durante la compresion se difiere (no bloquea la UI)" "PENDIENTE" "$OUT"
contiene "se muestra el progreso en tiempo real" "[compresion]" "$OUT"
contiene "la compresion termina" "[compresion] TERMINADA" "$OUT"
contiene "el guardado pendiente se ejecuta al terminar" "[guardado pendiente] cambios escritos" "$OUT"

if [[ "$(tail -n 1 "$DOC")" == "LINEA AGREGADA DURANTE LA COMPRESION" ]]; then
    ok "el cambio del usuario quedo en disco (no se perdio)"
else
    fallo "el cambio del usuario no quedo en disco"
fi

if [[ -x ../huffcli ]]; then
    ../huffcli -d "$DOC.huf" "$W/check.txt" 2>/dev/null
    if [[ "$(md5 "$W/check.txt")" == "$(md5 "$W/original.txt")" ]]; then
        ok "el .huf corresponde EXACTAMENTE a la version al iniciar (md5 $(md5 "$W/original.txt"))"
    else
        fallo "el .huf no corresponde a la version original (inconsistencia por carrera)"
    fi
fi

echo; linea
echo "3. Descompresion en segundo plano desde el editor ('x')"
linea
OUT=$(printf 'x %s %s\n' "$DOC.huf" "$W/restaurado.txt" | ./editor "$W/otro.txt")
contiene "la descompresion termina" "[descompresion] TERMINADA" "$OUT"
if cmp -s "$W/restaurado.txt" "$W/original.txt"; then
    ok "restaurado == original (cmp, md5 $(md5 "$W/restaurado.txt"))"
else
    fallo "restaurado distinto al original"
fi

echo; linea
echo "4. Cancelacion con 'k'"
linea
rm -f "$DOC.huf"
OUT=$(printf 'c\nk\n' | ./editor "$DOC")
contiene "la tarea se reporta como cancelada" "CANCELADA" "$OUT"
if [[ ! -e "$DOC.huf" && ! -e "$DOC.huf.tmp" ]]; then
    ok "no quedan archivos a medias (.huf ni .huf.tmp)"
else
    fallo "quedaron archivos a medias tras cancelar"
fi

echo; linea
echo "5. Ctrl+C (SIGINT) durante la compresion"
linea
rm -f "$DOC.huf"
{ printf 'c\n'; sleep 4; printf 'p 1\n'; } | ./editor "$DOC" > "$W/sig.out" 2>&1 &
PID=$!
for _ in $(seq 1 100); do grep -q "segundo plano" "$W/sig.out" 2>/dev/null && break; sleep 0.05; done
kill -INT "$PID" 2>/dev/null
wait "$PID"; RC=$?
OUT=$(cat "$W/sig.out")
contiene "SIGINT cancela la tarea" "[senal SIGINT] cancelando" "$OUT"
contiene "la tarea queda cancelada" "CANCELADA" "$OUT"
contiene "el editor sigue vivo y atiende comandos despues de la senal" "1	Sistemas Operativos" "$OUT"
[[ $RC -eq 0 ]] && ok "el editor termina normalmente (exit 0)" || fallo "exit $RC"
[[ ! -e "$DOC.huf.tmp" ]] && ok "no queda .huf.tmp" || fallo "quedo .huf.tmp"

echo; linea
echo "6. Protecciones mientras hay una tarea en curso"
linea
OUT=$(printf 'c\no %s\nc\nk\n' "$W/otro.txt" | ./editor "$DOC")
contiene "no se puede abrir otro archivo con una tarea en curso" "antes de abrir otro archivo" "$OUT"
contiene "no se lanzan dos tareas a la vez" "Ya hay una compresion" "$OUT"
OUT=$(printf 'x %s %s\nq\n' "$DOC.huf" "$DOC" | ./editor "$DOC")
contiene "no se descomprime sobre el documento abierto" "no se puede descomprimir sobre el documento abierto" "$OUT"
OUT=$(printf 'c %s\nq\n' "$DOC" | ./editor "$DOC")
contiene "no se comprime sobre el mismo documento" "no puede ser el mismo documento" "$OUT"
OUT=$(printf 'x %s\nq\n' "$W/no_existe.huf" | ./editor "$W/otro.txt")
contiene "un .huf inexistente se reporta como error sin crashear" "[descompresion] ERROR" "$OUT"

echo; linea
echo "RESUMEN: $PASS prueba(s) exitosa(s), $FAIL fallida(s)"
linea
[[ $FAIL -eq 0 ]]
