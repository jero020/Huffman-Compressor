#!/bin/bash
# ============================================================================
# Pruebas de integracion: sys_shell -> editor -> compresion en segundo plano
#
#   1. Los comandos del shell del Parcial 1 siguen funcionando.
#   2. 'editor' abre el editor nuevo y este comprime con 'c' (con guardado
#      diferido) y al salir con 'q' se vuelve al prompt del shell.
#   3. Ctrl+C (SIGINT a todo el grupo de procesos, como hace la terminal)
#      cancela la compresion SIN cerrar el shell.
#
# La entrada se envia linea por linea con pausas porque el shell y el editor
# leen de la misma entrada estandar, igual que en una terminal real.
# ============================================================================
set -u
trap '' PIPE   # si el shell muere, escribirle no debe matar a este script
cd "$(dirname "$0")"

PASS=0
FAIL=0
W=$(mktemp -d)
trap 'rm -rf "$W"' EXIT

ok()    { echo "  [OK]    $1"; PASS=$((PASS+1)); }
fallo() { echo "  [FALLO] $1"; FAIL=$((FAIL+1)); }
enviar() { echo "$1" >&3 2>/dev/null || true; }
contiene() { if [[ "$3" == *"$2"* ]]; then ok "$1"; else fallo "$1 (se esperaba \"$2\")"; fi; }

[[ -x ./sys_shell && -x ../editor/editor ]] || { echo "Compila primero con: make (en la raiz)"; exit 1; }

DOC="$W/documento.txt"
yes "Sistemas Operativos EAFIT - compresion Huffman concurrente." 2>/dev/null | head -n 900000 | cat -n > "$DOC"

echo "1. Comandos del shell del Parcial 1"
OUT=$( { echo "help"; sleep 0.2; echo "hora"; sleep 0.2; echo "d_create $W/a.txt \"hola SO\""; sleep 0.2;
         echo "d_read $W/a.txt"; sleep 0.2; echo "exit"; } | ./sys_shell 2>&1 )
contiene "help lista las categorias, incluida edicion" "edicion" "$OUT"
contiene "hora responde" "[syscall] time" "$OUT"
contiene "d_create y d_read funcionan" "hola SO" "$OUT"

echo "2. editor -> compresion -> volver al shell"
OUT=$( { echo "editor $DOC"; sleep 1.5; printf 'c\na LINEA DESDE EL SHELL\n'; sleep 6;
         echo "q"; sleep 0.5; echo "hora"; sleep 0.2; echo "exit"; } | ./sys_shell 2>&1 )
contiene "el shell lanza el editor con fork + execvp" "Abriendo editor EAFIT" "$OUT"
contiene "el editor comprime en segundo plano" "[compresion] TERMINADA" "$OUT"
contiene "el guardado diferido funciona dentro del shell" "[guardado pendiente] cambios escritos" "$OUT"
contiene "al salir del editor se vuelve al shell" "Editor cerrado. Código de salida: 0" "$OUT"
if [[ "${OUT##*Editor cerrado}" == *"[syscall] time"* ]]; then ok "el shell sigue atendiendo comandos despues del editor"
else fallo "el shell no atendio comandos despues del editor"; fi
[[ "$(tail -n 1 "$DOC")" == "LINEA DESDE EL SHELL" ]] && ok "el cambio hecho en el editor quedo en disco" || fallo "el cambio no quedo en disco"

