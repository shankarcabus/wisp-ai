#!/bin/bash
#
# Exporta o personagem desenhado no simulador como conjunto de sprites do app
# do Mac: oito PNG com alfa em ~/.wisp/mascots/<personagem>/.
#
#   WISP_MASCOT=bytelo ./sim/sprites.sh          # para o lugar de verdade
#   WISP_MASCOT=bytelo ./sim/sprites.sh /tmp/x   # para olhar antes
#
# É a unificação: o mesmo código que desenha na placa passa a desenhar o
# mascote da barra de menus, porque o app já sabe ler conjuntos dessa pasta
# (mac/Sources/Sprites.swift). Um personagem, duas superfícies.
#
# O app IGNORA um conjunto incompleto e volta para o vetorial — regra do
# MASCOTS.md, e boa: um personagem coerente ganha de sete quadros bonitos e um
# buraco. Por isso este script falha se não saírem oito.
set -euo pipefail

RAIZ="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
SIM="$RAIZ/sim/build/wisp-sim"
NOME="${WISP_MASCOT:-terminal}"
DESTINO="${1:-$HOME/.wisp/mascots/$NOME}"

[[ -x "$SIM" ]] || { echo "compile primeiro: cmake --build sim/build -j" >&2; exit 1; }
command -v sips >/dev/null || { echo "sips ausente — isto é para macOS" >&2; exit 1; }

TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT

cd "$RAIZ"      # o shim de assets usa caminho relativo à raiz
printf 'sprite %s\nquit\n' "$TMP" | WISP_MASCOT="$NOME" "$SIM" --headless \
    | grep -E '^(I|W|E) \(sim\)' || true

QTD=$(ls -1 "$TMP"/*.tiff 2>/dev/null | wc -l | tr -d ' ')
if [[ "$QTD" -ne 8 ]]; then
    echo "esperava 8 sprites, saíram $QTD — nada foi escrito em $DESTINO" >&2
    exit 1
fi

mkdir -p "$DESTINO"
for f in "$TMP"/*.tiff; do
    sips -s format png "$f" --out "$DESTINO/$(basename "${f%.tiff}").png" >/dev/null
done

echo "8 sprites de '$NOME' em $DESTINO"
ls -1 "$DESTINO"
