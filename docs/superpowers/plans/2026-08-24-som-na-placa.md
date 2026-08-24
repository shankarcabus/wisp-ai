# Som na placa — plano de implementação

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** A placa toca um som quando o estado dominante do Claude muda, com lista de estados e volume próprios, configurados no painel do Mac.

**Architecture:** O Mac publica configuração no `~/.wisp/ui.json`; o bridge repassa a seção `board` inteira no `/state`; a placa detecta a transição sozinha (o canal é polling de 600 ms, onde comando efêmero se perde ou se repete) e toca pelo ES8311 via `esp_codec_dev`. A chamada de som mora no `main.c`, nunca no `ui.c`, porque o simulador compila o `ui.c` e não tem áudio para linkar.

**Tech Stack:** ESP-IDF 5.5 (C), `espressif/esp_codec_dev`, cJSON, SwiftUI/AppKit (painel), Python 3 (bridge e gerador de clips).

**Spec:** `docs/superpowers/specs/2026-08-24-som-na-placa-design.md`

## Global Constraints

- **Placa:** Waveshare ESP32-C6-Touch-AMOLED-2.16. Single-core RISC-V, **sem PSRAM**, 82 KB de RAM interna livres com o firmware atual.
- **I2S:** MCLK 19, BCLK 20, DIN 21, WS 22, DOUT 23. Codec **ES8311 no I2C 0x18**, no barramento compartilhado (SDA 8 / SCL 7) — reusar o handle existente, nunca abrir um segundo bus.
- **Rail do amplificador:** ALDO2 do AXP2101 — liga no **bit 1 do `0x90`**, tensão no **`0x93`** como `(mV-500)/100` nos 5 bits baixos, preservando os 3 de cima. **3300 mV → `0x1C`**.
- **Não existe GPIO de habilitação do amplificador** nesta placa. Não procurar por um.
- **Padrão da placa é mudo:** `board.sound.enabled = false`. O `mac.sound` continua nascendo ligado — as listas são independentes de propósito.
- **Mapa estado→som fixo:** `done` = Glass, `error` = Basso, todo o resto = Ping.
- **Volume:** três degraus, `baixo = 40`, `médio = 65`, `alto = 85` na escala 0–100 do `esp_codec_dev`.
- **Os testes do bridge não têm framework.** Rodam com `python3 bridge/test_ui.py` e usam o helper `checa(nome, obtido, esperado)`, que conta falhas. Não introduzir pytest.
- **Comentários e documentação em português**, como o resto do firmware e do bridge. Chaves de JSON e identificadores em inglês.
- **O `ui.c` é compilado pelo simulador** (`sim/CMakeLists.txt:48`). Nada que ele passe a chamar pode existir só no firmware.
- Build do firmware: `export PATH=/usr/bin:/bin`, `. ~/esp/esp-idf/export.sh`, `idf.py build` em `firmware/`. Gravar só o app: `idf.py -p /dev/cu.usbmodem101 app-flash`.
- Build do simulador: `cmake -S sim -B sim/build && cmake --build sim/build -j`.

---

### Task 1: O rail do amplificador (ALDO2)

O BSP do Wisp liga só o ALDO3, do display. Sem o ALDO2 o NS4150B fica sem alimentação e todo o resto do trabalho toca no silêncio.

**Files:**
- Modify: `firmware/components/bsp_c6_amoled_216/bsp_c6.c`
- Modify: `firmware/components/bsp_c6_amoled_216/include/bsp_c6.h`

**Interfaces:**
- Consumes: nada.
- Produces: `esp_err_t bsp_c6_amp_power(bool on);` — liga/desliga o rail ALDO2 (3,3 V). Idempotente. Chamável depois do `bsp_c6_pmic_init()`.

- [ ] **Step 1: Ler o que já existe**

Abra `bsp_c6.c` e localize o bloco do ALDO3 (`AXP_LDO_ONOFF_CTRL0`, `AXP_ALDO3_VOL_CTRL`, `AXP_ALDO3_BIT`, `AXP_ALDO3_3V3`) e os helpers `axp_write()` / `axp_bit()`. O novo código é o mesmo padrão com outro bit e outro registrador — siga a nomenclatura que está lá.

- [ ] **Step 2: Escrever a função**

Ao lado das constantes do ALDO3, acrescente:

```c
/* ALDO2 alimenta o amplificador NS4150B. Não há GPIO de enable nesta placa: o
 * amp existe enquanto o rail existir. Os endereços vêm da XPowersLib (que é o
 * que faz isso funcionar no projeto vizinho): os LDOs são consecutivos, ALDO1
 * no bit 0 / 0x92, ALDO2 no bit 1 / 0x93, ALDO3 no bit 2 / 0x94. */
#define AXP_ALDO2_VOL_CTRL  0x93
#define AXP_ALDO2_BIT       1
#define AXP_ALDO2_3V3       0x1C   /* (3300 - 500) / 100 */
```

E a função pública:

```c
esp_err_t bsp_c6_amp_power(bool on)
{
    if (!on) return axp_bit(AXP_LDO_ONOFF_CTRL0, AXP_ALDO2_BIT, false);

    /* Os 3 bits altos do 0x93 não são deste LDO — preservar, como a XPowersLib
     * faz. Escrever o byte inteiro aqui mexeria em coisa alheia. */
    uint8_t v = 0;
    ESP_RETURN_ON_ERROR(axp_read(AXP_ALDO2_VOL_CTRL, &v), TAG, "ALDO2 vol rd");
    v = (uint8_t)((v & 0xE0) | AXP_ALDO2_3V3);
    ESP_RETURN_ON_ERROR(axp_write(AXP_ALDO2_VOL_CTRL, v), TAG, "ALDO2 vol wr");
    ESP_RETURN_ON_ERROR(axp_bit(AXP_LDO_ONOFF_CTRL0, AXP_ALDO2_BIT, true), TAG, "ALDO2 on");
    ESP_LOGI(TAG, "ALDO2 (rail do amplificador) ligado");
    return ESP_OK;
}
```

