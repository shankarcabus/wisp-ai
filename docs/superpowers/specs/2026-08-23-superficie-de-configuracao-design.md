# Uma superfície de configuração no painel — desenho

**Data:** 2026-08-23
**Estado:** aprovado; aguardando plano de implementação
**Antecede:** `2026-08-23-escolha-de-personagem-em-runtime-design.md`, que abriu o
canal do painel para a placa com um único valor.
**Sucede:** som, que é projeto próprio — ver *Fora de escopo*.

## O problema

O canal existe e funciona: o painel escolhe o personagem e a placa troca sem
regravar. Mas ele carrega **um valor**, e há mais coisa que se quer ajustar:

- **Não se vê os oito estados.** O painel mostra o estado atual e nada mais, então
  escolher entre personagens é escolher no escuro. A informação já está carregada
  — o app tem os oito sprites na mão — e não é mostrada.
- **O texto embaixo do mascote é tudo ou nada.** Hoje é uma propriedade do
  personagem (`rotulos`), booleana e fixa em código: o Terminal mostra detalhe e
  lista de projetos, o Bytelo não mostra nada. Quem quer a ação sem os projetos,
  ou vice-versa, não tem como pedir.
- **O texto é em inglês.** Está registrado no `ui.c:193` por que: *"as Montserrat
  do LVGL não têm acento"*.
- **O tamanho é fixo.** O Bytelo desenha a 72% da vaga, o Terminal a 100%, ambos
  em constante de código.

## Objetivos

- O painel mostra os oito estados do personagem escolhido.
- Ajustes independentes **por superfície**, porque a mesma palavra não significa o
  mesmo nos dois lugares: "grande" numa tela de 480px e "grande" num flutuante de
  46px são coisas diferentes.
- Na placa: mostrar a ação, mostrar os projetos, o idioma do texto e o tamanho.
- No Mac: o tamanho.
- Tudo pelo canal que já existe, sem regravar a placa.

## Não-objetivos

- **Som.** É subsistema novo no firmware — o Wisp não tem áudio nenhum hoje,
  nenhum I2S, nenhuma dependência — e tem spec próprio. O hardware existe, e o
  projeto irmão desta bancada tem um motor pronto para ele.
- **Ampliar a fonte do LVGL.** Não é preciso: as oito palavras escolhidas não
  levam acento. Ver *O idioma*.
- **Janela de preferências separada.** Os controles vão numa seção do próprio
  painel. Fica anotado que é para lá que o simulador embutido iria, se for feito.
- **Escolha independente de personagem por superfície.** Continua sendo uma só,
  como o desenho anterior decidiu.

## O esquema, e a migração

`~/.wisp/mascot`, de uma linha, dá lugar a `~/.wisp/ui.json`:

```json
{
  "character": "bytelo",
  "board": { "size": "medium", "action_label": true, "project_label": true,
             "language": "en" },
  "mac":   { "size": "medium" }
}
```

**As chaves e os valores são em inglês**, como as do `config.json` que este módulo
já possui (`port`, `token`, `require_token`, `weather`, `legacy_hostnames`). O que
é em português é o texto **exibido**, não a configuração — misturar os dois é
como se cria um arquivo em que ninguém sabe qual idioma esperar.

`size` é `"small" | "medium" | "large"` — degraus, e não porcentagem, porque quem
escolhe está num seletor e não num campo numérico. `language` é `"en" | "pt"`.

**A migração não tem passo de migração.** O leitor tenta o `ui.json`; não achando,
tenta o `mascot` antigo e devolve só o personagem, com o resto no padrão. O app
escreve o `ui.json` na primeira mudança que alguém fizer. Ninguém roda nada, nada
se perde, e o arquivo antigo simplesmente deixa de ser lido quando o novo aparece.

**No `/state`, a chave `mascot` fica como está.** O firmware já gravado a lê, e
quebrar isso trocaria um problema resolvido por um em aberto.

O objeto `board` entra **inteiro** numa chave nova, `ui`, com as mesmas quatro
chaves e sem renomear nada:

```json
"ui": { "size": "medium", "action_label": true,
        "project_label": true, "language": "en" }
```

Chave ausente, ou uma das quatro ausente, significa "mantenha o que tem" — o
mesmo contrato do personagem.

## O painel

Uma seção "Mascote", com o `SectionHeader` que o painel já usa, contendo:

- **A galeria:** os oito estados em fileira, do personagem escolhido. Sai de
  graça — o `Sprites.image(_:)` já resolve cada um — e é ela que responde "não
  consigo ver todos os estados facilmente".
- O Picker de personagem, que sai de onde está hoje para dentro da seção.
- **Ação embaixo** e **Projetos embaixo**, dois toggles.
- **Idioma**, com `en` e `pt`.
- **Tamanho na placa** e **Tamanho no Mac**, dois seletores de três degraus.

O painel fica mais alto, e isso foi decidido de olhos abertos: tudo visível, sem
clique a mais.

## A placa

### Rótulos

A propriedade `rotulos` do `personagem_t`, hoje um booleano, passa a ser o
**padrão** do personagem — o Terminal sugere mostrar, o Bytelo sugere esconder — e
o ajuste sobrepõe quando existe. Assim um personagem novo não nasce sem opinião, e
quem quiser contrariá-la consegue.

