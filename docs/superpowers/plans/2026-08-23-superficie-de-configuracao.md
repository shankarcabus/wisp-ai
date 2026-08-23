# Superfície de configuração no painel — plano de implementação

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** O painel do Mac mostra os oito estados do personagem e passa a ajustar rótulos, idioma e tamanho, com ajustes independentes por superfície e sem regravar a placa.

**Architecture:** O `~/.wisp/mascot` de uma linha dá lugar a `~/.wisp/ui.json` com seções `board` e `mac`; o bridge carrega a seção `board` inteira na chave `ui` do `/state`; a placa aplica por `ui_configurar()`, chamada a cada payload e saindo fora quando nada mudou; o Mac aplica a sua seção sem passar pelo bridge.

**Tech Stack:** Swift (`swiftc`, sem dependências), Python 3.9 do sistema, C11 com ESP-IDF 5.5 e LVGL 9.5.0.

**Spec:** `docs/superpowers/specs/2026-08-23-superficie-de-configuracao-design.md`

## Global Constraints

- **O app do Mac continua sendo só `swiftc`, sem dependência nenhuma.**
- `firmware/main/ui.c` e `firmware/main/main.c` mantêm **zero** `#if CONFIG_IDF_TARGET_*`.
- As duas placas continuam compilando: `esp32c6` e `esp32s3`.
- Mutação de objeto do LVGL só com o mutex do display na mão.
- **`vaga_de()` não se altera.** O ajuste de tamanho é do personagem dentro da vaga.
- **`lv_image_set_scale()` nunca é aplicado por quadro** — só na mudança de ajuste ou de layout, com guarda. A conta que travou a placa uma vez foram 93.636 pixels a ~0,76 µs.
- **Chaves e valores do JSON em inglês**, como o `config.json`. O que é em português é o texto exibido.
- Chave ausente, ou uma das quatro ausente, significa **"mantenha o que tem"**.
- JSON inválido **não pode derrubar o bridge**.
- `./sim/folha.sh` continua reproduzível: zero diferenças entre duas execuções.
- Nada de som. É o projeto seguinte.

---

### Task 1: O canal cresce para `ui.json`

**Files:**
- Modify: `bridge/config.py` — o leitor novo, mantendo `mascot()` como migração
- Modify: `bridge/server.py` — a chave `ui` ao lado de `mascot`
- Create: `bridge/test_ui.py`
- Modify: `mac/Sources/Sprites.swift` — publicar o JSON em vez da linha
- Create: `mac/Sources/Ajustes.swift` — a loja de ajustes do app

**Interfaces:**
- Produces:
  - `config.UI_FILE` — `Path`, `~/.wisp/ui.json`
  - `config.ui() -> dict` — sempre com as chaves completas e os padrões preenchidos
  - chave `ui` no payload `/state`, com a seção `board` inteira
  - `Ajustes` em Swift: a struct de ajustes, com leitura, escrita e `publicar()`

- [ ] **Step 1: O teste, que falha**

`bridge/test_ui.py`:

```python
"""
O esquema de ajustes e a sua migração.

Rode: python3 bridge/test_ui.py

SEM FRAMEWORK, como os outros testes deste diretório.

Não toca o ~/.wisp de verdade: aponta config.FOLDER, config.UI_FILE e
config.MASCOT_FILE para um diretório temporário.
"""
import json
import sys
import tempfile
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import config  # noqa: E402

falhas = 0


def checa(nome, obtido, esperado):
    global falhas
    ok = obtido == esperado
    falhas += 0 if ok else 1
    print(f"  {'ok  ' if ok else 'FALHA'}  {nome}")
    if not ok:
        print(f"          esperava {esperado!r}")
        print(f"          obtive   {obtido!r}")


def limpa(tmp):
    for f in (config.UI_FILE, config.MASCOT_FILE):
        if f.exists():
            f.unlink()


def main():
    tmp = Path(tempfile.mkdtemp(prefix="wisp-ui-"))
    config.FOLDER = tmp
    config.UI_FILE = tmp / "ui.json"
    config.MASCOT_FILE = tmp / "mascot"

    PADRAO = {
        "character": "",
        "board": {"size": "medium", "action_label": True,
                  "project_label": True, "language": "en"},
        "mac": {"size": "medium"},
    }

    print("config.ui()")
    limpa(tmp)
    checa("sem nada, devolve o padrao inteiro", config.ui(), PADRAO)

    limpa(tmp)
    config.MASCOT_FILE.write_text("bytelo\n")
    esperado = json.loads(json.dumps(PADRAO))
    esperado["character"] = "bytelo"
    checa("migracao: le o mascot antigo e o resto no padrao", config.ui(), esperado)

    limpa(tmp)
    config.UI_FILE.write_text(json.dumps({
        "character": "terminal",
        "board": {"size": "large", "action_label": False,
                  "project_label": True, "language": "pt"},
        "mac": {"size": "small"},
    }))
    checa("ui.json completo", config.ui(), {
        "character": "terminal",
        "board": {"size": "large", "action_label": False,
                  "project_label": True, "language": "pt"},
        "mac": {"size": "small"},
    })

    limpa(tmp)
    config.UI_FILE.write_text(json.dumps({"board": {"language": "pt"}}))
    esperado = json.loads(json.dumps(PADRAO))
    esperado["board"]["language"] = "pt"
    checa("ui.json parcial: o que falta cai no padrao", config.ui(), esperado)

    limpa(tmp)
    config.UI_FILE.write_text("{ isto nao e json")
    checa("json invalido nao derruba: devolve o padrao", config.ui(), PADRAO)

    limpa(tmp)
    config.UI_FILE.write_text(json.dumps({"character": "bytelo"}))
    config.MASCOT_FILE.write_text("terminal\n")
    esperado = json.loads(json.dumps(PADRAO))
    esperado["character"] = "bytelo"
    checa("com os dois, o ui.json ganha", config.ui(), esperado)

    print()
    print("all passed" if not falhas else f"{falhas} falha(s)")
    return 1 if falhas else 0


if __name__ == "__main__":
    sys.exit(main())
```