Se não houver um `axp_read()` no arquivo, escreva-o espelhando o `axp_write()` existente (mesma transação I2C, sentido inverso). Declare a função no `bsp_c6.h` com um comentário de uma linha dizendo que ela alimenta o amplificador e que não existe GPIO de enable.

- [ ] **Step 3: Chamar e verificar por leitura de volta**

No `main.c`, logo depois da inicialização do PMIC/BSP, chame `bsp_c6_amp_power(true)` e, **temporariamente**, logue a leitura de volta para provar o efeito:

```c
ESP_ERROR_CHECK(bsp_c6_amp_power(true));
uint8_t r90 = 0, r93 = 0;
pmic_read_reg(0x90, &r90);
pmic_read_reg(0x93, &r93);
ESP_LOGI(TAG, "ALDO2: 0x90=0x%02X (bit1=%d)  0x93=0x%02X", r90, (r90 >> 1) & 1, r93);
```

- [ ] **Step 4: Gravar e conferir o log**

```bash
cd firmware && idf.py build && idf.py -p /dev/cu.usbmodem101 app-flash monitor
```

Esperado: `bit1=1` e `0x93` com os cinco bits baixos em `0x1C`. Se `bit1=0`, o bit está errado — pare e reveja contra a XPowersLib antes de seguir.

- [ ] **Step 5: Remover o log temporário e commitar**

```bash
git add firmware/components/bsp_c6_amoled_216/ firmware/main/main.c
git commit -m "Alimentar o amplificador: o BSP ligava só o rail do display"
```

---

### Task 2: A configuração no bridge

**Files:**
- Modify: `bridge/config.py:53-63` (o `UI_DEFAULTS`)
- Test: `bridge/test_ui.py`

**Interfaces:**
- Consumes: nada.
- Produces: `config.ui()["board"]["sound"]` = `{"enabled": bool, "states": list[str], "volume": str}`, sempre presente.

- [ ] **Step 1: Escrever o teste que falha**

No `bridge/test_ui.py`, o dicionário `PADRAO` do `main()` descreve o retorno de `config.ui()` sem arquivo nenhum. Acrescente a chave nova ao `board` dele:

```python
        "board": {"size": "medium", "action_label": True,
                  "project_label": True, "language": "en",
                  "sound": {"enabled": False,
                            "states": ["asking", "waiting"],
                            "volume": "medium"}},
```

E, depois dos casos que já existem, um teste do `ui.json` antigo — o que hoje está no disco de quem já usa o Wisp, sem `board.sound`:

```python
    # Um ui.json escrito pela versão anterior do app não tem board.sound. O
    # merge é recursivo, então a seção precisa nascer do padrão SEM apagar o que
    # o usuário já escolheu ao lado dela.
    limpa()
    config.UI_FILE.write_text(json.dumps({
        "board": {"size": "large", "language": "pt"},
        "mac": {"sound": {"enabled": False, "states": []}},
    }))
    b = config.ui()["board"]
    checa("board antigo mantém size", b["size"], "large")
    checa("board antigo mantém language", b["language"], "pt")
    checa("board antigo ganha sound mudo", b["sound"]["enabled"], False)
    checa("board antigo ganha volume médio", b["sound"]["volume"], "medium")
    checa("mac.sound do usuário sobrevive", config.ui()["mac"]["sound"]["enabled"], False)
```

- [ ] **Step 2: Rodar e ver falhar**

Run: `python3 bridge/test_ui.py`
Esperado: FALHA nas linhas do `sound` (o padrão ainda não tem a chave).

- [ ] **Step 3: Implementar**

No `UI_DEFAULTS`, acrescente a chave ao `board` e **substitua** o comentário que explica por que o som é só do Mac — ele agora está errado:

```python
UI_DEFAULTS = {
    "character": "",
    # Cada lado tem sua própria lista de estados que soam, e é de propósito: o
    # caso que isso serve é o Mac calado com a placa avisando. A placa nasce
    # MUDA — se nascesse ligada nos mesmos estados do Mac, quem atualizasse
    # passaria a ouvir o mesmo aviso duas vezes sem ter pedido.
    "board": {"size": "medium", "action_label": True,
              "project_label": True, "language": "en",
              "sound": {"enabled": False,
                        "states": ["asking", "waiting"],
                        "volume": "medium"}},
    # O padrão do Mac liga nos dois estados que significam "o Claude está te
    # esperando".
    "mac": {"size": "medium",
            "sound": {"enabled": True, "states": ["asking", "waiting"]}},
}
```

- [ ] **Step 4: Rodar e ver passar**

Run: `python3 bridge/test_ui.py`
Esperado: todas as linhas `ok`, saída final sem falhas.

- [ ] **Step 5: Commitar**

```bash
git add bridge/config.py bridge/test_ui.py
git commit -m "Ensinar a configuração a ter som na placa, e mudo por padrão"
```

---

### Task 3: Os ajustes no Mac

**Files:**
- Modify: `mac/Sources/Ajustes.swift`

**Interfaces:**
- Consumes: nada.
- Produces: `Ajustes.boardSoundEnabled: Bool`, `Ajustes.boardSoundStates: Set<String>`, `Ajustes.boardVolume: Volume` (enum `low/medium/high` com `rotulo`). O `publicar()` passa a escrever `board.sound`.

- [ ] **Step 1: Acrescentar o enum de volume**

Ao lado do `enum Tamanho`, dentro de `Ajustes`:

```swift
    /// Três degraus em vez de um slider: um alto-falante deste tamanho não tem
    /// volume certo universal, mas também não tem cem valores distinguíveis.
    /// Os números que a placa aplica vivem no firmware, não aqui — o painel
    /// publica o degrau.
    enum Volume: String, CaseIterable {
        case low, medium, high
        var rotulo: String { rawValue }
    }
```

- [ ] **Step 2: Acrescentar as chaves e os acessores**

Junto das outras `kBoard*`:

```swift
    static let kBoardSoundOn     = "ui.board.sound.enabled"
    static let kBoardSoundStates = "ui.board.sound.states"
    static let kBoardVolume      = "ui.board.sound.volume"
```

E os acessores, seguindo os helpers que já existem (`bool`, `gravar`):

