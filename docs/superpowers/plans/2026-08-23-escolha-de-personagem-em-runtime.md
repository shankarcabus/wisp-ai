# Escolha de personagem em runtime — plano de implementação

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Uma escolha de personagem, feita no painel do Mac, que vale nas duas superfícies e chega na placa sem regravar.

**Architecture:** O `setter` de `Sprites.chosen` publica o nome em `~/.wisp/mascot`; o `config.py` lê e o `server.py` acrescenta a chave `mascot` ao `/state`; a placa chama `ui_personagem()` a cada payload, que destrói e reconstrói os objetos do personagem quando o nome muda, e grava na NVS só quando difere.

**Tech Stack:** Swift (`swiftc`, sem dependências), Python 3.9 do sistema, C11 com ESP-IDF 5.5 e LVGL 9.5.0.

**Spec:** `docs/superpowers/specs/2026-08-23-escolha-de-personagem-em-runtime-design.md`

## Global Constraints

- **O app do Mac continua sendo só `swiftc`, sem dependência nenhuma.** Embutir C é o projeto SEGUINTE; nada aqui pode encostar em `mac/build.sh`.
- `firmware/main/ui.c` e `firmware/main/main.c` mantêm **zero** `#if CONFIG_IDF_TARGET_*`. Personagem é dado.
- As duas placas continuam compilando: `esp32c6` e `esp32s3`.
- **Qualquer mutação de objeto do LVGL acontece com o mutex do display na mão.** Este firmware já registra o que o contrário produz: o watchdog pegou a task de rede presa dentro de `lv_inv_area()`, e na tela o sintoma foi a placa conectar, reportar uma vez e emudecer — parece problema de rede e não é.
- **Escrita na NVS só quando o valor difere do gravado.** A escolha muda quando uma pessoa decide mudar, não a cada poll.
- O arquivo é o nome seguido de `\n`. O leitor tira espaço em branco das duas pontas e aceita a ausência da quebra.
- Arquivo ausente → campo ausente → a placa **mantém** o personagem que tem. Instalação nova, bridge velho e placa gravada antes disto caem todos nesse caminho.
- `./sim/folha.sh` continua reproduzível: 27 capturas × 2 execuções, zero diferenças.
- **O risco central tem nome:** as interrogações do Terminal são três objetos compartilhados, criados uma vez e guardados por um `bool` estático. É o ponteiro pendurado que fez este projeto recusar troca em runtime na rodada anterior.

---

### Task 1: O canal — o painel publica, o bridge lê, o `/state` carrega

Metade em Swift, metade em Python, e testável inteira **sem a placa**.

**Files:**
- Modify: `mac/Sources/Sprites.swift` — o `setter` de `chosen`
- Modify: `bridge/config.py` — o leitor
- Modify: `bridge/server.py` — a chave no payload, junto de `snap["rest"]`
- Create: `bridge/test_mascot.py`

**Interfaces:**
- Produces:
  - `config.MASCOT_FILE` — `Path`, `~/.wisp/mascot`
  - `config.mascot() -> str` — o nome publicado, ou `""`
  - chave `"mascot"` no payload `/state`, presente só quando há nome

- [ ] **Step 1: Escrever o teste, que falha**

`bridge/test_mascot.py`:

```python
"""
O canal do personagem: o que o painel publica é o que o /state carrega.

Rode: python3 bridge/test_mascot.py

SEM FRAMEWORK, como o test_limits.py deste mesmo diretório — o projeto não tem
dependência de teste e não é para ganhar uma.

Estes testes NÃO tocam o ~/.wisp de verdade: apontam config.FOLDER e
config.MASCOT_FILE para um diretório temporário. Um teste que escreve na casa de
quem roda é um teste que ninguém roda duas vezes.
"""
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
    print(f"  {'ok  ' if ok else 'FALHA'}  {nome}  — obtive {obtido!r}")


def main():
    tmp = Path(tempfile.mkdtemp(prefix="wisp-mascot-"))
    config.FOLDER = tmp
    config.MASCOT_FILE = tmp / "mascot"

    print("config.mascot()")
    checa("sem arquivo, devolve vazio", config.mascot(), "")

    config.MASCOT_FILE.write_text("bytelo\n")
    checa("le o nome publicado", config.mascot(), "bytelo")

    config.MASCOT_FILE.write_text("  terminal  \n\n")
    checa("tira espaco das duas pontas", config.mascot(), "terminal")

    config.MASCOT_FILE.write_text("bytelo")
    checa("aceita sem a quebra de linha", config.mascot(), "bytelo")

    config.MASCOT_FILE.write_text("\n")
    checa("arquivo so com quebra e vazio", config.mascot(), "")

    print()
    print("all passed" if not falhas else f"{falhas} falha(s)")
    return 1 if falhas else 0


if __name__ == "__main__":
    sys.exit(main())
```