- [ ] **Step 2: Rodar e ver falhar**

```bash
python3 bridge/test_ui.py
```

Esperado: `AttributeError: module 'config' has no attribute 'ui'`.

- [ ] **Step 3: O leitor**

Em `bridge/config.py`, junto de `MASCOT_FILE`:

```python
# Os ajustes de interface publicados pelo painel do Mac.
#
# Substitui o MASCOT_FILE de uma linha, que ficou pequeno na primeira vez que se
# quis ajustar mais de uma coisa. O antigo continua sendo LIDO, como migração:
# ver ui() abaixo.
UI_FILE = FOLDER / "ui.json"

# Os padrões, e o contrato do formato num lugar só. Chaves e valores em inglês,
# como as do config.json — o que é em português é o texto que aparece na tela,
# não a configuração.
UI_DEFAULTS = {
    "character": "",
    "board": {"size": "medium", "action_label": True,
              "project_label": True, "language": "en"},
    "mac": {"size": "medium"},
}


def ui() -> dict:
    """
    Os ajustes, SEMPRE com todas as chaves preenchidas.

    Quem consome não precisa perguntar se a chave existe, e é por isso que o
    merge é aqui: espalhar `.get(chave, padrao)` por três arquivos é como se
    cria três padrões diferentes para a mesma coisa.

    A MIGRAÇÃO NÃO TEM PASSO
    ------------------------
    Sem o ui.json, tenta o `mascot` de uma linha e usa só o personagem dele. O
    app escreve o ui.json na primeira mudança que alguém fizer; ninguém roda
    nada e nada se perde. Com os dois presentes, o ui.json ganha — ele é o novo.

    JSON inválido devolve o padrão. Um arquivo que alguém editou à mão e errou
    uma vírgula não pode derrubar o bridge: o custo do erro é a configuração
    ignorada, e ela aparece na tela.
    """
    cfg = _merge(UI_DEFAULTS, {})
    try:
        cfg = _merge(cfg, json.loads(UI_FILE.read_text()))
    except (OSError, json.JSONDecodeError):
        antigo = mascot()
        if antigo:
            cfg["character"] = antigo
    return cfg
```

`_merge` já existe neste módulo e é recursivo, que é exatamente o que a seção
`board` precisa.

- [ ] **Step 4: Rodar e ver passar**

```bash
python3 bridge/test_ui.py && python3 bridge/test_mascot.py && python3 bridge/test_limits.py
```

Esperado: `all passed` nos três. O `test_mascot.py` continua valendo porque
`mascot()` continua existindo, agora como migração.

- [ ] **Step 5: A chave no payload**

Em `bridge/server.py`, substituir o bloco que hoje manda só o personagem:

```python
        # Os ajustes de interface. `mascot` continua com o nome que tem porque o
        # firmware já gravado o lê, e quebrar isso trocaria um problema
        # resolvido por um em aberto. A seção `board` vai inteira, sem renomear
        # nada: chave ausente significa "placa, mantenha o que você tem".
        _ui = config.ui()
        if _ui["character"]:
            snap["mascot"] = _ui["character"]
        snap["ui"] = _ui["board"]
```

- [ ] **Step 6: A loja de ajustes no app**

`mac/Sources/Ajustes.swift` — um arquivo próprio porque o `Sprites.swift` é sobre
carregar arte, e ajuste de interface não é isso:

```swift
import Foundation

/// Os ajustes que o painel publica, e o único lugar que conhece o formato.
///
/// UserDefaults continua sendo a loja do app — é dele que a interface lê, é nele
/// que o SwiftUI observa. O `~/.wisp/ui.json` é a versão PUBLICADA, para o
/// bridge e a placa: um arquivo, porque `defaults read` do lado do Python passa
/// pelo cfprefsd e leitura defasada ali é o tipo de bug que custa uma tarde.
enum Ajustes {

    enum Tamanho: String, CaseIterable {
        case small, medium, large
        /// Fator sobre os tamanhos de hoje. `medium` é exatamente o que existe,
        /// para quem nunca mexer não ver diferença.
        var fator: CGFloat {
            switch self {
            case .small:  return 0.8
            case .medium: return 1.0
            case .large:  return 1.25
            }
        }
        var rotulo: String {
            switch self {
            case .small:  return "small"
            case .medium: return "medium"
            case .large:  return "large"
            }
        }
    }

    // As chaves do UserDefaults. Prefixadas, para não colidir com as que já
    // existem (`mascot`, `floating`, `fetchLimits`).
    static let kBoardSize    = "ui.board.size"
    static let kBoardAction  = "ui.board.action_label"
    static let kBoardProject = "ui.board.project_label"
    static let kBoardLang    = "ui.board.language"
    static let kMacSize      = "ui.mac.size"

    static var boardSize: Tamanho {
        get { Tamanho(rawValue: UserDefaults.standard.string(forKey: kBoardSize) ?? "") ?? .medium }
        set { UserDefaults.standard.set(newValue.rawValue, forKey: kBoardSize); publicar() }
    }
    static var macSize: Tamanho {
        get { Tamanho(rawValue: UserDefaults.standard.string(forKey: kMacSize) ?? "") ?? .medium }
        set { UserDefaults.standard.set(newValue.rawValue, forKey: kMacSize); publicar() }
    }
    static var boardAction: Bool {
        get { UserDefaults.standard.object(forKey: kBoardAction) as? Bool ?? true }
        set { UserDefaults.standard.set(newValue, forKey: kBoardAction); publicar() }
    }
    static var boardProject: Bool {
        get { UserDefaults.standard.object(forKey: kBoardProject) as? Bool ?? true }
        set { UserDefaults.standard.set(newValue, forKey: kBoardProject); publicar() }
    }
    static var boardLanguage: String {
        get { UserDefaults.standard.string(forKey: kBoardLang) ?? "en" }
        set { UserDefaults.standard.set(newValue, forKey: kBoardLang); publicar() }
    }

    /// Onde o JSON é publicado. Var para a sonda poder apontar para /tmp — um
    /// teste que escreve no ~/.wisp de quem roda muda a configuração real.
    static var arquivo = FileManager.default.homeDirectoryForCurrentUser
        .appendingPathComponent(".wisp/ui.json")

    /// Escreve o JSON inteiro, por temp+rename, para quem lê nunca ver meio
    /// arquivo. Chamado por cada setter: o app não tem "salvar".
    static func publicar() {
        let payload: [String: Any] = [
            "character": Sprites.chosen,
            "board": ["size": boardSize.rawValue,
                      "action_label": boardAction,
                      "project_label": boardProject,
                      "language": boardLanguage],
            "mac": ["size": macSize.rawValue],
        ]
        let dir = arquivo.deletingLastPathComponent()
        let tmp = arquivo.appendingPathExtension("tmp")
        do {
            try FileManager.default.createDirectory(at: dir, withIntermediateDirectories: true)
            let dados = try JSONSerialization.data(withJSONObject: payload,
                                                   options: [.prettyPrinted, .sortedKeys])
            try dados.write(to: tmp)
            _ = try FileManager.default.replaceItemAt(arquivo, withItemAt: tmp)
        } catch {
            // Falhar aqui não pode derrubar a mudança no Mac: o que se perde é
            // a placa acompanhar, e o mascote da barra já mudou.
            NSLog("wisp: could not publish the settings: \(error.localizedDescription)")
        }
    }
}
```

- [ ] **Step 7: O `Sprites.chosen` passa a publicar o JSON**

Em `mac/Sources/Sprites.swift`, o `setter` troca `publish(newValue)` por
`Ajustes.publicar()`, e o `publish(_:)`/`published` saem — o formato agora mora
num lugar só. O `~/.wisp/mascot` deixa de ser escrito; o bridge continua lendo-o
como migração, para instalações que ainda o tenham.

- [ ] **Step 8: A sonda, e os três testes**

```bash
mkdir -p /tmp/wisp-probe2
cat > /tmp/wisp-probe2/main.swift <<'EOF'
import Foundation
Ajustes.arquivo = URL(fileURLWithPath: "/tmp/wisp-probe2/ui.json")
Ajustes.boardLanguage = "pt"
Ajustes.boardAction = false
Ajustes.macSize = .large
print(try! String(contentsOf: Ajustes.arquivo, encoding: .utf8))
EOF
swiftc -O -target arm64-apple-macosx14.0 -o /tmp/wisp-probe2/probe \
    /tmp/wisp-probe2/main.swift mac/Sources/Ajustes.swift \
    mac/Sources/Sprites.swift mac/Sources/Model.swift mac/Sources/Mascot.swift
/tmp/wisp-probe2/probe
for k in ui.board.size ui.board.action_label ui.board.project_label ui.board.language ui.mac.size; do
    defaults delete com.marciovicente.wisp "$k" 2>/dev/null || true
done
```

Esperado: um JSON com `"language": "pt"`, `"action_label": false` e
`"mac": {"size": "large"}`. A limpeza no fim existe porque a sonda escreve no
UserDefaults de verdade.

- [ ] **Step 9: O app compila**

```bash
./mac/build.sh
```

Esperado: sem erro, e **sem dependência nova**.

- [ ] **Step 10: Commit**

```bash
git add bridge/config.py bridge/server.py bridge/test_ui.py \
        mac/Sources/Ajustes.swift mac/Sources/Sprites.swift
git commit -F - <<'MSG'
Grow the channel from one line to a small JSON

One value was enough for the character and stopped being enough the moment there
was more to set. The sections are per surface because "large" on a 480px screen
and "large" on a 46px floating mascot are not the same word.

The migration has no migration step: without ui.json the reader falls back to the
one-line file and takes the character from it, and the app writes the new format
on the first change anyone makes. Invalid JSON returns the defaults rather than
taking the bridge down — the cost of a stray comma should be settings ignored,
not a dead bridge.

Keys and values are English like config.json's. What is Portuguese is the text on
screen.
MSG
```