```swift
    static var boardSoundEnabled: Bool {
        get { bool(kBoardSoundOn, false) }   // a placa nasce muda; ver Ajustes do bridge
        set { gravar(newValue, kBoardSoundOn) }
    }

    /// Mesmo padrão do `soundStates` do Mac, e a mesma razão: os dois estados
    /// que significam "o Claude está te esperando". Só vale quando alguém liga
    /// o som da placa, que nasce desligado.
    static var boardSoundStates: Set<String> {
        get {
            guard let a = UserDefaults.standard.array(forKey: kBoardSoundStates) as? [String]
            else { return ["asking", "waiting"] }
            return Set(a)
        }
        set { gravar(newValue.sorted(), kBoardSoundStates) }
    }

    static var boardVolume: Volume {
        get { Volume(rawValue: UserDefaults.standard.string(forKey: kBoardVolume) ?? "") ?? .medium }
        set { gravar(newValue.rawValue, kBoardVolume) }
    }
```

- [ ] **Step 3: Publicar no JSON**

No `publicar()`, a seção `board` passa a ser:

```swift
            "board": ["size": boardSize.rawValue,
                      "action_label": boardAction,
                      "project_label": boardProject,
                      "language": boardLanguage,
                      "sound": ["enabled": boardSoundEnabled,
                                "states": boardSoundStates.sorted(),
                                "volume": boardVolume.rawValue]],
```

- [ ] **Step 4: Compilar e conferir o arquivo publicado**

```bash
cd mac && ./build.sh && cd ..
python3 -c "import json,pathlib; d=json.loads(pathlib.Path.home().joinpath('.wisp/ui.json').read_text()); print(json.dumps(d['board'], indent=2, sort_keys=True))"
```

Esperado: o `board` com `sound.enabled=false`, `states` e `volume="medium"`. Se o `ui.json` não existir ainda, abra o app e mexa em qualquer ajuste para forçar a publicação.

- [ ] **Step 5: Commitar**

```bash
git add mac/Sources/Ajustes.swift
git commit -m "Publicar os ajustes de som da placa"
```

---

### Task 4: O painel

**Files:**
- Modify: `mac/Sources/Configuracoes.swift` (a seção `naPlaca`, e o `.help()` do toggle do Mac)

**Interfaces:**
- Consumes: `Ajustes.boardSoundEnabled`, `Ajustes.boardSoundStates`, `Ajustes.boardVolume`, `CaixasDeSom`.
- Produces: nada para tarefas seguintes.

- [ ] **Step 1: Acrescentar os controles em `naPlaca`**

Ao fim do `Bloco("On the board")`, depois do `Campo("Mascot size")`:

```swift
            Toggle(isOn: liga({ Ajustes.boardSoundEnabled },
                              { Ajustes.boardSoundEnabled = $0 })) {
                Text("Sound when the state changes").font(.system(size: 11))
            }
            .toggleStyle(.checkbox)
            .help("The board has its own list, independent from the Mac's: "
                  + "the case this serves is a silent Mac with the board "
                  + "calling you.")

            if Ajustes.boardSoundEnabled {
                CaixasDeSom(estados: liga({ Ajustes.boardSoundStates },
                                          { Ajustes.boardSoundStates = $0 }))
                    .padding(.leading, 18)

                Campo("Volume") {
                    Picker("", selection: liga({ Ajustes.boardVolume },
                                               { Ajustes.boardVolume = $0 })) {
                        ForEach(Ajustes.Volume.allCases, id: \.self) {
                            Text($0.rotulo).tag($0)
                        }
                    }
                }
            }
```

- [ ] **Step 2: Corrigir o texto de ajuda do toggle do Mac**

No `noMac`, o `.help()` do toggle de som diz hoje que a placa não tem áudio utilizável. Troque por:

```swift
            .help("Independent from the board's list, right above — "
                  + "each side has its own states.")
```

- [ ] **Step 3: Ensinar a ferramenta de shots a fotografar os ajustes**

A ferramenta hoje só rende `AbaPlaca` e `AbaUso` — a aba de ajustes, que é justamente a que esta tarefa muda, não é fotografada por ninguém. Acrescente no `mac/Shots/main.swift`, ao lado das linhas que já existem:

```swift
    write(aba(AbaConfig(bridge: bridge)), "panel-settings", opaque: true)
```

- [ ] **Step 4: Renderizar e OLHAR**

```bash
cd mac && ./build.sh && cd .. && ./mac/shots.sh
```

Abra `docs/panel-settings.png` com a ferramenta de leitura de imagem e confira: os controles novos aparecem em "On the board", o Picker de volume está alinhado com os outros `Campo`, e nada foi empurrado para fora do bloco. Depois ligue o som da placa no app de verdade e renderize outra vez — as caixas de estado e o volume só existem com o toggle ligado, então a foto com ele desligado não prova nada sobre eles.

O `shots.sh` regenera todos os PNGs de `docs/`, não só o novo. É esperado: a interface mudou.

- [ ] **Step 5: Commitar**

```bash
git add mac/Sources/Configuracoes.swift mac/Shots/main.swift docs/
git commit -m "Oferecer som da placa no painel, com estados e volume"
```

---

### Task 5: O firmware entende a configuração

**Files:**
- Modify: `firmware/main/ui.h` (a `wisp_cfg_t`, o `WISP_CFG_PADRAO`, dois protótipos)
- Modify: `firmware/main/ui.c` (`ui_state_index`, `ui_volume_from_text`)
- Modify: `firmware/main/main.c` (o parser do objeto `ui`)

**Interfaces:**
- Consumes: nada.
- Produces: campos `som` (bool), `som_estados` (uint8_t, bitmask por `wisp_state_t`) e `som_volume` (uint8_t, `WISP_VOL_*`) em `wisp_cfg_t`; `int ui_state_index(const char *s)`; `uint8_t ui_volume_from_text(const char *s)`; enum `WISP_VOL_BAIXO/MEDIO/ALTO`.

- [ ] **Step 1: Estender a struct e o padrão no `ui.h`**

