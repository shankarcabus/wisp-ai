#!/bin/bash
#
# Folha de contato da tela da placa: cada estado, em cada contagem de sessões,
# mais repouso e o painel de limites.
#
#   ./sim/folha.sh [pasta]
#
# É a rede de proteção da extração do personagem: gere antes de refatorar, gere
# depois, compare byte a byte. Igual significa que o Terminal não mudou.
#
# Roda em --headless, e isso não é detalhe: com janela SDL a primeira execução
# após um build difere das seguintes. Sem janela, a mesma sequência de comandos
# dá os mesmos bytes, sempre. Medido.
#
# Sobre a contagem de sessões: a placa desenha UM mascote sempre (ui.c:1319).
# O que muda entre 1, 2 e 4 é a lista de sessões sob o rótulo. Vale capturar
# porque é layout de texto, e é ali que estoura.
set -euo pipefail

RAIZ="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
OUT="${1:-$RAIZ/sim/shots}"
# O personagem vem do ambiente, como na placa vem da NVS.
export WISP_MASCOT="${WISP_MASCOT:-terminal}"
SIM="$RAIZ/sim/build/wisp-sim"

[[ -x "$SIM" ]] || { echo "compile primeiro: cmake --build sim/build -j" >&2; exit 1; }

mkdir -p "$OUT"
rm -f "$OUT"/*.bmp
cd "$RAIZ"          # o shim de assets usa caminho relativo à raiz

ESTADOS="idle working tool asking waiting done error offline"

{
    for n in 1 2 4; do
        echo "wake"; echo "n $n"
        for e in $ESTADOS; do
            echo "todos $e"
            echo "shot $OUT/n${n}-${e}.bmp"
        done
    done
    echo "rest";              echo "shot $OUT/repouso.bmp"
    echo "wake"; echo "tile 1"; echo "shot $OUT/limites.bmp"
    echo "nolim";             echo "shot $OUT/limites-indisponivel.bmp"
    echo "lim"; echo "tile 0"

    # As variantes dos ajustes, num estado só cada.
    #
    # O produto cartesiano seriam 4 combinados de rótulo x 3 tamanhos x 2 idiomas
    # x 8 estados = 192 imagens por personagem, que ninguém abre. O que se compara
    # aqui é o AJUSTE, e um estado basta para vê-lo.
    echo "todos tool"
    for t in small medium large; do
        echo "tam $t";          echo "shot $OUT/tam-$t.bmp"
    done
    echo "tam medium"
    for par in "1 1" "1 0" "0 1" "0 0"; do
        set -- $par
        echo "acao $1"; echo "proj $2"; echo "shot $OUT/rot-$1$2.bmp"
    done
    echo "acao 1"; echo "proj 1"
    for l in en pt; do
        echo "idioma $l";       echo "shot $OUT/idioma-$l.bmp"
    done
    echo "idioma en"
    echo "quit"
} > "$OUT/roteiro.txt"

# O esperado sai do PRÓPRIO roteiro, contando os `shot`. Era uma constante à mão
# ao lado do roteiro que a produz: acrescentar uma captura exigia editar dois
# lugares, e esquecer o segundo dava "esperava 36 capturas" em vez do problema.
ESPERADO=$(grep -c '^shot ' "$OUT/roteiro.txt")

"$SIM" --headless < "$OUT/roteiro.txt" > "$OUT/folha.log" 2>&1 || true

if grep -q "^E (sim)" "$OUT/folha.log"; then
    echo "ERROS na captura:" >&2
    grep "^E (sim)" "$OUT/folha.log" >&2
    exit 1
fi

QTD=$(ls -1 "$OUT"/*.bmp 2>/dev/null | wc -l | tr -d ' ')
if grep -q "sem assets convertidos" "$OUT/folha.log"; then
    echo "AVISO: sem assets convertidos — o Terminal saiu no vetorial." >&2
    echo "       Comparar esta folha com uma feita COM assets acusa 33 diferenças" >&2
    echo "       que são só modo de arte. Rode \`idf.py build\` em firmware/." >&2
fi

echo "$QTD capturas de '$WISP_MASCOT' em $OUT"
[[ "$QTD" -eq "$ESPERADO" ]] || {
    echo "esperava $ESPERADO capturas, saíram $QTD — veja $OUT/folha.log" >&2; exit 1; }
