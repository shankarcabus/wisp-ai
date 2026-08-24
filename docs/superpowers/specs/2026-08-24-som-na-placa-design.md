# Som na placa

Desenho de 24/08/2026. O Wisp toca som quando o estado muda, mas só no Mac.
Este documento descreve como a placa passa a tocar também.

## O que mudou, e por que o assunto reabriu

O som no Mac foi escrito com uma justificativa de hardware registrada em três
lugares — `mac/Sources/Som.swift`, `bridge/config.py` e a mensagem do commit
`0077fea` — todos afirmando que a placa C6 não tem áudio utilizável: sem pinos
I2S mapeados e com o amplificador atrás de um expansor que ninguém dirige.

**A afirmação é verdadeira, mas de outra placa.** O expansor que gateia o
amplificador é da Waveshare AMOLED **1.8** C6; a 2.16 C6 desta bancada não tem
expansor nenhum — uma varredura do barramento responde `0x18 0x34 0x40 0x51
0x5A 0x6B`, sem nada na faixa `0x20–0x27` em que um TCA9554 responderia. O
`0x18` dessa lista é o próprio ES8311. A página do produto anuncia "Audio
Playback" e "Dual MICs Audio Capture".

Em 23/08/2026 isso foi verificado em hardware, pelo caminho mais curto: o
Clawdmeter já tinha um motor de chime ES8311 provado na placa S3, e bastou dar a
ele os pinos do C6 para o som sair pelo alto-falante desta placa. O que ficou
medido:

- ES8311 responde em `0x18`; I2S em **MCLK 19, BCLK 20, DIN 21, WS 22, DOUT 23**
  (os números da BSP XiaoZhi da Waveshare para esta placa, agora confirmados).
- **Não existe GPIO de habilitação do amplificador.** O NS4150B é alimentado
  pelo rail ALDO2 do AXP2101. A S3 usa o pino 46 e a 1.8 usa uma linha do
  expansor; esta placa não usa nenhum dos dois.
- O clip toca de dentro do flash sem travar o LVGL, em task própria.

Registrado do lado do Clawdmeter em `feat/c6-216-chime`.

## Decisões

**Mac e placa têm listas de estados independentes.** Não é "a placa substitui o
Mac" nem "os dois tocam sempre": cada lado tem seu próprio conjunto de estados
que soam, configurado no painel. O caso que isso serve é o Mac calado e a placa
avisando, sem que a escolha de um lado imponha o outro.

**A placa nasce muda.** O padrão de `board.sound.enabled` é `false`, ao
contrário do `mac.sound`, que nasce ligado em `asking`/`waiting`. O motivo é que
essas listas são independentes por escolha: se a placa nascesse ligada nos
mesmos estados, quem atualizasse o firmware passaria a ouvir o mesmo aviso duas
vezes sem ter pedido isso.

**Os sons são os do macOS, os mesmos que o Mac usa.** Ping, Glass e Basso, de
`/System/Library/Sounds`, convertidos e embutidos no firmware. O aviso soa igual
nos dois lugares, a distinção entre chamar/errar/concluir já vem calibrada por
quem desenhou os sons do sistema, e nenhum timbre é inventado aqui.

**O mapeamento estado→som é fixo:** `done` = Glass, `error` = Basso, todo o
resto = Ping. É o mapa que o Mac já usa, e a razão de não ser configurável está
escrita no commit do som do Mac: escolher oito sons num painel é trabalho para
quem usa, e a expressividade se ganha de graça aqui.

**O volume é ajustável em três níveis** (baixo/médio/alto), como Picker no
painel, ao lado do tamanho do mascote. Um alto-falante deste tamanho numa mesa
qualquer não tem um volume certo universal, e o alternativo seria recompilar
para calibrar.

## Arquitetura

### Quem decide quando tocar

**O Mac manda configuração; a placa detecta a transição.** A placa consulta
`GET /state` a cada 600 ms (`INTERVALO_MS` em `main.c`). Num canal de polling,
um comando efêmero do tipo "toca agora" é frágil nos dois sentidos: o mesmo
comando pode ser lido em duas consultas, e dois eventos dentro da mesma janela
de 600 ms viram um. Configuração é idempotente e não tem esse problema — e é o
que o canal já carrega hoje para tamanho, idioma e rótulos.

### O contrato de dados

A seção `board` do `ui.json` ganha um objeto `sound`, no mesmo formato do que já
existe em `mac`, mais o volume:

```json
"board": {
  "size": "medium", "action_label": true, "project_label": true,
  "language": "en",
  "sound": {"enabled": false, "states": ["asking", "waiting"], "volume": "medium"}
}
```