```c
typedef enum { WISP_VOL_BAIXO = 0, WISP_VOL_MEDIO, WISP_VOL_ALTO,
               WISP_VOL_QTD } wisp_vol_t;
```

Na `wisp_cfg_t`, depois de `tamanho`:

```c
    bool    som;          /* board.sound.enabled */
    uint8_t som_estados;  /* bitmask: bit N = tocar no estado N (wisp_state_t) */
    uint8_t som_volume;   /* WISP_VOL_* */
```

E no `WISP_CFG_PADRAO`, `.som = false, .som_estados = 0, .som_volume = WISP_VOL_MEDIO` — a placa nasce muda, como o bridge.

Protótipos, com o comentário explicando por que `ui_state_index` existe:

```c
/* Índice do estado pelo nome, ou -1 se não é um nome de estado.
 *
 * Existe porque ui_state_from_text() devolve WISP_IDLE para o que não conhece,
 * e isso é certo para o campo "st" de uma sessão e errado para uma LISTA de
 * estados: "offline" ou um nome digitado errado ligariam o `idle` sem ninguém
 * ter pedido. Quem monta bitmask usa esta; quem lê uma sessão usa a outra. */
int ui_state_index(const char *s);

/* "low"/"medium"/"high" -> degrau. Desconhecido vira médio. */
uint8_t ui_volume_from_text(const char *s);
```

- [ ] **Step 2: Implementar as duas funções no `ui.c`**

Reescreva `ui_state_from_text` em termos da nova, para o nome decodificar num lugar só:

```c
int ui_state_index(const char *s)
{
    static const char *NOMES[WISP_COUNT] = {
        [WISP_IDLE] = "idle",   [WISP_WORKING] = "working",
        [WISP_TOOL] = "tool",   [WISP_ASKING]  = "asking",
        [WISP_WAITING] = "waiting", [WISP_DONE] = "done",
        [WISP_ERROR] = "error", [WISP_OFFLINE] = "offline",
    };
    if (!s) return -1;
    for (int i = 0; i < WISP_COUNT; i++)
        if (NOMES[i] && !strcmp(s, NOMES[i])) return i;
    return -1;
}

wisp_state_t ui_state_from_text(const char *s)
{
    const int i = ui_state_index(s);
    return i < 0 ? WISP_IDLE : (wisp_state_t) i;
}

uint8_t ui_volume_from_text(const char *s)
{
    if (s && !strcmp(s, "low"))  return WISP_VOL_BAIXO;
    if (s && !strcmp(s, "high")) return WISP_VOL_ALTO;
    return WISP_VOL_MEDIO;
}
```

- [ ] **Step 3: Ler os campos no parser do `main.c`**

Dentro do `if (cJSON_IsObject(cfg))`, depois da linha do `size`:

```c
        /* Chave ausente mantém o que já vale, como as quatro de cima. */
        const cJSON *snd = cJSON_GetObjectItemCaseSensitive(cfg, "sound");
        if (cJSON_IsObject(snd)) {
            if (cJSON_IsBool(v = cJSON_GetObjectItemCaseSensitive(snd, "enabled")))
                s_cfg.som = cJSON_IsTrue(v);
            const cJSON *st = cJSON_GetObjectItemCaseSensitive(snd, "states"), *it = NULL;
            if (cJSON_IsArray(st)) {
                uint8_t m = 0;
                cJSON_ArrayForEach(it, st) {
                    const int k = cJSON_IsString(it) ? ui_state_index(it->valuestring) : -1;
                    if (k >= 0) m |= (uint8_t) (1u << k);
                }
                s_cfg.som_estados = m;
            }
            if (cJSON_IsString(v = cJSON_GetObjectItemCaseSensitive(snd, "volume")))
                s_cfg.som_volume = ui_volume_from_text(v->valuestring);
        }
```

- [ ] **Step 4: Compilar os dois alvos**

```bash
cd firmware && idf.py build && cd ..
cmake -S sim -B sim/build && cmake --build sim/build -j
```

Esperado: os dois compilam. O simulador é o que garante que a struct nova não quebrou o `ui.c` compartilhado.

- [ ] **Step 5: Commitar**

```bash
git add firmware/main/ui.h firmware/main/ui.c firmware/main/main.c
git commit -m "Ler os ajustes de som do payload, sem confundir nome errado com idle"
```

---

### Task 6: Quem é a sessão dominante

**Files:**
- Modify: `firmware/main/ui.c:886-895` (o laço de `URGENCIA` dentro do `ui_update`)
- Modify: `firmware/main/ui.h`

**Interfaces:**
- Consumes: nada.
- Produces: `int ui_sessao_dominante(const wisp_data_t *d);` — índice da sessão mais urgente, ou `-1` se não há sessão. O `ui_update` passa a usá-la no lugar do laço inline.

- [ ] **Step 1: Extrair a função**

No `ui.c`, antes do `ui_update`, com o `URGENCIA[]` movido para dentro dela:

```c
/* Quem PAROU esperando você fala primeiro. Trabalhando pode aguardar; travado,
 * não. Devolve ÍNDICE e não estado porque o ui_update precisa do índice para
 * marcar com "> " qual sessão o mascote representa — e o som, que compara
 * estados, tira o estado do índice.
 *
 * Fica pública para o main.c decidir o som a partir da MESMA escolha que decide
 * a cara do mascote. A chamada de som não pode morar aqui: o simulador compila
 * este arquivo e não tem áudio para linkar. */
int ui_sessao_dominante(const wisp_data_t *d)
{
    static const wisp_state_t URGENCIA[] = {
        WISP_ASKING, WISP_WAITING, WISP_ERROR,
        WISP_TOOL, WISP_WORKING, WISP_DONE,
    };
    if (!d || d->session_count <= 0) return -1;
    for (size_t u = 0; u < sizeof(URGENCIA)/sizeof(URGENCIA[0]); u++)
        for (int k = 0; k < d->session_count; k++)
            if (d->sessions[k].state == URGENCIA[u]) return k;
    return 0;
}
```

- [ ] **Step 2: Usar a função no `ui_update`**

Substitua o laço que calcula `esc` por:

