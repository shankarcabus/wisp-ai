# Mascote configurável e simulador de tela — desenho

**Data:** 2026-08-23
**Estado:** aprovado; aguardando plano de implementação

## O problema

Duas coisas que hoje não existem.

1. **Um segundo personagem na placa.** O Wisp tem *um* personagem — o computador
   retrô — em duas renderizações: os oito PNGs assados na partição `storage` e o
   desenho vetorial de `ui.c`, que é o fallback quando o `mmap` falha. Não há
   como ter outro, nem como escolher.
2. **Uma forma de ver a tela da placa sem regravar.** `mac/preview.sh` e
   `mac/shots.sh` cobrem só o app do Mac. A interface de 480×480 só existe na
   placa.

O segundo é pré-requisito prático do primeiro: iterar um personagem regravando o
firmware a cada tentativa não é um ciclo de trabalho.

## Objetivos

- Um segundo personagem na placa, em pixel art, animado, escolhido por dado.
- O personagem atual intacto — as duas renderizações continuam disponíveis.
- Um simulador no Mac que roda o `ui.c` real e permite trocar estado, número de
  sessões, repouso, tela e personagem.

## Não-objetivos

- **Nenhuma superfície de configuração para usuário final.** O simulador é
  ferramenta de desenvolvimento; a escolha do personagem na placa é uma chave de
  NVS, não um painel.
- O personagem novo **não** vai para o app do Mac nesta rodada. `Mascot.swift`
  continua com o computador.
- Nenhum canal de configuração em runtime pela bridge. Fica anotado como caminho
  futuro; não é preciso para isto.

## As restrições medidas

Três fatos decidem o desenho, e dois deles vieram de fora deste repositório.

### Imagem escalada não serve como caminho de render no C6

O Clawdmeter, na placa irmã, já tentou exatamente o que parecia óbvio: buffer
minúsculo de 20×20 mais escala de imagem do LVGL. A medição está registrada em
`Clawdmeter/firmware/src/splash.cpp`, no comentário "Two render paths":
**~0,76 µs por pixel de saída**, ou
seja **100–220 ms por quadro** no C6 de núcleo único, porque o LVGL transforma o
quadro inteiro por software a cada redesenho. Pior, invalidação parcial de uma
imagem transformada não recorta a transformação e **borra**.
`lv_image_set_antialias(obj, false)` (`lv_image.h:205`) resolve a qualidade da
ampliação e nada do custo.

Isso explica um comportamento que o `CLAUDE.md` deste repositório já documenta
como "não é bug": o mascote de imagem **não anima** e o vetorial anima. É a mesma
causa.

### A solução do Clawdmeter não transplanta

Lá, o caminho que funciona é fazer o upscale à mão, por replicação de célula, e
enviar só as células que mudaram direto ao painel, contornando o LVGL. Funciona
porque o splash é **tela cheia e exclusiva**.

No Wisp o mascote é **composto**: um a quatro deles dividindo a tela com rótulos,
dentro de um tileview que desliza, e com squash/stretch/tilt — que é
transformação. Escrever direto no painel numa região que o LVGL também redesenha
dá flicker e sobreposição.

E canvas por mascote está fora: 224×224 em RGB565 são 100KB, o C6 não tem PSRAM,
e este firmware já registrou **4,7KB** de RAM interna mínima
(`firmware/main/ui.c:27-31`).

### Objetos LVGL não pagam esse custo

O vetorial atual anima porque cada parte é um objeto que o LVGL move e recolore —
não existe bitmap para transformar. É a via que sobra, e é a que este desenho
adota.

## Decisão: híbrido

A cara do personagem novo é feita de **objetos LVGL**; os adornos são **sprites
minúsculos** que trocam só na mudança de estado.

**Cara.** Silhueta em degraus de três tons a partir de retângulos arredondados,
mais olhos, sobrancelha e boca em blocos. Cerca de doze objetos. Independente de
resolução: sem grid, sem escala inteira, sem degradação a 60px. Anima pelo
`animar_um()` que já existe — respiração, squash, flutuação, tilt — mais piscar,
que fica barato.

**Props.** Bolha de pensamento, laptop com mãos, `?`, mãos juntas, faíscas, wifi
cortado. Autorados em malha de **32**, que é onde a arte de referência já está: a
cara tem ~22–24 pixels de arte e a célula com adornos fecha em ~32. Cada prop
custa a sua própria caixa, não o grid inteiro. Aparecem e desaparecem na troca de
estado; nunca são transformados por quadro.

Por que 32 e não os 20 do Clawdmeter nem 40: num caminho de bitmap o grid é
orçamento e o menor tamanho manda — a 60px, quatro sessões em cena, um grid de 40
não fecha em escala inteira e o de 20 raspa a 3×. No híbrido a cara não usa grid
nenhum, então o grid virou só a malha em que os props são desenhados, e ali a
resolução é ganho quase de graça.

**Limiar.** Abaixo de ~100px os props não são mostrados. A 60px um laptop de
vinte pixels de arte não comunica nada, e a cara sozinha comunica tudo. Os 100px
são ponto de partida, para ser acertado no simulador olhando as quatro
disposições de sessão — não é constante fechada.

**Mapeamento.** Os oito estados da referência caem um a um nos do Wisp:
parado→`idle`, pensando→`working`, trabalhando→`tool`, perguntando→`asking`,
pedindo ajuda→`waiting`, feliz→`done`, triste→`error`, desligado→`offline`.

## Arquitetura

### Registro de personagens

Hoje `mascote_t` (`ui.c:194-214`) carrega campos do computador direto — `tela`,
`braco[2]`, `chama` —, `criar_mascote()` (`:1004`) monta essas partes e
`animar_um()` (`:547`) as anima contra `ALVO[]` (`:179`) e `COR[]` (`:136`).

