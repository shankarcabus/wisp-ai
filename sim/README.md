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

### Escolhendo o personagem

```bash
WISP_MASCOT=bytelo ./sim/build/wisp-sim      # o Bytelo
./sim/build/wisp-sim                        # terminal, o padrão
WISP_MASCOT=bytelo ./sim/folha.sh sim/px     # a folha inteira, com o pixel
```

Espelha a placa, onde a escolha vem da chave `mascot` na NVS e é lida no boot
(`main.c`, `escolher_personagem()`). Nome desconhecido cai no padrão e avisa no
log, nos dois lados.

Há também o comando `char <nome>`, que troca em tempo de execução. Ele **não
existia** na primeira versão, e a razão é o que a interface ganhou depois: sem um
`destruir`, reconstruir a tela deixava os objetos compartilhados do Terminal — as
interrogações, criadas uma vez — apontando para memória liberada. Com `destruir`,
a troca é a mesma que a placa faz quando o painel publica outra escolha, e é aqui
que ela se testa.

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

## Captura e a folha de contato

```bash
./sim/folha.sh                 # 27 imagens em sim/shots/
./sim/folha.sh sim/shots-antes # ou onde você quiser
```

Cada estado, em cada contagem de sessões, mais repouso e o painel de limites.
Serve para olhar, e serve como **regressão byte a byte**: gere antes de mexer no
personagem, gere depois, compare com `cmp`. Igual significa que nada mudou.

Para isso funcionar a captura tem de ser determinística, e três decisões
existem só por causa disso:

- **Relógio virtual** (`sim/relogio.c`). O mascote respira contra
  `lv_tick_get()`. Ligado ao relógio do sistema, duas execuções caem em fases
  diferentes da respiração. Aqui o tempo é contado em passos de 16ms, não
  medido.
- **Sem indev de mouse.** O mouse real da máquina passando sobre a janela mexe
  no scroll do tileview. Medido: 25 mil pixels de diferença no painel de
  limites por causa disso.
- **`--headless`.** Mesmo sem mouse, com janela SDL a primeira execução após um
  build difere das seguintes — são os eventos que o sistema entrega ao lançar
  uma janela. Sem janela, a mesma sequência dá os mesmos bytes, sempre. É o
  modo que a `folha.sh` usa.

Com os três, 27 capturas × 2 execuções = 0 diferenças.

## Comandos

Um por linha, em stdin.

| comando | efeito |
|---|---|
| `n <1-4>` | número de sessões. **Não muda a quantidade de mascotes** — a placa desenha um só (`ui.c:1319`); o que muda é a lista sob o rótulo |
| `s <estado> [i]` | estado da sessão `i` (padrão 0) |
| `todos <estado>` | o mesmo estado em todas |
| `rest` / `wake` | entra e sai do repouso. `rest` esvazia a lista, porque o repouso exige lista vazia além do silêncio (`ui.c:1313`) |
| `tile <0\|1>` | mascote ou painel de limites |
| `bat <pct\|-1>` | bateria; -1 = desconhecida |
| `lim` / `nolim` | limites presentes ou indisponíveis |
| `shot <arquivo.bmp>` | captura. Converta com `sips -s format png x.bmp --out x.png` |
| `char <nome>` | troca de personagem, reconstruindo os objetos. Existe porque `destruir` existe — sem ele, reconstruir a tela deixava os objetos compartilhados do Terminal apontando para memória liberada |
| `heap` | bytes em uso no pool do LVGL. Serve para provar que dez trocas não vazam |
| `acao <0\|1>` | mostrar ou não o rótulo de detalhe |
| `proj <0\|1>` | mostrar ou não a lista de projetos |
| `idioma <en\|pt>` | idioma do rótulo. Em `pt` o estado ganha do detalhe, porque o detalhe vem do bridge e não é traduzível |
| `tam <small\|medium\|large>` | degrau de tamanho. O que cada um significa é do personagem |
| `sprite <pasta>` | exporta o personagem como conjunto do app do Mac |
| `quit` | encerra. É o que torna a captura em lote síncrona |
| `?` | ajuda |

O log do LVGL fica ligado em nível de aviso, de propósito: foi ele que
explicou a primeira falha de captura (`lv_draw_buf_create_ex: No memory`) em
vez de deixar adivinhar.

## Exportar o personagem para o app do Mac

```bash
WISP_MASCOT=bytelo ./sim/sprites.sh          # para ~/.wisp/mascots/bytelo/
WISP_MASCOT=bytelo ./sim/sprites.sh /tmp/x   # para olhar antes
```

Oito PNG com alfa, um por estado, no formato que o `mac/Sources/Sprites.swift`
já sabe ler — o seletor "Character" do painel passa a listar o personagem **sem
nenhuma mudança no app**. É a unificação pelo caminho mais curto: o mesmo código
que desenha na placa passa a desenhar o mascote da barra de menus.

O enquadramento é um recorte **fixo** de 340×340, igual nos oito, e não uma
caixa ajustada ao conteúdo de cada estado. É o requisito que o `MASCOTS.md`
chama de o que mais arruína esse trabalho: personagem maior num arquivo e mais à
esquerda em outro *pula* na troca de estado, e o efeito lê como bug.

Dois detalhes que a exportação resolve sozinha: o fundo preto opaco que o
`ui_create()` pinta no root, no tileview e nos dois tiles fica transparente
durante a captura e volta depois; e a bateria sai com `battery_pct = -1`, que o
`ui.c` já trata como "sem medida, rótulo vazio".

Os rótulos saem **durante** a exportação e voltam depois, junto com a bateria —
o que o sprite não pode ter é texto, e desligar o texto é uma linha. A primeira
versão recusava personagem que mostrasse rótulos, guardada por uma propriedade
que depois deixou de existir; recusar era a resposta errada de todo jeito.

O Terminal não precisa disto: a arte dele já existe em `firmware/assets/`.