---

### Task 2: A placa obedece rótulos e idioma

**Files:**
- Modify: `firmware/main/ui.h` — a struct de ajustes e `ui_configurar()`
- Modify: `firmware/main/ui.c` — aplicar, e a tabela de nomes em português
- Modify: `firmware/main/mascote.h` — `rotulos` passa a ser padrão
- Modify: `firmware/main/main.c` — ler a chave `ui` e chamar
- Modify: `sim/cena.c` — comandos para exercitar

**Interfaces:**
- Produces:

```c
/* Ajustes de interface, vindos do painel pela chave `ui` do /state. */
typedef struct {
    bool acao;        /* mostrar o rótulo de detalhe */
    bool projetos;    /* mostrar a lista de projetos */
    bool pt;          /* texto em português; false = inglês */
    uint8_t tamanho;  /* 0 pequeno, 1 médio, 2 grande */
} wisp_cfg_t;

/* Aplica os ajustes. Toma o mutex sozinha — chamável de qualquer task.
 * Igual ao que já vale não faz nada, então quem chama pode chamar a cada
 * payload. */
void ui_configurar(const wisp_cfg_t *c);
```

- [ ] **Step 1: A struct e a tabela**

Em `ui.h`, a `wisp_cfg_t` e `ui_configurar()` como acima.

Em `ui.c`, ao lado da tabela `NOME[]`, a segunda:

```c
/* Os mesmos oito estados em português.
 *
 * NENHUMA das oito leva acento, e isso é escolha, não sorte: as Montserrat do
 * LVGL não têm acento — é a restrição que o comentário acima registra — e
 * palavras acentuadas sairiam como quadrado vazio. Foram tiradas da folha de
 * referência do Bytelo, onde a única acentuada era "sem conexão", e "desligado"
 * diz o mesmo.
 *
 * Quem acrescentar palavra com acento aqui vai ver o quadrado e precisa saber
 * por quê. */
static const char *NOME_PT[WISP_COUNT] = {
    [WISP_IDLE] = "parado",       [WISP_WORKING] = "pensando",
    [WISP_TOOL] = "trabalhando",  [WISP_ASKING]  = "perguntando",
    [WISP_WAITING] = "pedindo ajuda", [WISP_DONE] = "pronto",
    [WISP_ERROR] = "falhou",      [WISP_OFFLINE] = "desligado",
};

/* Os ajustes em vigor. Os padrões são o comportamento de antes disto existir,
 * para uma placa que nunca receba a chave `ui` não mudar de cara. */
static wisp_cfg_t g_cfg = {.acao = true, .projetos = true, .pt = false, .tamanho = 1};
```

E o uso de `NOME[...]` em `ui_update()` passa a escolher a tabela:

```c
    const char *const *nomes = g_cfg.pt ? NOME_PT : NOME;
```

- [ ] **Step 2: `rotulos` passa a ser padrão**

Em `mascote.h`, o comentário de `rotulos` ganha:

```c
    /* PADRÃO do personagem, não decisão final: os ajustes do painel sobrepõem.
     * Existe para um personagem novo não nascer sem opinião. */
    bool        rotulos;
```

Em `aplicar_layout()`, a linha que hoje é

```c
        const bool mostrar_rotulos = ativo && mascote_ativo()->rotulos;
```

passa a decidir os dois rótulos separadamente, porque eles já são dois objetos:

```c
        /* O personagem sugere; o ajuste manda. Um personagem cuja composição é
         * uma cara sozinha (rotulos = false) some com os dois por padrão, e quem
         * quiser um deles de volta pede no painel. */
        const bool sugere = mascote_ativo()->rotulos;
        const bool ver_acao = ativo && sugere && g_cfg.acao;
        const bool ver_proj = ativo && sugere && g_cfg.projetos;
```

E as duas listas de mostrar/esconder passam a ser separadas, uma por rótulo, em
vez do laço sobre os dois:

```c
        if (m->detail) {
            if (ver_acao) lv_obj_remove_flag(m->detail, LV_OBJ_FLAG_HIDDEN);
            else          lv_obj_add_flag(m->detail, LV_OBJ_FLAG_HIDDEN);
        }
        if (m->project) {
            if (ver_proj) lv_obj_remove_flag(m->project, LV_OBJ_FLAG_HIDDEN);
            else          lv_obj_add_flag(m->project, LV_OBJ_FLAG_HIDDEN);
        }
```

E o recentrar passa a olhar os dois:

```c
        if (!ver_acao && !ver_proj && total <= 1) cy = 0;
```

- [ ] **Step 3: `ui_configurar()`**

Em `ui.c`, junto de `ui_personagem()`:

```c
void ui_configurar(const wisp_cfg_t *c)
{
    if (!c) return;
    if (c->acao == g_cfg.acao && c->projetos == g_cfg.projetos
        && c->pt == g_cfg.pt && c->tamanho == g_cfg.tamanho) return;

    bsp_display_lock(-1);
    g_cfg = *c;
    /* Reaplicar o layout é o que faz os rótulos aparecerem ou sumirem e o
     * mascote recentrar; o idioma entra na próxima atualização de texto, que
     * vem no mesmo ui_update() logo depois. */
    g_qtd = -1;
    bsp_display_unlock();
    ESP_LOGI(TAG, "ajustes: acao=%d projetos=%d pt=%d tamanho=%u",
             c->acao, c->projetos, c->pt, (unsigned) c->tamanho);
}
```