echo "3. Ctrl+C durante una compresion lanzada desde el shell"
rm -f "$DOC.huf"
mkfifo "$W/entrada"
setsid ./sys_shell < "$W/entrada" > "$W/sig.out" 2>&1 &
exec 3> "$W/entrada"
sleep 0.3
SHELL_PID=$(pgrep -n -f "^./sys_shell$" || true)
PGID=$(ps -o pgid= -p "$SHELL_PID" | tr -d ' ')
enviar "editor $DOC"; sleep 1.5
enviar "c"
for _ in $(seq 1 60); do grep -q "segundo plano" "$W/sig.out" && break; sleep 0.05; done
kill -INT -- "-$PGID"                # como Ctrl+C: la senal llega al shell y al editor
sleep 1
enviar "q"; sleep 0.5
enviar "hora"; sleep 0.3
if kill -0 "$SHELL_PID" 2>/dev/null; then ok "el shell sobrevive a Ctrl+C"; else fallo "Ctrl+C cerro el shell"; fi
enviar "exit"; exec 3>&- 2>/dev/null
wait 2>/dev/null; pkill -f "editor/editor $DOC" 2>/dev/null
OUT=$(cat "$W/sig.out")
contiene "Ctrl+C cancela la compresion en el editor" "[senal SIGINT] cancelando" "$OUT"
contiene "la tarea queda cancelada" "CANCELADA" "$OUT"
if [[ "${OUT##*Editor cerrado}" == *"[syscall] time"* ]]; then ok "el shell atiende comandos despues de Ctrl+C"
else fallo "el shell no atendio comandos despues de Ctrl+C"; fi
[[ ! -e "$DOC.huf.tmp" ]] && ok "no queda .huf.tmp" || fallo "quedo .huf.tmp"

echo "4. comprimir / descomprimir desde el shell"
OUT=$( { echo "comprimir $DOC"; sleep 4; echo "descomprimir $DOC.huf $W/vuelta.txt -t 4"; sleep 4;
         echo "comprimir"; sleep 0.2; echo "help compresion"; sleep 0.2; echo "exit"; } | ./sys_shell 2>&1 )
contiene "comprimir lanza huffcli con fork + execvp" "Comprimiendo '$DOC' -> '$DOC.huf'" "$OUT"
contiene "la compresion termina bien" "Listo." "$OUT"
if cmp -s "$DOC" "$W/vuelta.txt"; then ok "descomprimir devuelve un archivo identico (cmp)"; else fallo "el archivo descomprimido no es identico"; fi
contiene "comprimir sin argumentos muestra el uso" "Uso: comprimir" "$OUT"
contiene "help compresion lista los comandos" "descomprimir" "$OUT"

echo "5. Ctrl+C durante 'comprimir' desde el shell"
GRANDE="$W/grande.txt"
yes "Sistemas Operativos EAFIT compresion Huffman concurrente" 2>/dev/null | head -c 200000000 > "$GRANDE"
mkfifo "$W/entrada2"
setsid ./sys_shell < "$W/entrada2" > "$W/sig2.out" 2>&1 &
exec 4> "$W/entrada2"
sleep 0.3
SHELL_PID=$(pgrep -n -f "^./sys_shell$" || true)
PGID=$(ps -o pgid= -p "$SHELL_PID" | tr -d ' ')
echo "comprimir $GRANDE -t 1" >&4 2>/dev/null
sleep 0.8                              # a mitad de la compresion
kill -INT -- "-$PGID"
sleep 1
echo "hora" >&4 2>/dev/null; sleep 0.3
if kill -0 "$SHELL_PID" 2>/dev/null; then ok "el shell sobrevive a Ctrl+C durante comprimir"; else fallo "Ctrl+C cerro el shell"; fi
echo "exit" >&4 2>/dev/null; exec 4>&- 2>/dev/null
wait 2>/dev/null
OUT=$(cat "$W/sig2.out")
contiene "huffcli se cancela" "Cancelado con Ctrl+C" "$OUT"
if [[ "${OUT##*Cancelado con Ctrl+C}" == *"[syscall] time"* ]]; then ok "el shell atiende comandos despues de cancelar"
else fallo "el shell no atendio comandos despues de cancelar"; fi
[[ ! -e "$GRANDE.huf" && ! -e "$GRANDE.huf.tmp" ]] && ok "no quedan archivos a medias" || fallo "quedaron archivos a medias"

echo "-----"
echo "RESUMEN: $PASS prueba(s) exitosa(s), $FAIL fallida(s)"
[[ $FAIL -eq 0 ]]