```c
        const int dom = ui_sessao_dominante(d);
        int esc = dom < 0 ? 0 : dom;
```

Não mexa em mais nada do `ui_update` — `esc` continua sendo usado igual, e `sem_sessao` continua respondendo pelo caso de não haver sessão.

- [ ] **Step 3: Declarar no `ui.h`**

Acrescente o protótipo com uma linha dizendo que devolve índice ou -1.

- [ ] **Step 4: Compilar e comparar o simulador**

```bash
cd firmware && idf.py build && cd ..
cmake --build sim/build -j && ./sim/build/wisp-sim
```

Esperado: compila nos dois, e o simulador desenha exatamente como antes — esta tarefa não muda comportamento nenhum, só move código. Se a cara do mascote mudar de sessão, a extração errou.

- [ ] **Step 5: Commitar**

```bash
git add firmware/main/ui.c firmware/main/ui.h
git commit -m "Expor quem é a sessão dominante, sem mudar quem ela é"
```

---

### Task 7: Os clips

**Files:**
- Create: `firmware/tools/sons_para_c.py`
- Create: `firmware/main/sons.c` (gerado)
- Create: `firmware/main/sons.h`

**Interfaces:**
- Consumes: nada.
- Produces: `const uint8_t *som_pcm(wisp_state_t s, size_t *len);` — devolve o PCM do estado (mono, 16-bit LE, 22050 Hz) e seu tamanho. Nunca nulo: estados sem som próprio recebem o Ping.

- [ ] **Step 1: Escrever o gerador**

`firmware/tools/sons_para_c.py`, no espírito do `props_to_c.py` que já existe (docstring explicando o porquê, `RAIZ` por `parents[2]`, aviso de não editar o `.c` à mão):

```python
#!/usr/bin/env python3
"""
Converte os sons do sistema do macOS em PCM embutido no firmware.

    python3 firmware/tools/sons_para_c.py

Escreve firmware/main/sons.c. NÃO EDITAR O .c À MÃO — rode isto.

POR QUE OS SONS DO SISTEMA
--------------------------
São os mesmos que o Mac toca (ver mac/Sources/Som.swift), então o aviso soa
igual nos dois lugares, e a distinção entre chamar, errar e concluir já vem
calibrada por quem os desenhou.

O FORMATO
---------
Mono, 16-bit little-endian, 22050 Hz. Mono porque o ES8311 é um codec mono;
22 kHz porque o alto-falante desta placa não distingue mais que isso e o dobro
custaria o dobro de flash. A conversão é `afconvert`, que já vem no macOS.
"""
import subprocess
import tempfile
from pathlib import Path

RAIZ = Path(__file__).resolve().parents[2]
SAIDA = RAIZ / "firmware" / "main" / "sons.c"
SONS = {"ping": "Ping", "glass": "Glass", "basso": "Basso"}
TAXA = 22050


def pcm(nome: str) -> bytes:
    origem = Path("/System/Library/Sounds") / f"{nome}.aiff"
    if not origem.exists():
        raise SystemExit(f"não achei {origem}")
    with tempfile.TemporaryDirectory() as tmp:
        destino = Path(tmp) / "s.raw"
        subprocess.run(
            ["/usr/bin/afconvert", "-f", "caff", "-d", f"LEI16@{TAXA}",
             "-c", "1", str(origem), str(destino)],
            check=True, capture_output=True)
        bruto = destino.read_bytes()
    # O CAF tem cabeçalho; o pedaço de áudio vem depois do chunk "data" (o
    # tamanho declarado pode ser -1 em stream, então tomamos o resto).
    i = bruto.index(b"data")
    return bruto[i + 12:]


def em_c(nome: str, dados: bytes) -> str:
    linhas = [f"static const uint8_t {nome}_pcm[] = {{"]
    for i in range(0, len(dados), 16):
        linhas.append("    " + ",".join(str(b) for b in dados[i:i + 16]) + ",")
    linhas.append("};")
    return "\n".join(linhas)


def main():
    partes = ["/* GERADO por firmware/tools/sons_para_c.py — não editar à mão. */",
              '#include "sons.h"', ""]
    for chave, arquivo in SONS.items():
        dados = pcm(arquivo)
        print(f"  {arquivo:6} {len(dados):>7} bytes  ({len(dados)/2/TAXA:.2f}s)")
        partes.append(em_c(chave, dados))
        partes.append("")

    partes.append("""
const uint8_t *som_pcm(wisp_state_t s, size_t *len)
{
    /* O mapa é o mesmo do Mac, e fixo pela mesma razão: escolher oito sons num
     * painel é trabalho para quem usa, e a expressividade sai de graça aqui —
     * curto e claro para quem chama, grave para quem falhou, cristalino para
     * quem terminou. */
    switch (s) {
    case WISP_DONE:  *len = sizeof(glass_pcm); return glass_pcm;
    case WISP_ERROR: *len = sizeof(basso_pcm); return basso_pcm;
    default:         *len = sizeof(ping_pcm);  return ping_pcm;
    }
}
""")
    SAIDA.write_text("\n".join(partes))
    print(f"escrito {SAIDA.relative_to(RAIZ)}")


if __name__ == "__main__":
    main()
```

- [ ] **Step 2: Escrever o header**

`firmware/main/sons.h`:

```c
#pragma once
#include <stddef.h>
#include <stdint.h>
#include "ui.h"

/* PCM do som de um estado: mono, 16-bit LE, 22050 Hz. Nunca devolve nulo —
 * estado sem som próprio recebe o Ping. Gerado por tools/sons_para_c.py. */
const uint8_t *som_pcm(wisp_state_t s, size_t *len);
```

- [ ] **Step 3: Rodar o gerador e conferir os números**

Run: `python3 firmware/tools/sons_para_c.py`
Esperado: três linhas com tamanho e duração plausíveis (Ping ~1,5 s ≈ 66 KB; Glass ~1,65 s ≈ 73 KB; Basso ~0,77 s ≈ 34 KB), somando bem menos que os 4 MB livres da partição de app. Se algum vier com 0 bytes, o corte do cabeçalho CAF errou — imprima os primeiros 64 bytes e ajuste antes de seguir.

