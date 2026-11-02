#!/usr/bin/env bash
# Pruebas de corrección: comprimir -> descomprimir -> comparar (cmp + md5sum)
# y determinismo: el .huf debe ser idéntico con 1 hilo y con 8 hilos.
set -u
BIN="${1:-./huffcli}"
DIR="$(mktemp -d)"
trap 'rm -rf "$DIR"' EXIT
fails=0

md5() { if command -v md5sum >/dev/null; then md5sum "$1" | cut -d' ' -f1; else md5 -q "$1"; fi; }

# --- Archivos de prueba (casos borde incluidos) ---
: > "$DIR/vacio.bin"
printf 'a' > "$DIR/un_byte.txt"
head -c 3000000 /dev/zero | tr '\0' 'a' > "$DIR/un_simbolo.txt"
head -c 8000000 /dev/urandom > "$DIR/aleatorio.bin"
for i in $(seq 1 60000); do echo "Linea $i: El veloz murcielago hindu comia feliz cardillo y kiwi."; done > "$DIR/texto.txt"
head -c 1048576 /dev/urandom > "$DIR/exacto_1MiB.bin"      # múltiplo exacto del bloque

check() {   # check <archivo> <hilos> <KB por bloque>
    local f="$1" t="$2" s="$3" name; name="$(basename "$f") t=$t s=${s}KB"
    if ! "$BIN" -c "$f" "$f.huf" -t "$t" -s "$s" 2>/dev/null; then echo "FALLO compresión: $name"; fails=$((fails+1)); return; fi
    if ! "$BIN" -d "$f.huf" "$f.out" -t "$t" 2>/dev/null;     then echo "FALLO descompresión: $name"; fails=$((fails+1)); return; fi
    if cmp -s "$f" "$f.out" && [ "$(md5 "$f")" = "$(md5 "$f.out")" ]; then
        printf "OK   %-32s %10d -> %10d bytes  md5=%s\n" "$name" "$(wc -c < "$f")" "$(wc -c < "$f.huf")" "$(md5 "$f")"
    else
        echo "FALLO integridad: $name"; fails=$((fails+1))
    fi
}

for f in "$DIR"/*.bin "$DIR"/*.txt; do
    check "$f" 1 1024
    check "$f" 8 1024
    check "$f" 8 4          # bloques muy pequeños => mucha contención
done

# Determinismo: misma salida sin importar el número de hilos
"$BIN" -c "$DIR/texto.txt" "$DIR/t1.huf" -t 1 -s 64 2>/dev/null
"$BIN" -c "$DIR/texto.txt" "$DIR/t8.huf" -t 8 -s 64 2>/dev/null
if cmp -s "$DIR/t1.huf" "$DIR/t8.huf"; then echo "OK   determinismo (1 vs 8 hilos)"; else echo "FALLO determinismo"; fails=$((fails+1)); fi

# Archivo corrupto: debe fallar limpiamente (no crash)
head -c 5000 "$DIR/texto.txt.huf" > "$DIR/corrupto.huf"
if "$BIN" -d "$DIR/corrupto.huf" "$DIR/corrupto.out" 2>/dev/null; then echo "FALLO: aceptó archivo corrupto"; fails=$((fails+1));
else echo "OK   archivo corrupto rechazado"; fi
[ -e "$DIR/corrupto.out.tmp" ] && { echo "FALLO: quedó .tmp"; fails=$((fails+1)); }

echo "-----"
if [ "$fails" -eq 0 ]; then echo "TODAS LAS PRUEBAS PASARON"; else echo "$fails PRUEBAS FALLARON"; exit 1; fi