Dois ajustes independentes, e não um: `acao` controla o rótulo de detalhe,
`projetos` controla a lista. Eles já são dois objetos separados no `ui.c`.

E o recentrar continua valendo: sem nenhum dos dois, o deslocamento vertical que
abria espaço para eles perde a razão de ser, e o mascote volta ao centro.

### O idioma

Uma segunda tabela, ao lado da que existe:

| estado | en (hoje) | pt |
|---|---|---|
| `idle` | idle | parado |
| `working` | thinking | pensando |
| `tool` | working | trabalhando |
| `asking` | asking you | perguntando |
| `waiting` | needs you | pedindo ajuda |
| `done` | done | pronto |
| `error` | failed | falhou |
| `offline` | offline | desligado |

**Nenhuma das oito leva acento**, e é por isso que a fonte não muda. Foram
escolhidas da folha de referência do próprio personagem, onde a única com acento
era "sem conexão" — e "desligado" diz o mesmo. O comentário do `ui.c` que explica
a restrição da fonte **fica**, porque a restrição continua lá: quem acrescentar
uma palavra com acento vai ver um quadrado vazio na tela e precisa saber por quê.

Padrão `en`, para o fork continuar de cara em inglês como a documentação dele.

### O tamanho

Três degraus, e o que cada um significa é **do personagem**, não do layout:

- **Bytelo** desenha a cara numa fração da vaga — hoje 72% fixos (`CARA_PCT`).
  Os degraus viram 60%, 72% e 88%. A margem que sobra é onde os adornos moram, e
  ela encolhe junto: no degrau grande os adornos ficam mais apertados, e é uma
  consequência aceita.
- **Terminal** é o caso que morde. O objeto da foto recebe `v.d` como tamanho, e
  o `ui.c` registra que **imagem maior que o objeto sai CORTADA, não reduzida** —
  a arte tem exatamente 306px. Então diminuir o Terminal encolhendo o objeto o
  recorta. Redimensionar de verdade exige `lv_image_set_scale()`, que é
  transformação por software.

  Isso é aceitável **e só é aceitável** porque a escala é aplicada na mudança de
  layout, não por quadro. A mesma conta que travou a placa uma vez — 93.636
  pixels a ~0,76 µs — é irrelevante uma vez a cada troca de ajuste. A guarda que
  garante "uma vez" é a mesma dos adornos: só reaplica quando o valor muda.

`vaga_de()` continua intocada. É geometria afinada à mão, e o ajuste de tamanho é
do personagem dentro da vaga.

## O Mac

O flutuante tem 46px fixos e o mascote do painel tem o seu próprio lado, ambos
passados como `side:` ao `Mascot`/`SpriteMascot`. Os degraus são **fatores**
aplicados aos dois: `small` = 0,8, `medium` = 1,0, `large` = 1,25. Assim o degrau
médio é exatamente o que existe hoje, e quem nunca mexer não vê diferença.

É mudança contida no app: não passa pelo bridge nem pela placa. E o flutuante
reposiciona a janela ao mudar de tamanho, pelo caminho de `resize()` que o
`Floating.swift` já tem.

## Verificação

- **O esquema e a migração**, em `bridge/test_ui.py`, sem framework como o resto:
  `ui.json` completo, `ui.json` parcial (chaves faltando caem no padrão),
  `ui.json` ausente com o `mascot` antigo presente, nada presente, e JSON
  inválido — que não pode derrubar o bridge.
- **A placa**, no simulador: comandos para cada ajuste, e a folha de contato
  ganhando as variantes que importam — os quatro combinados de rótulo, os três
  tamanhos, os dois idiomas. A folha continua tendo de reproduzir byte a byte.
- **O Terminal em tamanho pequeno é o teste que decide** a parte de tamanho: se
  ele sair cortado em vez de reduzido, o `lv_image_set_scale()` não está no
  caminho.
- **Na placa**, com o app: mexer em cada controle e ver a tela obedecer, sem
  regravar.
- **O painel**, a olho: a galeria com os oito, e o popover ainda usável na tela.

## Riscos

- **O painel fica alto demais.** Foi decidido assim, mas é o risco mais provável
  de virar arrependimento depois. A saída, se acontecer, é a seção recolhível que
  ficou de fora — e não uma janela nova.
- **O Terminal em `lv_image_set_scale()`.** A guarda contra reaplicar por quadro é
  o que separa isto de travar a placa. Se o FPS cair ao mexer no tamanho, é ela.
- **Um ajuste de placa que ninguém vê.** O `ui` chega no payload e a placa aplica;
  se o firmware for anterior a isto, o campo é ignorado em silêncio e o painel
  parece quebrado. É o mesmo contrato do personagem, e a mitigação é a mesma:
  ausência significa "mantenha".

## Fora de escopo, anotado

- **Som**, em qualquer superfície. Projeto seguinte.
- O simulador embutido no Wisp.app, que continua pendente e cuja utilidade caiu
  com a galeria de estados: parte do que ele resolveria, a galeria resolve por um
  centésimo do custo.
- Ampliar a fonte do LVGL para acentos.
- Ajuste de cor ou de paleta do personagem.