- [ ] **Step 4: `main.c` lê a chave `ui`**

Em `interpretar()`, junto do campo `mascot`:

```c
    /* Ajustes de interface. Chave ausente, ou uma das quatro ausente, mantém o
     * que já vale — é o mesmo contrato do personagem, e é o que faz uma placa
     * com bridge antigo continuar igual. */
    const cJSON *cfg = cJSON_GetObjectItemCaseSensitive(raiz, "ui");
    if (cJSON_IsObject(cfg)) {
        const cJSON *v;
        if (cJSON_IsBool(v = cJSON_GetObjectItemCaseSensitive(cfg, "action_label")))
            s_cfg.acao = cJSON_IsTrue(v);
        if (cJSON_IsBool(v = cJSON_GetObjectItemCaseSensitive(cfg, "project_label")))
            s_cfg.projetos = cJSON_IsTrue(v);
        if (cJSON_IsString(v = cJSON_GetObjectItemCaseSensitive(cfg, "language")))
            s_cfg.pt = (strcmp(v->valuestring, "pt") == 0);
        if (cJSON_IsString(v = cJSON_GetObjectItemCaseSensitive(cfg, "size"))) {
            const char *t = v->valuestring;
            s_cfg.tamanho = !strcmp(t, "small") ? 0 : (!strcmp(t, "large") ? 2 : 1);
        }
    }
```

Com `static wisp_cfg_t s_cfg = {.acao = true, .projetos = true, .pt = false, .tamanho = 1};`
junto de `s_mascote_rx`. E, no bloco de payload bem-sucedido, `ui_configurar(&s_cfg);`
imediatamente **depois** de `aplicar_personagem()` — o personagem primeiro, porque
é ele que cria os objetos que os ajustes vão dispor.

- [ ] **Step 5: Comandos no simulador**

Em `sim/cena.c`, um comando só, com quatro argumentos posicionais é frágil;
melhor quatro comandos:

```c
    } else if (!strcmp(cmd, "acao") || !strcmp(cmd, "proj")
            || !strcmp(cmd, "idioma") || !strcmp(cmd, "tam")) {
        static wisp_cfg_t c = {.acao = true, .projetos = true, .pt = false, .tamanho = 1};
        if (!strcmp(cmd, "acao"))        c.acao = (atoi(arg) != 0);
        else if (!strcmp(cmd, "proj"))   c.projetos = (atoi(arg) != 0);
        else if (!strcmp(cmd, "idioma")) c.pt = !strcmp(arg, "pt");
        else                             c.tamanho = !strcmp(arg, "small") ? 0
                                                   : (!strcmp(arg, "large") ? 2 : 1);
        ui_configurar(&c);
        ui_update(&d);
        return;
```

E a ajuda lista `acao <0|1> | proj <0|1> | idioma <en|pt> | tam <small|medium|large>`.

- [ ] **Step 6: Compilar e olhar os quatro combinados**

```bash
cmake --build sim/build -j
{ echo "idioma pt"; echo "todos tool"; echo "shot /tmp/c-pt.bmp"
  echo "acao 0";               echo "shot /tmp/c-sem-acao.bmp"
  echo "acao 1"; echo "proj 0"; echo "shot /tmp/c-sem-proj.bmp"
  echo "acao 0";               echo "shot /tmp/c-sem-nada.bmp"
  echo quit; } | ./sim/build/wisp-sim --headless
for f in c-pt c-sem-acao c-sem-proj c-sem-nada; do
    sips -s format png /tmp/$f.bmp --out /tmp/$f.png >/dev/null; done
open /tmp/c-pt.png /tmp/c-sem-acao.png /tmp/c-sem-proj.png /tmp/c-sem-nada.png
```

Esperado: `c-pt` com **trabalhando** embaixo em vez de *working*; `c-sem-acao` só
com a lista de projetos; `c-sem-proj` só com a ação; `c-sem-nada` com o mascote
**centrado**, sem texto.

- [ ] **Step 7: A folha continua reproduzível, e as duas placas compilam**

```bash
./sim/folha.sh /tmp/g-a >/dev/null && ./sim/folha.sh /tmp/g-b >/dev/null
DIF=0; for f in /tmp/g-a/*.bmp; do cmp -s "$f" "/tmp/g-b/$(basename $f)" || DIF=$((DIF+1)); done
echo "diferencas: $DIF"
export PATH=/usr/bin:/bin
source ~/esp/esp-idf/export.sh
cd firmware
for alvo in esp32c6 esp32s3; do
  rm -rf build sdkconfig; idf.py set-target $alvo && idf.py build || echo "$alvo FALHOU"
done
rm -rf build sdkconfig && idf.py set-target esp32c6 && idf.py build
cd .. && git checkout -- firmware/dependencies.lock
```

- [ ] **Step 8: Commit** (em chamada separada da compilação, que o `PATH` restrito quebra o `cat`)

