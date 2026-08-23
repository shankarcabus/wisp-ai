# Uma escolha de personagem, que chega na placa em runtime — desenho

**Data:** 2026-08-23
**Estado:** aprovado; aguardando plano de implementação
**Antecede:** `2026-08-23-mascote-configuravel-e-simulador-design.md`, que
deixou este item explicitamente fora de escopo.

## O problema

Existem **duas escolhas independentes** de personagem, e nenhuma conversa com a
outra:

- No Mac, o Picker "Character" do painel (`mac/Sources/Panel.swift:312`) manda no
  mascote da barra de menus e no flutuante. Persiste em UserDefaults, pela chave
  `mascot`, no `setter` de `Sprites.chosen` (`mac/Sources/Sprites.swift`).
- Na placa, a chave `mascot` da NVS manda, lida uma vez no boot por
  `escolher_personagem()` (`firmware/main/main.c`).

E a segunda só muda **reprovisionando**: o `bridge/provision_wifi.py` escreve a
partição NVS inteira, então trocar de personagem custa digitar a senha do WiFi de
novo. Foi a fricção que o desenho anterior anotou como preço de deixar isto de
fora.

Não há canal. Medido: o app só *lê* `~/.wisp/mascots/` e nunca escreve em
`~/.wisp`; o bridge não lê nada do app; e o payload `/state` não carrega nada
sobre personagem.

## Objetivos

- Uma escolha só, feita no painel, que vale nas duas superfícies.
- A placa troca de personagem **sem regravar**.
- A escolha sobrevive a um reboot da placa com o bridge fora do ar.

## Não-objetivos

- **Escolha independente por superfície.** O ponto é unificar; quem quiser
  divergir tem a chave da NVS.
- **Marcar no seletor quais conjuntos a placa conhece.** O painel lista conjuntos
  de arte do Mac (`~/.wisp/mascots/`), e a placa conhece personagens compilados.
  Escolher um que só existe no Mac faz a placa cair no Terminal com aviso no log
  — comportamento que `mascote_por_nome()` já tem. Marcar isso no seletor seria
  bonito e é escopo a mais.
- **Nenhum endpoint novo no bridge.**

## O canal

```
  Picker do painel
        │  (setter de Sprites.chosen)
        ├──────────────► UserDefaults["mascot"]      ← a loja do app, como hoje
        └──────────────► ~/.wisp/mascot              ← a escolha PUBLICADA
                              │
                              │ (bridge/config.py)
                              ▼
                    payload /state, chave literal "mascot"
                              │
                              ▼
                    firmware/main/main.c ──► ui_personagem()
                              │
                              └──► NVS `mascot`, só quando muda
```

**Um arquivo, e não UserDefaults lido pelo bridge.** Ler `defaults read` do
Python passaria pelo `cfprefsd`, e leitura defasada ali é o tipo de bug que custa
uma tarde e não se anuncia. Um arquivo se lê com `cat`, que é meio caminho de
qualquer depuração — e é como este projeto já guarda estado nos dois outros
lugares que importam, o `config.json` e a NVS.

**O formato do arquivo:** o nome do personagem seguido de um `\n`, nada mais.
O leitor tira espaço em branco das duas pontas e aceita a ausência da quebra —
um arquivo que alguém editou à mão não deve virar um nome inválido.

**Um arquivo próprio, e não uma chave no `config.json`.** O `config.py` faz
merge de defaults, gera token e escreve com 0600; reimplementar isso em Swift
para publicar um escalar seria muito código para pouca coisa, e escrever o
`config.json` de dois lados convida a corrida. O `mascot` é uma linha, gravada
por temp+rename, e o `config.py` continua sendo quem **lê** — a pasta é dele.

**Ausente significa ausente.** Sem o arquivo, o bridge omite o campo; sem o
campo, a placa mantém o que tem. Instalação nova, placa gravada antes desta
mudança e bridge velho caem todos nesse caminho, e nenhum deles fica sem
mascote.

## A troca em runtime

`ui.h` ganha uma função, e `mascote.h` ganha um ponteiro:

```c
/* Troca o personagem ativo, reconstruindo os objetos da tela.
 * Pega o mutex do LVGL sozinha — chamável da task de rede.
 * Nome desconhecido ou igual ao atual não faz nada. */
void ui_personagem(const char *nome);
```

```c
/* Desfaz o que criar() fez. Chamada com o mutex do LVGL JÁ na mão. */
void (*destruir)(mascote_t *m);
```