- [ ] **Step 2: Rodar e ver falhar**

```bash
python3 bridge/test_mascot.py
```

Esperado: `AttributeError: module 'config' has no attribute 'mascot'`.

- [ ] **Step 3: O leitor, em `bridge/config.py`**

Depois da definição de `FILE`, acrescentar:

```python
# A escolha de personagem publicada pelo painel do Mac.
#
# ARQUIVO PRÓPRIO, E NÃO UMA CHAVE NO config.json
# -----------------------------------------------
# Quem escreve é o app, em Swift. Para publicar um escalar no config.json ele
# teria de reimplementar o merge de defaults, a geração de token e o 0600 deste
# módulo — muito código para pouca coisa — e dois escritores no mesmo arquivo
# convidam a corrida. Uma linha, gravada por temp+rename do lado do app, e a
# leitura mora aqui porque a pasta é deste módulo.
MASCOT_FILE = FOLDER / "mascot"


def mascot() -> str:
    """
    O personagem que o painel publicou, ou "" quando ninguém escolheu.

    Vazio é um resultado legítimo e frequente: instalação nova, ou alguém que
    nunca abriu o seletor. Quem consome trata ausência como "não mande o campo",
    e a placa trata campo ausente como "mantenha o que tem".
    """
    try:
        return MASCOT_FILE.read_text().strip()
    except OSError:
        return ""
```

- [ ] **Step 4: Rodar e ver passar**

```bash
python3 bridge/test_mascot.py
```

Esperado: `all passed`.

- [ ] **Step 5: A chave no payload**

Em `bridge/server.py`, imediatamente depois de `snap["rest"] = REST_S`:

```python
        # O personagem escolhido no painel. Só vai quando há escolha: campo
        # ausente significa "placa, mantenha o que você tem", e é o caminho de
        # toda instalação nova.
        if (mc := config.mascot()):
            snap["mascot"] = mc
```

`config` já está importado (`bridge/server.py:40`).

- [ ] **Step 6: O painel publica**

Em `mac/Sources/Sprites.swift`, o `setter` de `chosen` passa de:

```swift
        set {
            UserDefaults.standard.set(newValue, forKey: "mascot")
            cache.removeAll()
            complete.removeAll()
        }
```

para:

```swift
        set {
            UserDefaults.standard.set(newValue, forKey: "mascot")
            cache.removeAll()
            complete.removeAll()
            publish(newValue)
        }
```

E, junto do resto do enum:

```swift
    /// O arquivo que o bridge lê para saber qual personagem a placa deve
    /// desenhar. UserDefaults continua sendo a loja do app; este arquivo é a
    /// escolha PUBLICADA.
    ///
    /// Um arquivo, e não o bridge lendo UserDefaults: `defaults read` do lado
    /// do Python passa pelo cfprefsd, e leitura defasada ali é o tipo de bug
    /// que custa uma tarde sem se anunciar. Isto se lê com `cat`.
    static var published = FileManager.default.homeDirectoryForCurrentUser
        .appendingPathComponent(".wisp/mascot")

    /// Grava por temp+rename: quem lê nunca vê meio nome. O bridge lê este
    /// arquivo a cada /state, e /state é servido a cada segundo.
    static func publish(_ name: String) {
        let dir = published.deletingLastPathComponent()
        let tmp = published.appendingPathExtension("tmp")
        do {
            try FileManager.default.createDirectory(at: dir, withIntermediateDirectories: true)
            try (name + "\n").write(to: tmp, atomically: false, encoding: .utf8)
            _ = try FileManager.default.replaceItemAt(published, withItemAt: tmp)
        } catch {
            // Falhar aqui não pode derrubar a troca de personagem no Mac: o
            // mascote da barra já mudou, e o que se perde é a placa acompanhar.
            NSLog("wisp: nao consegui publicar o personagem: \(error.localizedDescription)")
        }
    }
```