```bash
git add firmware/main/ui.h firmware/main/ui.c firmware/main/mascote.h \
        firmware/main/main.c sim/cena.c
git commit -F - <<'MSG'
Let the panel decide the labels and their language

The character's `rotulos` becomes a default rather than a verdict: it exists so a
new character does not arrive without an opinion, and the panel overrides it. The
two labels split, because they were already two objects.

The Portuguese table needs no font work. None of its eight words carries an
accent, and that is a choice and not luck — LVGL's Montserrat has none, so an
accented word would draw an empty box. They come from Bytelo's own reference
sheet, where the only accented one was "sem conexão", and "desligado" says it.
MSG
```

---

### Task 3: A placa obedece o tamanho

**Files:**
- Modify: `firmware/main/mascote.h` — o tamanho na interface do personagem
- Modify: `firmware/main/mascote_bytelo.c` — `CARA_PCT` vira degrau
- Modify: `firmware/main/mascote_terminal.c` — `lv_image_set_scale()`
- Modify: `firmware/main/ui.c` — passar o degrau ao `dispor`

**Interfaces:**
- Consumes: `wisp_cfg_t.tamanho` da Task 2.
- Produces: `dispor` ganha o parâmetro `uint8_t tamanho`.

- [ ] **Step 1: O parâmetro**

Em `mascote.h`, a assinatura de `dispor` ganha `uint8_t tamanho` no fim, com o
comentário de que 0/1/2 são pequeno/médio/grande e que **o que cada degrau
significa é do personagem** — a mesma palavra não quer dizer o mesmo para uma
imagem de 306px e para uma cara desenhada.

Em `aplicar_layout()`, a chamada passa `g_cfg.tamanho`.

- [ ] **Step 2: O Bytelo**

`CARA_PCT` sai de constante e vira tabela:

```c
/* O que cada degrau significa para esta cara. O médio é 72%, que é o que existia
 * antes de haver ajuste. A margem que sobra é onde os adornos moram, e ela
 * encolhe no degrau grande — consequência aceita: adorno mais apertado é melhor
 * que cara pequena para quem pediu grande. */
static const uint8_t CARA_PCT[3] = {60, 72, 88};
#define CARA(d, t) ((int16_t) ((int32_t) (d) * CARA_PCT[(t) < 3 ? (t) : 1] / 100))
```

`bytelo_t` ganha `uint8_t tam;` — o degrau em vigor. `bytelo_dispor` o guarda ao
receber, e `bytelo_animar` o lê: o `animar` não recebe o tamanho na assinatura, e
inventar um global para isso seria um segundo lugar guardando a mesma coisa.

Todas as chamadas de `CARA(m->d)` passam a ser `CARA(m->d, p->tam)`.

- [ ] **Step 3: O Terminal**

Aqui está a armadilha, e o comentário tem de dizê-la:

```c
    /* TAMANHO DA FOTO: escala, não o tamanho do objeto.
     *
     * O ui.c registra que a arte tem exatamente 306px e que imagem MAIOR que o
     * objeto sai CORTADA, não reduzida. Então encolher o objeto recorta o
     * mascote em vez de diminuí-lo — foi assim que este ajuste nasceu errado na
     * primeira tentativa.
     *
     * lv_image_set_scale transforma por software, e a conta que travou esta
     * placa uma vez foram 93.636 pixels a ~0,76 µs. É aceitável AQUI e só aqui
     * porque `dispor` roda na mudança de layout ou de ajuste, nunca por quadro —
     * e a guarda abaixo é o que garante isso. */
    static const uint8_t FOTO_PCT[3] = {70, 100, 118};
    const uint8_t pct = FOTO_PCT[tamanho < 3 ? tamanho : 1];
    if (T->p_escala != pct) {
        T->p_escala = pct;
        lv_image_set_scale(T->foto, 256 * pct / 100);
    }
```

Com `uint8_t p_escala;` no `terminal_t`, inicializado em 0 para a primeira
aplicação sempre acontecer. O objeto da foto continua recebendo `d` como tamanho.

- [ ] **Step 4: O teste que decide**

```bash
cmake --build sim/build -j
{ echo "char terminal"; echo "todos idle"
  echo "tam small";  echo "shot /tmp/t-small.bmp"
  echo "tam medium"; echo "shot /tmp/t-medium.bmp"
  echo "tam large";  echo "shot /tmp/t-large.bmp"
  echo "char bytelo"
  echo "tam small";  echo "shot /tmp/b-small.bmp"
  echo "tam large";  echo "shot /tmp/b-large.bmp"
  echo quit; } | ./sim/build/wisp-sim --headless
for f in t-small t-medium t-large b-small b-large; do
    sips -s format png /tmp/$f.bmp --out /tmp/$f.png >/dev/null; done
open /tmp/t-small.png /tmp/t-medium.png /tmp/t-large.png /tmp/b-small.png /tmp/b-large.png
```

Esperado, e é o critério: **o Terminal pequeno tem de sair REDUZIDO, não
cortado.** Se aparecer um pedaço do computador em vez do computador inteiro
menor, o `lv_image_set_scale()` não está no caminho. E o Bytelo grande tem de ter
a cara maior com os adornos mais próximos dela.

- [ ] **Step 5: O FPS não pode cair ao mexer no tamanho**

```bash
{ echo "char bytelo"
  for i in $(seq 1 20); do echo "tam small"; echo "tam large"; done
  echo quit; } | ./sim/build/wisp-sim --headless 2>&1 | grep -cE "^E " | xargs echo "erros:"
```