- [ ] **Step 4: Compilar**

```bash
cd firmware && idf.py build && cd ..
```

Esperado: compila, e o `idf.py build` reporta o binário maior que antes na ordem de ~170 KB.

- [ ] **Step 5: Commitar**

```bash
git add firmware/tools/sons_para_c.py firmware/main/sons.c firmware/main/sons.h
git commit -m "Embutir os sons do sistema, convertidos por script"
```

---

### Task 8: O componente de som

**Files:**
- Create: `firmware/main/som.c`
- Create: `firmware/main/som.h`
- Modify: `firmware/main/idf_component.yml`
- Modify: `firmware/main/CMakeLists.txt` (se ele lista fontes explicitamente)

**Interfaces:**
- Consumes: `bsp_c6_amp_power()` (Task 1), `som_pcm()` (Task 7), o handle do barramento I2C existente.
- Produces: `esp_err_t som_init(i2c_master_bus_handle_t bus);`, `void som_volume(uint8_t degrau);`, `void som_tocar(wisp_state_t s);` (não bloqueante, ignora chamada durante playback).

- [ ] **Step 1: Declarar a dependência**

No `firmware/main/idf_component.yml`, junto das outras, com o comentário no estilo do arquivo:

```yaml
  # Driver do ES8311 (o codec de áudio desta placa, I2C 0x18) mais a camada de
  # volume. É o mesmo componente que o exemplo i2s_es8311 do ESP-IDF usa; o
  # alternativo era manter à mão um driver de registradores de codec.
  espressif/esp_codec_dev:
    version: "^1.3.4"
```

Rode `cd firmware && idf.py reconfigure` e confirme que o componente resolve para o target `esp32c6`.

- [ ] **Step 2: Escrever o header**

`firmware/main/som.h`:

```c
#pragma once
#include "driver/i2c_master.h"
#include "esp_err.h"
#include "ui.h"

/* O som da placa: ES8311 no I2C 0x18, I2S nos GPIOs 19/20/22/23 e o
 * amplificador vivendo do rail ALDO2 (não há GPIO de enable nesta placa).
 *
 * Recebe o barramento I2C JÁ ABERTO: o codec divide o bus com o PMIC, o touch,
 * o IMU e o RTC, e abrir um segundo bus nos mesmos pinos é como se conquista um
 * dia de depuração. */
esp_err_t som_init(i2c_master_bus_handle_t bus);

/* WISP_VOL_* -> volume do codec. Chamável a cada payload: igual ao que já vale
 * não faz nada. */
void som_volume(uint8_t degrau);

/* Enfileira UMA reprodução do som do estado. Não bloqueia — a escrita no I2S
 * roda em task própria, porque o laço do LVGL não pode esperar por ela.
 * Chamada durante uma reprodução é ignorada, e não empilhada. */
void som_tocar(wisp_state_t s);
```

- [ ] **Step 3: Escrever a implementação**

`firmware/main/som.c`, com esta estrutura:

```c
#include "som.h"
#include "sons.h"
#include "bsp_c6.h"
#include "driver/i2s_std.h"
#include "esp_check.h"          /* ESP_RETURN_ON_ERROR */
#include "esp_codec_dev.h"
#include "esp_codec_dev_defaults.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "som";

#define TAXA        22050          /* casa com o PCM de sons.c */
#define PIN_MCLK    19
#define PIN_BCLK    20
#define PIN_WS      22
#define PIN_DOUT    23
#define PIN_DIN     21

static esp_codec_dev_handle_t s_dev;
static i2s_chan_handle_t      s_tx;
static volatile bool          s_tocando;
static uint8_t                s_degrau = 0xFF;   /* nenhum aplicado ainda */
static wisp_state_t           s_fila;

/* Os três degraus na escala 0-100 do esp_codec_dev. O 65 é o valor com que o
 * som foi ouvido nesta placa; os outros dois são um passo para cada lado. */
static const int VOLUMES[WISP_VOL_QTD] = {40, 65, 85};
```

O `som_init()`, com o código real (as structs abaixo são as do componente, não
de memória — vieram do exemplo `i2s_es8311` do ESP-IDF instalado):

```c
esp_err_t som_init(i2c_master_bus_handle_t bus)
{
    /* Sem o rail, todo o resto toca no silêncio. */
    ESP_RETURN_ON_ERROR(bsp_c6_amp_power(true), TAG, "amp");

    const i2s_chan_config_t ch = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
    ESP_RETURN_ON_ERROR(i2s_new_channel(&ch, &s_tx, NULL), TAG, "i2s chan");

    i2s_std_config_t std = {
        .clk_cfg  = I2S_STD_CLK_DEFAULT_CONFIG(TAXA),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT,
                                                        I2S_SLOT_MODE_MONO),
        .gpio_cfg = {
            .mclk = PIN_MCLK, .bclk = PIN_BCLK, .ws = PIN_WS,
            .dout = PIN_DOUT, .din = PIN_DIN,
            .invert_flags = {false, false, false},
        },
    };
    std.clk_cfg.mclk_multiple = I2S_MCLK_MULTIPLE_256;
    ESP_RETURN_ON_ERROR(i2s_channel_init_std_mode(s_tx, &std), TAG, "i2s std");
    ESP_RETURN_ON_ERROR(i2s_channel_enable(s_tx), TAG, "i2s en");

    /* O bus I2C vem de fora, como no pmic_start(): o codec divide o barramento
     * com o PMIC, o touch, o IMU e o RTC. */
    const audio_codec_i2c_cfg_t i2c_cfg = {
        .port = 0, .addr = 0x18, .bus_handle = bus,
    };
    const audio_codec_ctrl_if_t *ctrl = audio_codec_new_i2c_ctrl(&i2c_cfg);
    if (!ctrl) return ESP_FAIL;

    const audio_codec_i2s_cfg_t i2s_cfg = {
        .port = I2S_NUM_0, .rx_handle = NULL, .tx_handle = s_tx,
    };
    const audio_codec_data_if_t *data = audio_codec_new_i2s_data(&i2s_cfg);
    if (!data) return ESP_FAIL;

    const es8311_codec_cfg_t es = {
        .ctrl_if     = ctrl,
        .gpio_if     = audio_codec_new_gpio(),
        .codec_mode  = ESP_CODEC_DEV_WORK_MODE_DAC,   /* só saída: os mics ficam fora */
        .master_mode = false,
        .use_mclk    = true,
        .pa_pin      = -1,        /* NÃO HÁ pino de amplificador nesta placa */
        .hw_gain     = {.pa_voltage = 5.0f, .codec_dac_voltage = 3.3f},
        .mclk_div    = 256,
    };
    const audio_codec_if_t *codec = es8311_codec_new(&es);
    if (!codec) return ESP_FAIL;

    const esp_codec_dev_cfg_t dev = {
        .dev_type = ESP_CODEC_DEV_TYPE_OUT, .codec_if = codec, .data_if = data,
    };
    s_dev = esp_codec_dev_new(&dev);
    if (!s_dev) return ESP_FAIL;

    const esp_codec_dev_sample_info_t fmt = {
        .bits_per_sample = 16, .channel = 1, .channel_mask = 0x01,
        .sample_rate = TAXA,
    };
    if (esp_codec_dev_open(s_dev, &fmt) != ESP_CODEC_DEV_OK) return ESP_FAIL;

    ESP_LOGI(TAG, "ES8311 pronto (%d Hz mono)", TAXA);
    return ESP_OK;
}
```