Passa a existir `mascote.h` com uma interface:

- `criar(pai, mascote_t *)` — monta as partes e as guarda num bloco opaco do
  personagem;
- `aplicar(mascote_t *, wisp_state_t, uint32_t agora)` — leva as partes ao alvo
  do estado, chamada pelo timer que já existe;
- `snap(int tamanho)` — devolve o tamanho ajustado. Serve aos props: quando eles
  estão em cena, o tamanho do mascote é arredondado para um múltiplo da malha 32,
  para os sprites caírem em escala inteira. Abaixo do limiar, e no Terminal, o
  tamanho volta inalterado;
- metadados: nome, se usa props, limiar mínimo.

Duas implementações, em arquivos próprios: `mascote_terminal.c` (o atual, com os
dois caminhos — imagem e vetorial) e `mascote_pixel.c` (o novo). `ui.c` fica com
layout, painel, repouso e tileview, e encolhe — o que já era desejável em 1654
linhas.

A escolha entra como **dado**, nunca como `#if`. A regra do `CLAUDE.md` —
diferenças de placa em `board.h`, diferenças de estilo não existem — continua
intacta, porque personagem não é propriedade do chip.

### Seleção

Chave `mascot` no namespace `wisp` da NVS, lida no boot em `main.c` junto de
`ssid`/`pass`/`token`/`host` (`:656-665`). Ausente ou desconhecida cai no
Terminal — vale a mesma regra do resto do projeto, um personagem coerente ganha
de um estado inválido na tela.

A chave é escrita pelo caminho que já grava as outras: o gerador de NVS invocado
por `flash.sh`, com uma opção nova. Ou seja, **trocar de personagem exige
regravar** — o que incomoda menos do que parece, porque a iteração de desenho
acontece no simulador. Tirar essa limitação é o item de configuração em runtime
que está fora de escopo.

### O simulador

`sim/`, alvo CMake próprio, LVGL 9.5.0 mais SDL2, janela de 480×480 rodando **o
`ui.c` real**. O LVGL vendorizado já traz `src/drivers/sdl/` completo — janela,
mouse, teclado e os dois renderizadores. Na máquina falta só `brew install sdl2`.

O shim são seis símbolos:

| símbolo no firmware | no host |
|---|---|
| `ESP_LOGI` / `ESP_LOGW` / `esp_log.h` | `printf` |
| `bsp_display_lock` / `bsp_display_unlock` | mutex |
| `mmap_assets_*` e `esp_lv_decoder_*` em `carregar_fotos()` (`ui.c:62-126`) | leitura dos PNGs de `firmware/assets/` do disco, com o decoder PNG do LVGL habilitado no `lv_conf.h` do host |

Alimentação por `wisp_data_t` sintético. Controles por teclado com um overlay de
texto: estado, número de sessões, repouso, personagem, tile. Nenhuma interface a
manter. `ui_update()` e `ui_swipe()` não mudam de contrato — é o mesmo cabeçalho
que a placa usa.

Efeito colateral desejado: `ui.c` passa a ser exercitável no host, o que hoje não
é.

## Ordem de execução

1. **Simulador** com o `ui.c` como está. Ganho imediato e independente: a tela
   sem regravar.
2. **Extração do registro de personagens.** Refatoração pura — o Terminal tem de
   sair igual, e o simulador serve de conferência lado a lado.
3. **O personagem pixel**, iterado no simulador.
4. **Medição na placa.**
5. **Seleção por NVS.**

Os passos 1 e 2 são reversíveis e não mudam nada visível. O 3 é onde está o
trabalho de desenho. O 4 é a única coisa que pode invalidar o desenho, e por isso
vem antes do 5.

## Verificação

O simulador **não** prova custo de render: o Mac tem CPU e RAM de sobra e nenhuma
das restrições acima existe lá. Isso não é um detalhe do plano, é o risco central.

O que precisa ser medido **na placa**, com o personagem novo e quatro sessões em
cena:

- FPS pelo contador que já existe em `ui.c` (`g_refrescos`);
- RAM interna mínima, contra os 4,7KB registrados;
- comportamento no swipe entre os tiles, onde props e transformações concorrem.

A comparação é contra o Terminal vetorial na mesma cena, não contra um número
absoluto.

Vale repetir o que o `CLAUDE.md` cobra: compilar para um alvo não é o mesmo que
rodar nele. Ao fim disto, o caminho do C6 fica verificado em hardware e o do S3
por compilação — não há placa S3 nesta bancada.

## Riscos

- **Doze objetos é estimativa, não medida.** Se a silhueta em degraus exigir
  muito mais formas, o custo por mascote sobe e com quatro em cena pode não
  fechar. O passo 4 é onde isso aparece; se aparecer, a saída é reduzir os
  degraus, não voltar para bitmap.
- **A extração pode mudar o Terminal sem querer.** Mitigação: o simulador vem
  antes da extração, para haver com o que comparar.
- **A arte dos props precisa ser autorada.** A folha de referência é estilo, não
  asset. O Clawdmeter puxa a arte de `claudepix.vercel.app` e traz consigo um
  aviso explícito de zona cinzenta de licenciamento, porque o mascote Clawd é
  copyrighted da Anthropic. O mecanismo de lá serve de modelo; a arte não.

## Fora de escopo, anotado

- Levar o personagem pixel para o app do Mac (`Mascot.swift`, `Sprites.swift`).
- Configuração em runtime pela bridge — um campo no payload `/state` tiraria a
  necessidade de regravar para trocar de personagem.
- Animação por quadro nos props, que este desenho torna possível mas não usa.
- Sequências de mais de um quadro por estado no caminho de imagem, mencionadas em
  `MASCOTS.md`.