Esperado: `erros: 0`. Quarenta mudanças de tamanho e nenhuma transformação por
quadro — a guarda `p_escala` é o que se está testando.

- [ ] **Step 6: A folha ganha as variantes**

O spec pede que a folha de contato cubra as variantes que importam. O produto
cartesiano seriam 192 imagens por personagem, o que ninguém olha; uma seleção
bounded é o que serve. Acrescentar ao fim do `sim/folha.sh`, antes do `quit`:

```bash
    # As variantes dos ajustes, num estado só cada — o que se compara aqui é o
    # AJUSTE, e repetir os oito estados por variante daria 192 imagens que
    # ninguém abre.
    echo "todos tool"
    for t in small medium large; do
        echo "tam $t";      echo "shot $OUT/tam-$t.bmp"
    done
    echo "tam medium"
    for i in "1 1" "1 0" "0 1" "0 0"; do
        set -- $i
        echo "acao $1"; echo "proj $2"; echo "shot $OUT/rot-$1$2.bmp"
    done
    echo "acao 1"; echo "proj 1"
    for l in en pt; do
        echo "idioma $l";   echo "shot $OUT/idioma-$l.bmp"
    done
    echo "idioma en"
```

E a checagem de quantidade do script passa de 27 para 38.

- [ ] **Step 7: A folha, as placas, e commit**

Igual ao Step 7 da Task 2, e depois:

```bash
git add firmware/main/mascote.h firmware/main/mascote_bytelo.c \
        firmware/main/mascote_terminal.c firmware/main/ui.c
git commit -F - <<'MSG'
Let the panel set the mascot's size, per character

What a step means belongs to the character, because the same word does not mean
the same thing for a 306px render and for a face drawn from objects. Bytelo
scales its face inside the slot; the Terminal cannot, because shrinking its photo
object crops the art rather than reducing it — the ui.c comment records that the
art is exactly 306px and that a larger image comes out cut.

So the Terminal scales with lv_image_set_scale, which transforms in software, and
that is acceptable here and only here because dispor runs on a layout or settings
change and never per frame. The guard on the previous percentage is what makes
"never" true.

vaga_de is untouched. The size is the character's inside the slot.
MSG
```

---

### Task 4: O painel — a galeria e os controles

**Files:**
- Modify: `mac/Sources/Panel.swift` — a seção "Mascote"

**Interfaces:**
- Consumes: `Ajustes` da Task 1; `Sprites.image(_:)` e `MascotState.allCases`.

- [ ] **Step 1: A galeria**

Uma `View` própria, no `Panel.swift`, acima dos controles:

```swift
/// Os oito estados do personagem escolhido, em fileira.
///
/// Sai de graça: os sprites já estão carregados para desenhar o estado atual, e
/// mostrar os oito é o que responde "não consigo ver todos os estados". Sem
/// rótulo por estado — oito palavras em 300px de largura viram ruído, e o que se
/// quer aqui é reconhecer a cara, não ler o nome.
struct Galeria: View {
    var body: some View {
        HStack(spacing: 6) {
            ForEach(MascotState.allCases, id: \.self) { s in
                Mascot(state: s, side: 26)
                    .help(s.label)
            }
        }
    }
}
```

- [ ] **Step 2: Os controles**

No `footer`, o Picker de personagem sai de onde está e entra numa seção com
`SectionHeader(title: "Mascot")`, seguido da `Galeria()` e de:

- `Toggle` "Action under the mascot" → `Ajustes.boardAction`
- `Toggle` "Projects under the mascot" → `Ajustes.boardProject`
- `Picker` "Language" com `en` e `pt` → `Ajustes.boardLanguage`
- `Picker` "Size on the board" com os três degraus → `Ajustes.boardSize`
- `Picker` "Size on the Mac" com os três degraus → `Ajustes.macSize`

Cada um com `@State` local espelhando o `Ajustes` e `.onChange` escrevendo de
volta, que é o padrão que o `character` já usa neste arquivo. Concretamente, para
não ficar em "o mesmo padrão":

```swift
    @State private var acao     = Ajustes.boardAction
    @State private var projetos = Ajustes.boardProject
    @State private var idioma   = Ajustes.boardLanguage
    @State private var tamPlaca = Ajustes.boardSize
    @State private var tamMac   = Ajustes.macSize
```

```swift
            Toggle(isOn: $acao) {
                Text("Action under the mascot").font(.system(size: 11))
            }
            .toggleStyle(.checkbox)
            .onChange(of: acao) { _, v in Ajustes.boardAction = v }

            Picker("Language", selection: $idioma) {
                Text("English").tag("en")
                Text("Português").tag("pt")
            }
            .font(.system(size: 11))
            .onChange(of: idioma) { _, v in Ajustes.boardLanguage = v }

            Picker("Size on the board", selection: $tamPlaca) {
                ForEach(Ajustes.Tamanho.allCases, id: \.self) { Text($0.rotulo).tag($0) }
            }
            .font(.system(size: 11))
            .onChange(of: tamPlaca) { _, v in Ajustes.boardSize = v }
```

`projetos` e `tamMac` seguem os mesmos dois moldes — `Toggle` e `Picker` — com os
rótulos "Projects under the mascot" e "Size on the Mac".

- [ ] **Step 3: Olhar**

```bash
./mac/build.sh && open mac/build/Wisp.app
```