**Se o log aparecer e não sair som**, o suspeito é o mono: o motor que provou esta
placa no projeto vizinho escrevia em slots ESTÉREO. Antes de procurar em outro
lugar, tente `I2S_SLOT_MODE_STEREO` com `channel = 2` / `channel_mask = 0x03` e o
PCM duplicado amostra a amostra. Se resolver, o `sons_para_c.py` passa a gerar
estéreo e o custo de flash dobra — registre a troca no spec.

O `som_volume()`:

```c
void som_volume(uint8_t degrau)
{
    if (degrau >= WISP_VOL_QTD || degrau == s_degrau || !s_dev) return;
    if (esp_codec_dev_set_out_vol(s_dev, VOLUMES[degrau]) == ESP_CODEC_DEV_OK)
        s_degrau = degrau;
}
```

O playback, em task de vida curta:

```c
static void tarefa(void *arg)
{
    (void) arg;
    size_t len = 0;
    const uint8_t *pcm = som_pcm(s_fila, &len);
    esp_codec_dev_write(s_dev, (void *) pcm, len);
    s_tocando = false;
    vTaskDelete(NULL);
}

void som_tocar(wisp_state_t s)
{
    if (!s_dev || s_tocando) return;
    s_tocando = true;
    s_fila = s;
    /* 3072 bytes de pilha: a task só chama write. Prioridade 1 para ficar
     * abaixo do LVGL — o som pode atrasar, a tela não. */
    if (xTaskCreate(tarefa, "som", 3072, NULL, 1, NULL) != pdPASS)
        s_tocando = false;
}
```

- [ ] **Step 4: Compilar e provar com uma chamada temporária**

Se o `firmware/main/CMakeLists.txt` lista as fontes uma a uma, acrescente `som.c` e `sons.c`.

Chame `som_init()` no `main.c` depois do I2C estar aberto (passando o mesmo handle que o `pmic` usa) e, **temporariamente**, um `som_tocar(WISP_ASKING)` logo depois. Grave e ouça:

```bash
cd firmware && idf.py build && idf.py -p /dev/cu.usbmodem101 app-flash monitor
```

Esperado: o log `ES8311 pronto (22050 Hz mono)` e o Ping saindo do alto-falante no boot. Se o log aparecer e o som não, o suspeito não é o codec: é o rail (Task 1) ou o slot mono/pinos.

- [ ] **Step 5: Remover a chamada temporária e commitar**

```bash
git add firmware/main/som.c firmware/main/som.h firmware/main/idf_component.yml \
        firmware/main/CMakeLists.txt firmware/main/main.c
git commit -m "Dar voz à placa: ES8311 por esp_codec_dev, em task própria"
```

---

### Task 9: O gatilho

**Files:**
- Modify: `firmware/main/main.c` (junto do laço que consome o `/state`)

**Interfaces:**
- Consumes: `ui_sessao_dominante()` (Task 6), `som_tocar()` / `som_volume()` (Task 8), `s_cfg` (Task 5).
- Produces: nada.

- [ ] **Step 1: Escrever o gatilho**

No `main.c`, perto de onde o payload já virou `wisp_data_t`:

```c
/* Toca ao ENTRAR no estado dominante, e só.
 *
 * Um `waiting` de dez minutos toca uma vez, não seiscentas — o /state é
 * consultado a cada 600 ms e o que interessa é a transição, não o estado.
 *
 * A PRIMEIRA leitura depois do boot nunca toca: ela só arma o comparador.
 * Ligar a placa e ouvir o alarme de um `asking` que já estava lá antes dela
 * acordar é alarme falso — a placa não presenciou transição nenhuma. */
static void som_no_estado(const wisp_data_t *d)
{
    static wisp_state_t ult = WISP_COUNT;   /* WISP_COUNT = ainda não vi nada */

    const int i = ui_sessao_dominante(d);
    const wisp_state_t dom = (i < 0) ? WISP_IDLE : d->sessions[i].state;
    if (dom == ult) return;

    const bool primeira = (ult == WISP_COUNT);
    const bool voltando = (ult == WISP_OFFLINE);
    ult = dom;
    if (primeira || !s_cfg.som) return;

    /* SAIR do offline não toca. O firmware nasce em WISP_OFFLINE e monta uma
     * sessão sintética com esse estado sempre que o bridge não responde, então
     * a volta é OFFLINE -> algum estado real. Isso não é um evento novo do
     * Claude: é a placa reconectando, e o estado que ela encontra do outro lado
     * já estava lá. Sem esta regra, ligar a placa com um `asking` ativo toca o
     * alarme de um pedido que ninguém acabou de fazer.
     *
     * ENTRAR no offline toca, se estiver marcado — "perdi o bridge" é
     * justamente um aviso que vale ter. */
    if (voltando) return;
    if (!(s_cfg.som_estados & (uint8_t) (1u << dom))) return;

    som_volume(s_cfg.som_volume);
    som_tocar(dom);
}
```

