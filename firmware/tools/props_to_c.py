#!/usr/bin/env python3
"""
Converte os mapas ASCII de firmware/props/ em lv_image_dsc_t de RGB565A8.

    python3 firmware/tools/props_to_c.py

Escreve firmware/main/mascote_pixel_props.c. NÃO EDITAR O .c À MÃO — rode isto.

POR QUE EM TEMPO DE BUILD
-------------------------
O ui.c deste projeto documenta, com medição, que decodificar imagem em tempo de
execução nesta placa custa o firmware inteiro: FPS de 62 para 1-7 e RAM interna
em 12 bytes de mínimo histórico. A regra do repositório é que a conversão
acontece antes. Isto obedece.

O FORMATO RGB565A8
------------------
O plano de cor inteiro (w*h uint16, little-endian), seguido do plano de alfa
(w*h uint8). O stride é w*2 — é somando stride*h ao mesmo ponteiro que o LVGL
encontra o alfa, o que o carregador de assets do Terminal já documenta.
"""
from pathlib import Path

RAIZ = Path(__file__).resolve().parents[2]
PROPS = RAIZ / "firmware" / "props"
SAIDA = RAIZ / "firmware" / "main" / "mascote_pixel_props.c"

ORDEM = ["bolha", "laptop", "pergunta", "maos", "faiscas", "wifi"]


def paleta() -> dict:
    p = {}
    for linha in (PROPS / "paleta.txt").read_text().splitlines():
        linha = linha.strip()
        if not linha or linha.startswith("#"):
            continue
        ch, cor = linha.split()
        p[ch] = None if cor == "transparente" else int(cor, 16)
    return p


def rgb565(rgb: int) -> int:
    r, g, b = (rgb >> 16) & 0xFF, (rgb >> 8) & 0xFF, rgb & 0xFF
    return ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3)


def converter(nome: str, pal: dict):
    linhas = [l for l in (PROPS / f"{nome}.txt").read_text().splitlines()
              if l.strip("\n") != "" or True]
    linhas = [l for l in linhas if l != ""]
    h = len(linhas)
    w = max(len(l) for l in linhas)
    cor, alfa, desconhecidos = [], [], set()
    for y in range(h):
        linha = linhas[y].ljust(w, ".")
        for x in range(w):
            ch = linha[x]
            if ch not in pal:
                desconhecidos.add(ch)
            v = pal.get(ch)
            cor.append(0 if v is None else rgb565(v))
            alfa.append(0 if v is None else 255)
    if desconhecidos:
        raise SystemExit(
            f"{nome}.txt: caractere sem cor na paleta: {sorted(desconhecidos)}")
    return w, h, cor, alfa


def main() -> None:
    pal = paleta()
    partes = [
        # O caminho com glob NÃO entra aqui: "props/" seguido de asterisco
        # abre um comentário dentro do comentário e o compilador avisa.
        "/* GERADO por firmware/tools/props_to_c.py — não editar à mão.\n"
        " * Fonte: os .txt em firmware/props/. Para mudar um adorno, edite o\n"
        " * mapa ASCII e rode o gerador de novo. */\n"
        '#include "mascote_pixel_props.h"\n'
    ]
    tabela, total = [], 0
    for nome in ORDEM:
        w, h, cor, alfa = converter(nome, pal)
        octetos = []
        for c in cor:
            octetos += [c & 0xFF, (c >> 8) & 0xFF]      # little-endian
        octetos += alfa
        total += len(octetos)
        corpo = ",".join(str(b) for b in octetos)
        partes.append(
            f"\n/* {nome}: {w}x{h} unidades de arte */\n"
            f"static const uint8_t {nome}_dados[] = {{{corpo}}};\n"
            f"static const lv_image_dsc_t {nome}_dsc = {{\n"
            f"    .header = {{ .magic  = LV_IMAGE_HEADER_MAGIC,\n"
            f"                 .cf     = LV_COLOR_FORMAT_RGB565A8,\n"
            f"                 .w      = {w}, .h = {h}, .stride = {w * 2} }},\n"
            f"    .data      = {nome}_dados,\n"
            f"    .data_size = sizeof({nome}_dados),\n"
            f"}};\n"
        )
        tabela.append(f"    [PROP_{nome.upper()}] = &{nome}_dsc,")

    partes.append(
        "\nstatic const lv_image_dsc_t *const TABELA[PROP_QTD] = {\n"
        "    [PROP_NENHUM] = NULL,\n" + "\n".join(tabela) + "\n};\n"
        "\nconst lv_image_dsc_t *prop_dsc(prop_t p)\n{\n"
        "    if (p <= PROP_NENHUM || p >= PROP_QTD) return NULL;\n"
        "    return TABELA[p];\n}\n"
    )
    SAIDA.write_text("".join(partes))
    print(f"{SAIDA.relative_to(RAIZ)}: {len(ORDEM)} adornos, {total} bytes de arte")


if __name__ == "__main__":
    main()