- [ ] **Step 7: Provar o lado Swift, com uma sonda compilada na hora**

```bash
mkdir -p /tmp/wisp-probe
cat > /tmp/wisp-probe/main.swift <<'EOF'
// Chama o MESMO setter que o Picker do painel chama.
Sprites.published = URL(fileURLWithPath: "/tmp/wisp-probe/mascot")
Sprites.chosen = "bytelo"
let lido = (try? String(contentsOf: Sprites.published, encoding: .utf8)) ?? "(nada)"
print("publicado: \(lido.debugDescription)")
print("conjuntos disponiveis: \(Sprites.available())")
EOF
swiftc -O -target arm64-apple-macosx14.0 -o /tmp/wisp-probe/probe \
    /tmp/wisp-probe/main.swift \
    mac/Sources/Sprites.swift mac/Sources/Model.swift mac/Sources/Mascot.swift
/tmp/wisp-probe/probe
```

Esperado: `publicado: "bytelo\n"`. A sonda aponta `published` para `/tmp` de propósito — um teste que escreve no `~/.wisp` de quem roda muda a configuração real da pessoa.

Ao fim, desfazer o que a sonda deixou no UserDefaults, porque ela escreveu de verdade:

```bash
defaults delete com.marciovicente.wisp mascot 2>/dev/null || true
```

- [ ] **Step 8: Provar o payload de ponta a ponta**

```bash
printf 'bytelo\n' > ~/.wisp/mascot
curl -s -H "X-Wisp-Token: $(python3 -c 'import sys; sys.path.insert(0,"bridge"); import config; print(config.read()["token"])')" \
     http://127.0.0.1:4666/state | python3 -m json.tool | grep -i mascot
```

Esperado: `"mascot": "bytelo"`. Se o bridge não estiver no ar, suba o Wisp.app antes — é ele quem roda o bridge.

- [ ] **Step 9: O app compila inteiro**

```bash
./mac/build.sh
```

Esperado: build sem erro. **Nenhuma dependência nova** — se este passo pedir cmake ou qualquer coisa, algo do projeto seguinte entrou aqui por engano.

- [ ] **Step 10: Commit**

```bash
git add mac/Sources/Sprites.swift bridge/config.py bridge/server.py bridge/test_mascot.py
git commit -m "Publish the panel's character choice where the bridge can read it"
```

---

### Task 2: `destruir`, e a troca no simulador como teste dela

A task perigosa. Ela entrega a capacidade de trocar de personagem com os objetos da tela sendo refeitos — e é onde o ponteiro pendurado mora.

**Files:**
- Modify: `firmware/main/mascote.h` — o ponteiro na vtable
- Modify: `firmware/main/mascote_terminal.c` — `terminal_destruir`
- Modify: `firmware/main/mascote_bytelo.c` — `bytelo_destruir`
- Modify: `firmware/main/ui.h` — `ui_personagem`
- Modify: `firmware/main/ui.c` — o corpo dela
- Modify: `sim/cena.c` — o comando `char`

**Interfaces:**
- Consumes: `personagem_t` (`criar`, `animar`, `dispor`, `nome`, `rotulos`), `mascote_por_nome()`, `mascote_escolher()`, `mascote_ativo()`.
- Produces:
  - `void (*destruir)(mascote_t *m);` na `personagem_t`
  - `void ui_personagem(const char *nome);` em `ui.h`
  - comando `char <nome>` no simulador

- [ ] **Step 1: O ponteiro na interface**

Em `firmware/main/mascote.h`, depois de `dispor`:

```c
    /* Desfaz o que criar() fez: apaga os objetos e libera o bloco `interno`.
     * Chamada com o mutex do LVGL JÁ na mão.
     *
     * Cuidado com o que NÃO morre por herança: objeto criado como IRMÃO da
     * raiz do personagem — a chama do Terminal, o adorno do Bytelo — tem de
     * ser apagado à mão, e é a mesma armadilha que os `dispor` dos dois já
     * documentam. */
    void (*destruir)(mascote_t *m);
```