Esperado: a seção "Mascot" com a fileira dos oito e os cinco controles. **Olhar se
o popover ainda cabe na tela** — foi o risco assumido no spec, e a saída, se não
couber, é a seção recolhível que ficou de fora.

- [ ] **Step 4: Commit**

```bash
git add mac/Sources/Panel.swift
git commit -F - <<'MSG'
Show the eight states in the panel, and the controls beside them

The sprites were already loaded to draw the current state, so showing all eight
costs nothing and answers the actual complaint — that choosing a character meant
choosing blind.
MSG
```

---

### Task 5: O Mac obedece o seu tamanho

**Files:**
- Modify: `mac/Sources/Floating.swift` — o lado do flutuante
- Modify: `mac/Sources/Panel.swift` — os dois lados do painel

- [ ] **Step 1: Aplicar o fator**

Os três lugares que passam `side:` recebem o fator:

| arquivo | hoje | passa a ser |
|---|---|---|
| `Floating.swift:54` | `side: size` | `side: size * Ajustes.macSize.fator` |
| `Panel.swift:84` | `side: 20` | `side: 20 * Ajustes.macSize.fator` |
| `Panel.swift:148` | `side: 42` | `side: 42 * Ajustes.macSize.fator` |

O flutuante reposiciona pelo `resize()` que o `Floating.swift` já tem — o mesmo
caminho que a mudança de estado usa quando a bolha cresce.

- [ ] **Step 2: Olhar os três degraus**

```bash
./mac/build.sh && open mac/build/Wisp.app
```

Trocar o degrau e olhar: o flutuante muda de tamanho **sem escorregar** na tela, e
o mascote da lista de sessões acompanha.

- [ ] **Step 3: Commit**

```bash
git add mac/Sources/Floating.swift mac/Sources/Panel.swift
git commit -F - <<'MSG'
Apply the Mac's own size step to the floating and panel mascots

Medium is exactly what existed, so nobody who never touches it sees a change.
MSG
```

---

### Task 6: Ponta a ponta na placa

- [ ] **Step 1: Gravar o app na placa**

```bash
export PATH=/usr/bin:/bin
source ~/esp/esp-idf/export.sh
idf.py -C firmware -p /dev/cu.usbmodem101 app-flash
```

Sai o firmware atual do Wisp, entra este. **Só a partição de app** — a NVS, com a
senha do WiFi e o token, não é tocada.

- [ ] **Step 2: Trocar o app instalado**

O bridge que roda é o de dentro do `~/Applications/Wisp.app`, não o do
repositório — editar `bridge/server.py` e testar contra a porta 4666 não prova
nada até o bundle ser trocado.

```bash
osascript -e 'tell application "Wisp" to quit'
rm -rf ~/Applications/Wisp.app
cp -R mac/build/Wisp.app ~/Applications/Wisp.app
codesign -dvv ~/Applications/Wisp.app 2>&1 | grep Authority
open ~/Applications/Wisp.app
```

Esperado: `Authority=Wisp Dev`. É essa identidade que faz a assinatura derivar do
certificado e não do conteúdo, então o keychain não perde a permissão.

- [ ] **Step 3: Mexer em cada controle e olhar a placa**

No painel: idioma para `pt`, ação desligada, projetos desligados, tamanho pequeno,
tamanho grande. A cada mudança, o serial deve dizer `ajustes: ...` e a tela
obedecer, **sem regravar**.

```bash
~/.espressif/python_env/idf5.5_py3.9_env/bin/python -c "
import glob, time, serial
s = serial.Serial(glob.glob('/dev/cu.usbmodem*')[0], 115200, timeout=1)
fim = time.time() + 180
while time.time() < fim:
    b = s.readline()
    if b:
        t = b.decode('utf-8','replace').rstrip()
        if any(k in t for k in ('ajustes','personagem','FPS','E (','wdt')): print(t, flush=True)
s.close()"
```

Esperado: uma linha `ajustes:` por mudança, FPS estável, nenhum watchdog.

- [ ] **Step 4: Escrever o que ficou verificado, e commitar**

Em `CLAUDE.md`, a linha do canal passa a citar o `ui.json`, e as expectativas de
verificação ganham a data e o que foi conferido na placa. Em `sim/README.md`, os
quatro comandos novos na tabela.

```bash
git add CLAUDE.md sim/README.md
git commit -F - <<'MSG'
Record the configuration surface as verified on hardware
MSG
```

---

## Notas de execução

**Dependências reais entre as tasks:** a 2 e a 3 precisam da 1 (não há chave `ui`
no payload sem ela). A 4 precisa da 1 (não há `Ajustes`). A 5 precisa da 1. A 6
precisa de todas. A 2 e a 3 são independentes entre si, e a 3 é a que pode dar
errado — o Terminal cortado em vez de reduzido.

**Onde parar e pensar.** Se o Terminal em `tam small` sair cortado, é o
`lv_image_set_scale()` fora do caminho, e não outra coisa. Se o FPS cair ao mexer
no tamanho, é a guarda `p_escala`. Se o popover não couber na tela depois da Task
4, a saída é a seção recolhível — não uma janela nova.

**O que não fazer.** Som, em qualquer superfície. Mexer em `vaga_de()`.
Transformar por quadro. Acrescentar dependência ao app do Mac. Commitar
`firmware/dependencies.lock`. Ampliar a fonte do LVGL — as oito palavras não
precisam.
