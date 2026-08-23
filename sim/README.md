# Simulador da tela da placa

Roda a interface do Wisp — o `firmware/main/ui.c` de verdade — numa janela de
480×480 no Mac, para trabalhar layout, expressão e composição sem regravar a
placa a cada tentativa.

## O que ele NÃO é

**Ele não prova custo de render.** O Mac tem CPU e RAM de sobra, e nenhuma das
restrições que decidem o desenho da placa existe aqui: não há a falta de PSRAM
do C6, não há o teto de RAM interna, não há núcleo único. Número de FPS e de
heap mínimo se medem na placa, com `idf.py monitor`, e só lá.

O que ele prova é o que se vê: onde as coisas ficam, o que cada estado
comunica, se o texto cabe, se o swipe leva onde deve.

## Como rodar

```bash
brew install sdl2 cmake
cmake -S sim -B sim/build && cmake --build sim/build -j
./sim/build/wisp-sim
```

**Rode da raiz do repositório.** O simulador procura os assets convertidos em
`firmware/build/mmap_build/assets/storage`, por caminho relativo.

Sem um `idf.py build` prévio essa pasta não existe, e aí o simulador avisa e cai
no mascote vetorial. Isso não é defeito: é exatamente o caminho de degradação da
placa quando a partição de assets não monta, e é útil poder vê-lo.

## Por que `sim/shim/` existe

O `ui.c` inclui quatro headers de ESP-IDF: `esp_log.h`, `bsp/esp-bsp.h`,
`esp_mmap_assets.h` e `esp_lv_decoder.h`. Em `sim/shim/` há um de cada, com o
mesmo nome, e `shim/` vem primeiro na ordem de include. O resultado é que o
`ui.c` do firmware compila aqui **sem uma linha de modificação** — não há
`#ifdef` de simulador dentro do firmware, e não pode haver.

A consequência prática: **se alguém acrescentar um include de ESP-IDF ao
`ui.c`, o simulador para de compilar.** A correção é escrever o shim que falta,
nunca mexer no `ui.c`.

O shim de assets tem uma sutileza documentada no próprio arquivo: na placa,
`mmap_assets_get_size()` conta dois bytes de magic que `get_mem()` já pulou, e
o `ui.c` compensa com `- 2`. Aqui não há empacotador, então o shim devolve
`tamanho + 2` para que a subtração chegue no mesmo número.

## O LVGL é o mesmo da placa

Quando `firmware/managed_components/lvgl__lvgl/` existe — ou seja, depois de um
build do firmware — o simulador compila **aquela** cópia, e a configuração
imprime `LVGL: cópia local do firmware (paridade exata)`. Num clone limpo essa
pasta não existe (é gitignored, 250MB) e o CMake busca a v9.5.0 do upstream:
mesma versão, origem diferente.