- [ ] **Step 2: `terminal_destruir`**

Em `firmware/main/mascote_terminal.c`, antes da tabela `MASCOTE_TERMINAL`:

```c
/* Os objetos de nível superior. Tudo o que é filho deles morre por herança:
 * moldura, topo, tela, luz, scan, olho, pupila, brilho, sobrancelha e boca são
 * todos descendentes de `corpo`. */
static void terminal_destruir(mascote_t *m)
{
    /* A contagem vem ANTES da saída antecipada: um mascote cujo `criar` falhou
     * em alocar tem `interno` nulo e nada para apagar, mas ainda conta como um
     * dos que saíram de cena. Contar depois deixaria o contador travado e as
     * interrogações nunca seriam apagadas. */
    static int vivos = 0;
    if (vivos == 0) vivos = WISP_MAX_SESSIONS;
    const bool ultimo = (--vivos == 0);

    terminal_t *T = m->interno;
    if (T) {

    lv_obj_t *raizes[] = {T->corpo, T->braco[0], T->braco[1],
                          T->foto, T->chama, T->wisp};
    for (size_t k = 0; k < sizeof(raizes) / sizeof(raizes[0]); k++)
        if (raizes[k]) lv_obj_delete(raizes[k]);

        lv_free(T);
        m->interno = NULL;
    }

    /* AS INTERROGAÇÕES.
     *
     * São três objetos COMPARTILHADOS por todos os mascotes, criados uma vez e
     * guardados por um bool estático. Sem apagá-las e zerar o guarda, a próxima
     * entrada no Terminal usa ponteiros para memória liberada — e o sintoma
     * aparece longe da causa: lixo ou crash ao entrar em `asking`, muitas
     * trocas depois de a troca ter "funcionado".
     *
     * Só o último mascote a sair as apaga, porque só ele sabe que não há mais
     * ninguém usando. Contar é mais simples que descobrir. */
    if (ultimo) {
        for (int i = 0; i < QTD_INTERROG; i++) {
            if (g_interrog[i]) lv_obj_delete(g_interrog[i]);
            g_interrog[i] = NULL;
        }
        g_interrog_prontas = false;
    }
}
```

E a tabela ganha `.destruir = terminal_destruir,`.

- [ ] **Step 3: `bytelo_destruir`**

Em `firmware/main/mascote_bytelo.c`, antes da tabela `MASCOTE_BYTELO`:

```c
/* A raiz leva consigo corpo, relevo, olhos, cruzes, sobrancelhas e boca, que
 * são todos filhos dela. O adorno NÃO: ele é irmão, de propósito, porque filho
 * que sai da caixa do pai é recortado. Irmão não morre por herança. */
static void bytelo_destruir(mascote_t *m)
{
    bytelo_t *p = m->interno;
    if (!p) return;

    if (p->prop) lv_obj_delete(p->prop);
    if (p->raiz) lv_obj_delete(p->raiz);

    lv_free(p);
    m->interno = NULL;
}
```

E a tabela ganha `.destruir = bytelo_destruir,`.

- [ ] **Step 4: `ui_personagem` no cabeçalho**

Em `firmware/main/ui.h`, junto de `ui_swipe`:

```c
/* Troca o personagem desenhado, reconstruindo os objetos da tela.
 *
 * Toma o mutex do LVGL sozinha — chamável de qualquer task. Nome nulo, vazio,
 * desconhecido ou igual ao ativo não faz nada, e é por isso que quem chama pode
 * chamar a cada payload sem se perguntar se mudou: a pergunta é respondida aqui,
 * num lugar só. */
void ui_personagem(const char *nome);
```

- [ ] **Step 5: O corpo, em `ui.c`**

Depois de `ui_swipe`:

```c
void ui_personagem(const char *nome)
{
    if (!nome || !*nome) return;

    const personagem_t *novo = mascote_por_nome(nome);
    if (novo == mascote_ativo()) return;

    bsp_display_lock(-1);

    /* A ordem importa: destruir TODOS antes de trocar o ativo, porque é o
     * personagem antigo que sabe como desfazer o que ele fez. */
    for (int i = 0; i < WISP_MAX_SESSIONS; i++)
        if (mascote_ativo()->destruir) mascote_ativo()->destruir(&g_m[i]);

    mascote_escolher(novo);

    lv_obj_t *tela = g_telas[0];
    for (int i = 0; i < WISP_MAX_SESSIONS; i++)
        novo->criar(tela, &g_m[i]);

    /* Os rótulos NÃO são recriados: pertencem ao layout e sobreviveram à troca.
     * O que muda é aparecerem ou não, e disso quem cuida é aplicar_layout(),
     * pela propriedade `rotulos` do personagem.
     *
     * g_qtd = -1 é o idioma que este arquivo já usa para "reaplique o layout na
     * próxima atualização" — ver o bloco de repouso em ui_update(). */
    g_qtd = -1;

    bsp_display_unlock();
    ESP_LOGI(TAG, "personagem trocado para %s", novo->nome);
}
```

- [ ] **Step 6: O comando no simulador**

Em `sim/cena.c`, junto dos outros comandos:

```c
    } else if (!strcmp(cmd, "char")) {
        /* Só é seguro porque `destruir` passou a existir. Na implementação
         * anterior este comando foi recusado exatamente por isso: reconstruir a
         * tela deixava os objetos compartilhados do Terminal apontando para
         * memória liberada. */
        ui_personagem(arg);
        ui_update(&d);
        return;
```

E a ajuda passa a listar `char <terminal|bytelo>`.

- [ ] **Step 7: Compilar o simulador**

```bash
cmake --build sim/build -j
```

Esperado: sem erro e sem aviso.

- [ ] **Step 8: O teste da troca — dez idas e voltas**

```bash
{ for i in $(seq 1 10); do
    echo "char bytelo"; echo "todos asking"; echo "todos done"
    echo "char terminal"; echo "todos asking"; echo "todos done"
  done
  echo "char terminal"; echo "todos asking"; echo "shot /tmp/troca-terminal.bmp"
  echo "char bytelo";   echo "todos asking"; echo "shot /tmp/troca-bytelo.bmp"
  echo quit; } | ./sim/build/wisp-sim --headless 2>&1 | grep -E "^(E|W) |personagem trocado" | tail -25
sips -s format png /tmp/troca-terminal.bmp --out /tmp/troca-terminal.png >/dev/null
sips -s format png /tmp/troca-bytelo.bmp --out /tmp/troca-bytelo.png >/dev/null
open /tmp/troca-terminal.png /tmp/troca-bytelo.png
```

Esperado: vinte linhas de `personagem trocado`, **nenhuma** de erro, e as duas imagens corretas — o Terminal com o `?` das interrogações e os rótulos, o Bytelo com o `?` de adorno e sem rótulos. **A imagem do Terminal é o teste do ponteiro pendurado**: ela entra em `asking` depois de dez voltas pelo Bytelo, que é exatamente o caminho que usa as interrogações recriadas.

- [ ] **Step 9: As 64 transições depois de cada troca**

```bash
{ for c in bytelo terminal bytelo; do
    echo "char $c"
    for a in idle working tool asking waiting done error offline; do
      for b in idle working tool asking waiting done error offline; do
        echo "todos $a"; echo "todos $b"; done; done
  done
  echo "shot /tmp/troca-final.bmp"; echo quit; } \
  | ./sim/build/wisp-sim --headless 2>&1 | grep -cE "^E " | xargs -I{} echo "erros: {}"
```

Esperado: `erros: 0`. É na transição que o estado vazado entre casos aparece; numa captura isolada, nunca.

- [ ] **Step 10: O vazamento — heap antes e depois de dez trocas**

O spec lista vazamento por troca como risco, e cada `criar` aloca um bloco
`interno` que cada `destruir` tem de liberar. O simulador expõe o pool do LVGL,
que é de onde `lv_malloc_zeroed` tira.

Acrescentar ao `sim/cena.c` um comando de diagnóstico:

```c
    } else if (!strcmp(cmd, "heap")) {
        lv_mem_monitor_t mon;
        lv_mem_monitor(&mon);
        printf("I (sim) heap do LVGL: %u livres, %u usados, frag %u%%\n",
               (unsigned) mon.free_size, (unsigned) mon.total_size - (unsigned) mon.free_size,
               (unsigned) mon.frag_pct);
        return;
```

E medir:

```bash
{ echo "char bytelo"; echo "heap"
  for i in $(seq 1 10); do echo "char terminal"; echo "char bytelo"; done
  echo "heap"; echo quit; } | ./sim/build/wisp-sim --headless 2>&1 | grep "heap do LVGL"
```

Esperado: as duas linhas com o **mesmo** número de bytes usados, ou uma diferença
de poucas dezenas por fragmentação. Uma diferença que cresce com o número de
trocas é vazamento — e aí a conta é simples: dez trocas, e o excesso dividido por
vinte é o tamanho do bloco que ficou.

- [ ] **Step 11: A folha continua reproduzível**

```bash
./sim/folha.sh /tmp/f-a >/dev/null && ./sim/folha.sh /tmp/f-b >/dev/null
DIF=0; for f in /tmp/f-a/*.bmp; do cmp -s "$f" "/tmp/f-b/$(basename $f)" || DIF=$((DIF+1)); done
echo "diferencas: $DIF"
```

Esperado: `diferencas: 0`.

- [ ] **Step 12: As duas placas compilam**

```bash
export PATH=/usr/bin:/bin
source ~/esp/esp-idf/export.sh
cd firmware
for alvo in esp32c6 esp32s3; do
  rm -rf build sdkconfig
  idf.py set-target $alvo && idf.py build || echo "$alvo FALHOU"
done
rm -rf build sdkconfig && idf.py set-target esp32c6 && idf.py build
cd ..
git checkout -- firmware/dependencies.lock
```

O `rm -rf build sdkconfig` não é zelo: `set-target` recusa diretório de build do outro alvo, e o IDF não reaplica `sdkconfig.defaults` sobre um `sdkconfig` existente. E **não commite** o `dependencies.lock`: os builds o atualizam, e bumpar componente de carona num commit de feature é decisão de outra pessoa.

**Cuidado ao rodar isto:** `export PATH=/usr/bin:/bin` é necessário para o venv do IDF, e tira o `bat` do PATH — o perfil desta máquina tem `cat` aliasado para ele, então `$(cat <<'EOF')` na mesma chamada falha calado. Commite em chamada separada, com `git commit -F -`.

- [ ] **Step 13: Commit**

Em chamada separada da compilação, porque o `PATH` restrito do IDF quebra o `cat`:

```bash
git add firmware/main/mascote.h firmware/main/mascote_terminal.c \
        firmware/main/mascote_bytelo.c firmware/main/ui.h firmware/main/ui.c sim/cena.c
git commit -F - <<'MSG'
Let a character be torn down, so the board can switch without rebooting

The interface gains destroy(), and with it the simulator gets back the `char`
command it was refused last time — refused for precisely this reason, that
rebuilding the screen left the Terminal's shared question marks pointing at freed
memory. They are deleted by whichever mascot leaves last, counted rather than
discovered, and the count happens before the early return so a mascot whose
allocation failed still counts.

Bytelo's adornment is deleted by hand because it is a sibling of the face, not a
child. Siblings do not die by inheritance, which is the same price the Terminal's
flame pays.

Verified in the simulator: ten switches back and forth, then `asking` on the
Terminal — the path that uses the recreated question marks — plus all
sixty-four state transitions after each switch, and the LVGL pool back to the
same size.
MSG
```

---

### Task 3: A placa aplica o campo, e persiste

**Files:**
- Modify: `firmware/main/main.c`

**Interfaces:**
- Consumes: `ui_personagem()` da Task 2; `config.mascot()` → chave `"mascot"` do `/state`, da Task 1.
- Produces: nada que outra task consuma.

- [ ] **Step 1: Guardar o campo recebido**

Junto de `s_host` e `s_token` (`firmware/main/main.c:47-48`):

```c
static char s_mascote_rx[16] = {0};   /* personagem que o bridge mandou */
```

Em `interpretar()`, ao lado dos outros campos de topo:

```c
    /* Personagem. Campo AUSENTE não é "volte ao padrão": é "mantenha o que
     * você tem" — instalação nova e bridge velho caem aqui, e nenhum dos dois
     * deve derrubar a escolha que já está na NVS. Por isso a string só é
     * sobrescrita quando o campo existe. */
    const cJSON *mc = cJSON_GetObjectItemCaseSensitive(raiz, "mascot");
    if (cJSON_IsString(mc) && mc->valuestring && mc->valuestring[0])
        copiar_str(s_mascote_rx, sizeof(s_mascote_rx), raiz, "mascot");
```