**O `server.py` não muda.** Ele já faz `snap["ui"] = _ui["board"]`, mandando a
seção inteira sem renomear nada, e o contrato de chave ausente ("placa, mantenha
o que você tem") já está escrito lá. A única mudança do lado Python é o
`UI_DEFAULTS` em `config.py`, para que `config.ui()` continue devolvendo todas as
chaves preenchidas.

Os nomes de estado são os que o painel do Mac já usa: `idle`, `working`, `tool`,
`asking`, `waiting`, `done`, `error`, `offline`.

### O gatilho no firmware

O estado que interessa é o **dominante**, que o `ui.c` já elege por `URGENCIA[]`
(asking > waiting > error > tool > working > done) para decidir que cara o
mascote faz. O som se pendura nessa mesma escolha: quando o dominante muda para
um estado que está na lista, toca uma vez.

Duas regras que a placa exige e o Mac não:

1. **Toca ao entrar, nunca durante.** Um `waiting` de dez minutos toca uma vez.
   Mesma regra do `Som.aoEntrar` no Mac.
2. **A primeira leitura depois do boot não toca.** Ela só arma o comparador.
   Ligar a placa e ouvir o alarme de um `asking` que já estava lá antes dela
   acordar é alarme falso — a placa não presenciou transição nenhuma.

O `offline` do firmware é interno (`WISP_OFFLINE`, "ainda não conectou") e não
vem do JSON; na placa ele significa "perdi o bridge", que é justamente um aviso
que vale poder ligar. Vale a mesma regra de tocar só na entrada.

### O áudio

Componente novo `firmware/main/som.{c,h}`, com a superfície mínima: inicializar,
tocar um som de um estado, e ajustar volume.

- **Driver: `espressif/esp_codec_dev`** no `idf_component.yml`, o mesmo
  componente que o exemplo `i2s_es8311` do ESP-IDF usa. Traz o volume por API
  (`esp_codec_dev_set_out_vol`) em vez de escrita de registrador na mão.
- I2S standard, TX, **mono** — o ES8311 é um codec mono, e mono corta metade do
  PCM embutido.
- Os três níveis entram como `baixo = 40`, `médio = 65`, `alto = 85` na escala
  0–100 do `esp_codec_dev`. São pontos de partida: o 65 é o valor com que o
  chime foi ouvido nesta placa, e os outros dois se ajustam ouvindo.
- O codec entra no **barramento I2C que já existe** (o mesmo do PMIC, touch,
  IMU e RTC), reusando o handle do `i2c_master`, sem abrir um segundo bus.
- Playback em task própria, prioridade baixa: a escrita no I2S é bloqueante e o
  laço do LVGL não pode esperar por ela.

### O rail do amplificador

**Este é o ponto que o spike não cobriu e a única parte do porte que não está
provada.** No Clawdmeter, o `board_init.cpp` liga ALDO1–ALDO4 via XPowersLib
antes de qualquer coisa, e foi por isso que o amplificador já estava energizado
lá. O BSP caseiro do Wisp (`components/bsp_c6_amoled_216/bsp_c6.c`) liga **só o
ALDO3**, o do display.

Então ligar o ALDO2 é código novo no Wisp. O `pmic.c` já expõe
`pmic_write_reg()`/`pmic_read_reg()`, e o `bsp_c6.c` já documenta que `0x90` é o
controle liga/desliga dos LDOs (bit 2 = ALDO3) e `0x94` a tensão do ALDO3. Por
posição, ALDO2 seria o bit 1 de `0x90` com tensão em `0x93` — **inferência, não
medição**. A implementação começa por confirmar isso na placa, comparando com o
que a XPowersLib escreve, antes de qualquer código de áudio.

### O painel

A seção "On the board" ganha, na ordem: o toggle "Sound when the state changes",
as caixas de estado quando ligado, e o Picker de volume. As caixas reaproveitam o
componente `CaixasDeSom`, que já é parametrizado por binding e não precisa
mudar. `Ajustes` ganha `boardSoundEnabled`, `boardSoundStates` e `boardVolume`.

Sai o texto de ajuda do toggle do Mac, que hoje explica que a placa não tem
áudio utilizável.

### As três correções de rota

O mesmo diagnóstico errado está escrito em três lugares e todos passam a mentir
quando isto funcionar:

- `mac/Sources/Som.swift` — o cabeçalho "SÓ NO MAC, E POR UM MOTIVO DE HARDWARE".
- `bridge/config.py` — o comentário sobre o som ser da seção `mac`.
- `mac/Sources/Configuracoes.swift` — o `.help()` do toggle de som.

Nos três, o texto novo diz o que é verdade: cada lado tem sua lista, e o que
antes se descreveu como ausência de áudio era o hardware da placa de 1.8".

## Verificação

- **Config (bridge):** `bridge/test_ui.py` já existe e testa o merge de
  `config.ui()`. A seção nova entra por teste antes do código — incluindo o caso
  do `ui.json` antigo, sem `board.sound`, que precisa continuar válido e sair do
  merge com o padrão mudo.
- **Painel (Mac):** `mac/Shots` renderiza o painel para PNG, então a interface
  nova se confere sem pedir nada a ninguém.
- **Firmware:** ouvindo. Os três sons, em transições provocadas de propósito, com
  o volume nos três níveis. Mais duas medições: `esp_get_free_heap_size` com o
  áudio inicializado — o log de boot atual reporta 82 KB de RAM interna livre e
  a placa não tem PSRAM, então os buffers DMA do I2S saem de uma folga estreita
  e o número tem que ser medido, não estimado — e a confirmação de que o LVGL
  não engasga durante o playback.

## Fora de escopo

- **Microfones.** O ES7210 e os dois MEMS existem e o rail deles é o ALDO1, mas
  captura de áudio não tem uso neste projeto hoje.
- **Volume por estado**, e som configurável por estado. O mapa é fixo, por
  decisão registrada acima.
- **Sons próprios ou de terceiros** além dos do sistema. Trocar os clips é
  regenerar um `.c`, e a porta fica aberta sem custo de desenho.
- **A placa cobrir o Mac** (tocar só quando o Mac não pode avisar, por estar
  mudo ou dormindo). Foi considerado e descartado: exigiria o Mac medir e
  publicar esse estado, e as listas independentes já resolvem o caso de uso.