- [ ] **Step 2: Chamar em TODOS os caminhos, inclusive no offline**

Chame `som_no_estado(&dados)` no mesmo ponto em que o payload recém-parseado é entregue à tela, **depois** do parser (para o volume e a lista do payload da vez já valerem) e fora de qualquer seção que segure o mutex do LVGL.

Chame também nos caminhos que montam a sessão sintética de offline (`main.c:982` e `main.c:1056`, onde `sessions[0].state = WISP_OFFLINE`). É o que faz "perdi o bridge" poder avisar — e a regra de `voltando` do Step 1 é o que impede que a reconexão vire alarme.

- [ ] **Step 3: Compilar e gravar**

```bash
cd firmware && idf.py build && idf.py -p /dev/cu.usbmodem101 app-flash monitor
```

- [ ] **Step 4: Provar as três regras, ouvindo**

Com o som da placa ligado no painel e `asking`/`waiting` marcados:

1. **Toca na entrada:** provoque um `asking` real (um comando que peça aprovação). Um som, uma vez.
2. **Não repete durante:** deixe o `asking` parado um minuto. Nenhum som novo.
3. **Não toca no boot:** com um `asking` ativo, reinicie a placa (PWR longo + curto). Ela sobe mostrando `asking` e **em silêncio**.

Depois desmarque tudo no painel e confirme que nada toca — a configuração chega em menos de um segundo, sem regravar.

- [ ] **Step 5: Commitar**

```bash
git add firmware/main/main.c
git commit -m "Tocar na transição do estado dominante, e não no boot"
```

---

### Task 10: As correções de rota

Três lugares afirmam que esta placa não tem áudio utilizável. Com o som funcionando, os três mentem.

**Files:**
- Modify: `mac/Sources/Som.swift:1-20`
- Modify: `bridge/config.py` (já feito na Task 2 — conferir)
- Modify: `README.md` e `CLAUDE.md` (se mencionarem o assunto)

- [ ] **Step 1: Reescrever o cabeçalho do `Som.swift`**

Troque o bloco "SÓ NO MAC, E POR UM MOTIVO DE HARDWARE" por:

```swift
/// O som que avisa que o Claude precisa de você, no Mac.
///
/// A PLACA TAMBÉM TOCA, E TEM A LISTA DELA
/// ---------------------------------------
/// Este arquivo já explicou que a placa C6 não tinha áudio utilizável. Era
/// verdade de outra placa: o amplificador atrás de um expansor é a AMOLED 1.8
/// C6, e nesta uma varredura do barramento não acha expansor nenhum — acha o
/// ES8311 em 0x18. A placa toca desde 24/08/2026; o firmware é quem decide,
/// pela lista em `board.sound`, e este arquivo cuida apenas do Mac.
///
/// As duas listas são independentes de propósito. O caso que isso serve é o Mac
/// calado com a placa avisando — e é por isso que a placa nasce muda, para
/// ninguém passar a ouvir o mesmo aviso duas vezes sem ter pedido.
```

- [ ] **Step 2: Procurar o resto**

```bash
/usr/bin/grep -rn "sem áudio\|no usable audio\|BOARD_HAS_SOUND\|expansor\|TCA9554" \
  --include="*.swift" --include="*.py" --include="*.md" --include="*.c" --include="*.h" . \
  | /usr/bin/grep -v "docs/superpowers/"
```

Use `/usr/bin/grep`, não o `grep` da shell: o da shell respeita `.gitignore` e uma busca vazia não prova ausência. Corrija o que aparecer, deixando os documentos de spec e plano como estão (eles narram a história e devem continuar narrando).

- [ ] **Step 3: Commitar**

```bash
git add -u
git commit -m "Parar de dizer que a placa não tem áudio"
```

---

### Task 11: As duas medições

O spec pede números, não impressões: a placa não tem PSRAM e o log de boot atual reporta 82 KB de RAM interna livres.

**Files:** nenhum alterado (a não ser que uma medição reprove algo).

- [ ] **Step 1: Medir a RAM com o áudio no ar**

O `main.c` já loga RAM interna periodicamente (`PSRAM livre: 0 | RAM interna: ...`). Grave, deixe rodar, e compare a linha antes e depois de um som tocar.

Run: `cd firmware && idf.py -p /dev/cu.usbmodem101 monitor`
Esperado: a RAM interna volta ao mesmo patamar depois da reprodução (a task morre e devolve a pilha). **Reprove** se cada som deixar um degrau para baixo — isso é vazamento, e o suspeito é a task não estar sendo deletada ou o codec ser reaberto a cada toque.

- [ ] **Step 2: Provar que a tela não engasga**

Com um mascote animado na tela, dispare o som (provoque um `asking`) e olhe a animação durante a reprodução.
Esperado: nenhuma travada visível. Se travar, a task de som está com prioridade alta demais ou o `write` está sem buffer DMA suficiente.

- [ ] **Step 3: Ouvir os três sons nos três volumes**

`done`, `error` e um terceiro estado qualquer, cada um em `low`, `medium` e `high`.
Esperado: Glass, Basso e Ping distinguíveis, e os três degraus audivelmente diferentes. Se `low` for inaudível na sua mesa ou `high` distorcer, ajuste os números de `VOLUMES[]` em `som.c` — é para isso que eles são três constantes num lugar só.

- [ ] **Step 4: Rodar o teste do bridge e o simulador uma última vez**

```bash
python3 bridge/test_ui.py
cmake --build sim/build -j && ./sim/build/wisp-sim
```

Esperado: teste sem falhas, simulador desenhando como sempre.

- [ ] **Step 5: Commitar o que a calibração mudou**

```bash
git add -u && git commit -m "Calibrar os três degraus de volume na placa"
```

(Se nada mudou, não há commit — e isso é um resultado, não um problema.)