- [ ] **Step 2: Aplicar e persistir**

Em `firmware/main/main.c`, no bloco de payload bem-sucedido (por volta da linha 970), a chamada entra **antes** do `ui_update`, para os objetos novos já receberem os dados:

```c
            if (interpretar(buf, &s_dados)) {
                /* Antes do ui_update: se o personagem trocou, são os objetos
                 * NOVOS que têm de receber estes dados. */
                aplicar_personagem(s_mascote_rx);

                /* Depois do interpretar: a bateria e medida aqui, nao vem do
                 * bridge, e nao pode ser sobrescrita pela resposta dele. */
                s_dados.battery_pct = s_bat_pct;
                s_dados.battery_charging = s_bat_carregando;
                ui_update(&s_dados);
            }
```

E a função, antes de `tarefa_rede`:

```c
/* Troca o personagem e guarda a escolha.
 *
 * Chamada a cada payload. Quem decide se há o que fazer é ui_personagem(), que
 * sai fora quando o nome é o ativo — concentrar a pergunta "mudou?" num lugar é
 * o que evita dois lugares discordando.
 *
 * A GRAVAÇÃO NA NVS só acontece quando o nome difere do gravado. Escrever a cada
 * poll desgastaria flash por nada, e o ganho é real: uma placa que reinicia com
 * o bridge fora do ar sobe no último personagem escolhido, e não no padrão. */
static void aplicar_personagem(const char *nome)
{
    if (!nome || !*nome) return;

    static char ultimo[16] = {0};
    if (strcmp(ultimo, nome) == 0) return;   /* nada mudou desde o último poll */

    ui_personagem(nome);

    nvs_handle_t h;
    if (nvs_open(NVS_NS, NVS_READWRITE, &h) == ESP_OK) {
        char gravado[16] = {0};
        size_t n = sizeof(gravado);
        if (nvs_get_str(h, "mascot", gravado, &n) != ESP_OK) gravado[0] = '\0';
        if (strcmp(gravado, nome) != 0) {
            if (nvs_set_str(h, "mascot", nome) == ESP_OK && nvs_commit(h) == ESP_OK)
                ESP_LOGI(TAG, "personagem \"%s\" gravado na NVS", nome);
            else
                ESP_LOGW(TAG, "nao consegui gravar o personagem na NVS");
        }
        nvs_close(h);
    }
    snprintf(ultimo, sizeof(ultimo), "%s", nome);
}
```

- [ ] **Step 3: As duas placas compilam**

```bash
export PATH=/usr/bin:/bin
source ~/esp/esp-idf/export.sh
cd firmware
for alvo in esp32c6 esp32s3; do
  rm -rf build sdkconfig
  idf.py set-target $alvo && idf.py build || echo "$alvo FALHOU"
done
rm -rf build sdkconfig && idf.py set-target esp32c6 && idf.py build
cd .. && git checkout -- firmware/dependencies.lock
```

- [ ] **Step 4: A regra do `CLAUDE.md` continua valendo**

```bash
grep -n "CONFIG_IDF_TARGET" firmware/main/ui.c firmware/main/main.c \
    firmware/main/mascote*.c firmware/main/mascote.h
```

Esperado: nenhuma saída.

- [ ] **Step 5: Commit**

```bash
git add firmware/main/main.c
git commit -F - <<'MSG'
Apply the character the bridge sends, and remember it

ui_personagem() is called on every payload and decides for itself whether there
is anything to do; asking "did it change?" in two places is how two places come
to disagree.

An absent field is not "go back to the default" — it is "keep what you have".
A fresh install and an older bridge both arrive that way, and neither should
knock out a choice already in NVS.

NVS is written only when the name differs from what is stored. The gain is real:
a board that reboots with the bridge down comes up on the last character chosen
rather than the factory one, which makes the provisioning question an initial
value instead of the only way to choose.
MSG
```

---

### Task 4: Ponta a ponta na placa

O teste de aceitação, e ele não admite interpretação.