`main.c` chama `ui_personagem()` **a cada payload recebido**, com o valor do
campo `mascot`, e não só quando percebe mudança: quem decide se há o que fazer é
a própria função. Custa uma comparação de string por poll, e concentra num lugar
a pergunta "mudou?" — dois lugares decidindo isso é como se cria discordância.

O corpo: pega o mutex, resolve o nome por `mascote_por_nome()`, sai se for o
ativo, chama `destruir` em cada mascote, troca o ativo, chama `criar` de novo, e
força a reaplicação do layout pelo idioma que o `ui.c` já usa para isso —
`g_qtd = -1`, que faz o próximo `ui_update()` chamar `aplicar_layout()`. Os rótulos **não** são recriados: eles pertencem ao layout
e sobrevivem à troca; o que muda é aparecerem ou não, que `aplicar_layout()` já
decide pela propriedade `rotulos` do personagem.

### O que cada `destruir` tem de acertar

Aqui é onde isto pode dar errado em silêncio, e os dois casos são conhecidos:

- **Terminal.** As interrogações do estado `asking` são três objetos
  compartilhados, criados **uma vez só** e guardados por um `bool` estático
  (`g_interrog_prontas`). Sem apagá-las e zerar o guarda, a próxima entrada no
  Terminal usa ponteiros para memória liberada. **Esta é a razão pela qual o
  simulador não tinha comando de troca:** eu recusei um na implementação anterior
  justamente porque `destruir` não existia.
- **Bytelo.** O adorno é **irmão** da cara, não filho, porque filho que sai da
  caixa do pai é recortado. Irmão não morre por herança, então o `destruir` tem
  de apagá-lo explicitamente. É o mesmo preço que a chama do Terminal paga, e a
  mesma armadilha que o `dispor` dele já documenta.

### O mutex não é opcional

O timer de animação roda dentro da task do LVGL e chama
`mascote_ativo()->animar` com o `interno` de cada mascote. Apagar objeto fora do
mutex corre com o render — e este firmware já registra o que isso produz: o
watchdog pegou a task de rede presa dentro de `lv_inv_area()`, numa lista de
invalidação corrompida, e na tela o sintoma foi a placa conectar, reportar uma
vez e emudecer. Parece problema de rede e não é.

As duas implementações de `animar` já toleram `interno` nulo (`if (!p) return`),
o que cobre a janela entre destruir e criar.

## Persistência

A placa grava o nome na NVS **só quando difere do que está gravado**. Escrever a
cada payload desgastaria flash por nada — a escolha muda quando uma pessoa
resolve mudar, não a cada segundo.

Com isso, a pergunta do personagem no provisionamento passa a ser o **valor
inicial**, e não a única forma de escolher. Uma placa que reinicia sem o bridge
no ar sobe no último personagem escolhido, e não no padrão.

## Verificação

**No simulador**, o comando `char <nome>` volta a ser possível — e ele só é
seguro *porque* o `destruir` passou a existir. Então ele é o teste honesto:

- alternar Terminal ↔ Bytelo repetidamente, dez vezes, e a tela continuar certa;
- depois de cada troca, rodar as 64 transições de estado — é ali que o estado
  vazado entre casos aparece, nunca numa captura isolada;
- entrar em `asking` no Terminal **depois** de uma volta pelo Bytelo, que é o
  caminho exato do ponteiro pendurado das interrogações.

**Na placa**, o teste de aceitação é um só e não admite interpretação: mexer no
Picker do painel e o mascote da placa trocar, **sem regravar**. Mais o reboot com
o bridge desligado, para provar a persistência.

## Riscos

- **O ponteiro pendurado das interrogações** é o risco central, e ele falha de
  um jeito que não parece com a causa: crash ou lixo na tela ao entrar em
  `asking`, muitas trocas depois. O teste acima existe para isso.
- **Vazamento por troca.** Cada `criar` aloca um bloco `interno`; cada
  `destruir` tem de liberá-lo. Dez trocas no simulador com o heap medido antes e
  depois é o que separa "funciona" de "funciona por enquanto".
- **O nome que só existe no Mac.** Escolher um conjunto de arte próprio no
  painel deixa as duas superfícies discordando. É comportamento aceito e
  documentado, não bug — mas é a pergunta que alguém vai fazer.

## Fora de escopo, anotado

- O simulador embutido no Wisp.app, que é o desenho seguinte.
- Levar o Bytelo para o mascote vetorial do Mac (`Mascot.swift`); hoje ele chega
  no app como conjunto de sprites, pelo caminho que o desenho anterior abriu.
- Qualquer outra configuração no painel além do personagem.