**Files:**
- Modify: `CLAUDE.md` — a tabela "Where things are" e as expectativas de verificação
- Modify: `firmware/README.md` — a seção de provisionamento
- Modify: `sim/README.md` — o comando `char`

- [ ] **Step 1: Gravar**

**Isto sobrescreve o que está na placa.** Sai o firmware atual do Wisp, entra este; e o `flash.sh` só grava a NVS se rodar num terminal de verdade, porque o prompt da senha é oculto e precisa de TTY. Para não mexer na NVS, gravar só o app:

```bash
export PATH=/usr/bin:/bin
source ~/esp/esp-idf/export.sh
idf.py -C firmware -p /dev/cu.usbmodem101 app-flash
```

Se o esptool recusar: desconecte e reconecte o cabo USB-C. Esta placa **não tem botão de reset** — o cabo é o reset — e não force DTR/RTS, que deixa a porta muda.

- [ ] **Step 2: O teste que decide**

Com o Wisp.app rodando, abrir o painel e mudar o Picker "Character" de `terminal` para `bytelo`.

Esperado: **o mascote da placa troca, sem regravar.** No serial, `personagem trocado para bytelo` e `personagem "bytelo" gravado na NVS`. Ler com:

```bash
~/.espressif/python_env/idf5.5_py3.9_env/bin/python -c "
import glob, time, serial
s = serial.Serial(glob.glob('/dev/cu.usbmodem*')[0], 115200, timeout=1)
fim = time.time() + 90
while time.time() < fim:
    b = s.readline()
    if b:
        t = b.decode('utf-8','replace').rstrip()
        if any(k in t for k in ('personagem','mascot','E (','wdt','Guru')): print(t)
s.close()"
```

- [ ] **Step 3: A persistência**

Fechar o Wisp.app (o bridge morre com ele), desconectar e reconectar o cabo da placa, e ler o serial no boot.

Esperado: `personagem: bytelo` — a placa subiu no último escolhido, sem o bridge no ar. É isso que a gravação na NVS compra.

- [ ] **Step 4: Voltar, e provar que volta**

Reabrir o app, mudar o Picker para `terminal`.

Esperado: a placa volta ao computador retrô, com os rótulos de detalhe e projeto de volta — porque a propriedade `rotulos` muda com o personagem e o `aplicar_layout()` a respeita.

- [ ] **Step 5: Escrever o que ficou verificado**

Em `CLAUDE.md`, acrescentar à tabela "Where things are" a linha do canal:

```
| `~/.wisp/mascot` + `bridge/config.py` | the character the panel published, and the only channel from the app to the bridge |
```

E, nas expectativas de verificação, dizer que a troca em runtime está verificada em hardware, com a data — incluindo o reboot sem o bridge.

Em `firmware/README.md`, na seção de provisionamento, dizer que a pergunta do personagem é o **valor inicial**: depois disso quem manda é o painel, e trocar não exige reprovisionar.

Em `sim/README.md`, acrescentar `char <nome>` à tabela de comandos, com a nota de que ele existe porque `destruir` existe.

- [ ] **Step 6: Commit**

```bash
git add CLAUDE.md firmware/README.md sim/README.md
git commit -F - <<'MSG'
Record that the character now changes without reflashing

Co-Authored-By: Claude Opus 5 (1M context) <noreply@anthropic.com>
MSG
```

---

## Notas de execução

**A ordem tem uma dependência real:** a Task 3 não tem como ser testada sem a Task 1 (não há campo no payload) nem sem a Task 2 (não há `ui_personagem`). As Tasks 1 e 2 são independentes entre si e testáveis isoladamente.

**Onde parar e pensar.** Se o Step 8 da Task 2 mostrar lixo ou crash na imagem do Terminal, é o ponteiro das interrogações e a contagem de `vivos` está errada — não seguir ajustando outras coisas. Se a folha do Step 10 divergir, alguma coisa na troca deixou estado global sujo, e isso é pior que um bug visual: quebra a rede de proteção de todo o resto.

**O que não fazer em nenhuma task.** Encostar em `mac/build.sh` ou acrescentar dependência ao app — isso é o projeto seguinte. Mexer em `vaga_de()`. Acrescentar `#if CONFIG_IDF_TARGET_*`. Commitar `firmware/dependencies.lock`. Gravar na NVS sem comparar com o que já está lá.
