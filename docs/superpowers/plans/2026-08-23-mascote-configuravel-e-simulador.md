# Mascote configurável e simulador de tela — plano de implementação

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Um segundo personagem em pixel art na placa, escolhido por dado e sem apagar o atual, mais um simulador no Mac que roda o `ui.c` real para iterar a tela sem regravar.

**Architecture:** O simulador compila `firmware/main/ui.c` **sem modificação**, substituindo os quatro headers de ESP-IDF que ele inclui por shims de mesmo nome em `sim/shim/`. Com o simulador de pé, a interface de personagem é extraída de `ui.c` sob rede de proteção de imagem-por-imagem, e só então o personagem novo é escrito: cara em objetos LVGL, adornos como `lv_image_dsc_t` gerados em tempo de build a partir de mapas ASCII.

**Tech Stack:** C11, LVGL 9.5.0, SDL2 (host), CMake (host), ESP-IDF 5.5+ (placa), Python 3 (gerador de props), `sips` do macOS (BMP→PNG).

**Spec:** `docs/superpowers/specs/2026-08-23-mascote-configuravel-e-simulador-design.md`

## Global Constraints

- `firmware/main/ui.c` e `firmware/main/main.c` contêm **zero** `#if CONFIG_IDF_TARGET_*`. Diferença de placa vai em `firmware/main/board.h`; diferença de estilo não existe. Personagem é **dado**, nunca condicional de compilação.
- Ambas as placas continuam compilando: `esp32c6` e `esp32s3`.
- **Nenhuma decodificação de imagem em tempo de execução.** A conversão acontece no build. Medido no projeto: com decodificação de PNG em runtime o FPS caiu de 62 para 1–7 e a RAM interna chegou a **12 bytes** de mínimo histórico.
- Tamanhos do mascote na placa: **306px** (1 sessão), **178px** (2), **140px** (3 e 4), de `vaga_de()` em `firmware/main/ui.c:388`. **Não alterar.** Os 306 são exatamente o tamanho da arte, e imagem maior que o objeto sai cortada, não reduzida.
- Props nunca são transformados por quadro — só na troca de estado.
- O C6 não tem PSRAM. RAM interna é o recurso mais escasso da placa.
- Depois da extração, o Terminal tem de sair **byte a byte igual** na tela. A Task 3 existe para tornar isso verificável.
- LVGL é 9.5.0, vindo de `firmware/managed_components/lvgl__lvgl/` — que é **gitignored**. O simulador aceita a cópia local quando ela existe e busca a v9.5.0 quando não.

---

### Task 1: Simulador — janela, shim e o `ui.c` real

O ponto central: `firmware/main/ui.c` inclui `esp_log.h`, `bsp/esp-bsp.h`, `esp_mmap_assets.h` e `esp_lv_decoder.h`. Colocando `sim/shim/` antes na ordem de include, os quatro resolvem para os nossos, e o arquivo compila **sem uma linha de diferença**. Nenhum `#ifdef` entra no firmware.

**Files:**
- Create: `sim/CMakeLists.txt`
- Create: `sim/lv_conf.h`
- Create: `sim/main.c`
- Create: `sim/shim/esp_err.h`
- Create: `sim/shim/esp_log.h`
- Create: `sim/shim/bsp/esp-bsp.h`
- Create: `sim/shim/esp_mmap_assets.h`
- Create: `sim/shim/esp_mmap_assets.c`
- Create: `sim/shim/esp_lv_decoder.h`
- Create: `sim/shim/esp_lv_decoder.c`
- Create: `sim/README.md`
- Create: `sim/.gitignore`
- Modify: nada em `firmware/`

**Interfaces:**
- Consumes: `firmware/main/ui.h` (`ui_create`, `ui_update`, `ui_swipe`, `ui_state_from_text`, `wisp_data_t`), compilado como está.
- Produces: binário `sim/build/wisp-sim`, que abre uma janela de 480×480 com a tela do tile 0. `bool bsp_display_lock(uint32_t)` e `void bsp_display_unlock(void)` implementados em `sim/main.c` sobre um mutex recursivo.

- [ ] **Step 1: Instalar o SDL2**

```bash
brew install sdl2
sdl2-config --version    # esperado: 2.x
```

- [ ] **Step 2: Escrever os shims triviais**

`sim/shim/esp_err.h`:

```c
/* Só o que ui.c usa do esp_err.h do IDF. */
#pragma once

typedef int esp_err_t;

#define ESP_OK    0
#define ESP_FAIL -1
```

`sim/shim/esp_log.h`:

```c
/* Os três macros de log que ui.c usa, em cima de printf.
 *
 * O formato imita o do IDF (letra, tag, mensagem) porque o objetivo é poder
 * comparar a saída do simulador com a saída do monitor serial da placa lado a
 * lado — inclusive a linha de FPS. */
#pragma once

#include <stdio.h>
#include "esp_err.h"

#define ESP_LOGI(tag, fmt, ...) printf("I (%s) " fmt "\n", tag, ##__VA_ARGS__)
#define ESP_LOGW(tag, fmt, ...) printf("W (%s) " fmt "\n", tag, ##__VA_ARGS__)
#define ESP_LOGE(tag, fmt, ...) printf("E (%s) " fmt "\n", tag, ##__VA_ARGS__)
```

`sim/shim/bsp/esp-bsp.h`:

```c
/* Dos nove símbolos do BSP, ui.c usa dois. A assinatura é copiada de
 * firmware/components/bsp_c6_amoled_216/include/bsp/esp-bsp.h:61-62 para que a
 * chamada bsp_display_lock(-1) compile igual nos dois lados. */
#pragma once

#include <stdbool.h>
#include <stdint.h>

bool bsp_display_lock(uint32_t timeout_ms);
void bsp_display_unlock(void);
```

`sim/shim/esp_lv_decoder.h`:

```c
/* Na placa, o decoder existe para registrar o formato RAW no LVGL. Os assets
 * já chegam convertidos, então no host não há nada para decodificar: o init
 * apenas sucede, e ui.c segue pelo caminho de imagem. */
#pragma once

#include "esp_err.h"

typedef struct esp_lv_decoder_t *esp_lv_decoder_handle_t;

esp_err_t esp_lv_decoder_init(esp_lv_decoder_handle_t *handle);
```

`sim/shim/esp_lv_decoder.c`:

```c
#include <stddef.h>          /* NULL */

#include "esp_lv_decoder.h"

esp_err_t esp_lv_decoder_init(esp_lv_decoder_handle_t *handle)
{
    if (handle) *handle = NULL;
    return ESP_OK;
}
```

- [ ] **Step 3: Escrever o shim de assets**

`sim/shim/esp_mmap_assets.h`:

```c
/* A parte do esp_mmap_assets que ui.c usa: abrir, pegar ponteiro por índice e
 * pegar tamanho por índice. */
#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

typedef struct mmap_assets_t *mmap_assets_handle_t;

typedef struct {
    const char *partition_label;
    int         max_files;
    uint32_t    checksum;
    struct { bool mmap_enable; } flags;
} mmap_assets_config_t;

esp_err_t      mmap_assets_new(const mmap_assets_config_t *config,
                               mmap_assets_handle_t *handle);
const uint8_t *mmap_assets_get_mem(mmap_assets_handle_t handle, int index);
int            mmap_assets_get_size(mmap_assets_handle_t handle, int index);
```

`sim/shim/esp_mmap_assets.c`:

```c
/* Lê os .bin que o build do firmware já produziu.
 *
 * POR QUE OS .bin INDIVIDUAIS, E NÃO O storage.bin EMPACOTADO
 * -----------------------------------------------------------
 * O storage.bin é o container do esp_mmap_assets, com um cabeçalho próprio
 * que só aquele componente sabe ler. Os arquivos individuais em
 * mmap_build/assets/storage/ são a saída direta do LVGLImage.py: 12 bytes de
 * lv_image_header_t seguidos dos planos de cor e alfa. Esse formato está
 * documentado no próprio ui.c, e ler dele significa que o simulador exercita
 * o MESMO parsing de cabeçalho que a placa.
 *
 * OS DOIS BYTES
 * -------------
 * Na placa, mmap_assets_get_mem() já pulou os 2 bytes de magic do empacotador
 * e mmap_assets_get_size() ainda os conta — ui.c compensa com `- 2`. Aqui não
 * existe empacotador, então devolvemos o arquivo inteiro em get_mem e
 * `tamanho + 2` em get_size. É o que faz a subtração de ui.c chegar no número
 * certo sem que ui.c saiba onde está rodando.
 *
 * ORDEM ALFABÉTICA
 * ----------------
 * A tabela IDX[] de ui.c mapeia estado -> índice contando com a ordem
 * alfabética dos arquivos na partição. scandir() com alphasort() reproduz
 * exatamente isso.
 *
 * SE NÃO HOUVER BUILD
 * -------------------
 * mmap_build/ é gerado por `idf.py build` e é gitignored. Sem ele, esta função
 * devolve ESP_FAIL, ui.c avisa "particao de mascotes nao abriu — segue no
 * vetor" e cai no mascote vetorial. É o mesmo caminho de degradação da placa
 * quando o mmap falha, e é justamente o que queremos poder ver. */
#include "esp_mmap_assets.h"

#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define ASSETS_MAX 16
#define PASTA "firmware/build/mmap_build/assets/storage"

struct mmap_assets_t {
    uint8_t *dados[ASSETS_MAX];
    long     tam[ASSETS_MAX];
    int      qtd;
};

static int e_asset(const struct dirent *d)
{
    const char *p = strrchr(d->d_name, '.');
    if (!p || strcmp(p, ".bin") != 0) return 0;
    /* O container empacotado tem o nome da partição e não é um asset. */
    return strcmp(d->d_name, "storage.bin") != 0;
}

esp_err_t mmap_assets_new(const mmap_assets_config_t *config,
                          mmap_assets_handle_t *handle)
{
    if (!config || !handle) return ESP_FAIL;

    struct dirent **lista = NULL;
    int n = scandir(PASTA, &lista, e_asset, alphasort);
    if (n <= 0) {
        printf("W (sim) %s: sem assets convertidos — rode `idf.py build` "
               "em firmware/ se quiser ver o mascote de imagem\n", PASTA);
        return ESP_FAIL;
    }

    struct mmap_assets_t *a = calloc(1, sizeof(*a));
    if (!a) { free(lista); return ESP_FAIL; }

    for (int i = 0; i < n && i < ASSETS_MAX; i++) {
        char caminho[512];
        snprintf(caminho, sizeof(caminho), "%s/%s", PASTA, lista[i]->d_name);
        FILE *f = fopen(caminho, "rb");
        if (!f) continue;
        fseek(f, 0, SEEK_END);
        long tam = ftell(f);
        fseek(f, 0, SEEK_SET);
        a->dados[a->qtd] = malloc((size_t) tam);
        if (a->dados[a->qtd] && fread(a->dados[a->qtd], 1, (size_t) tam, f) == (size_t) tam) {
            a->tam[a->qtd] = tam;
            a->qtd++;
        }
        fclose(f);
    }
    for (int i = 0; i < n; i++) free(lista[i]);
    free(lista);

    printf("I (sim) %d assets lidos de %s\n", a->qtd, PASTA);
    *handle = a;
    return a->qtd > 0 ? ESP_OK : ESP_FAIL;
}

const uint8_t *mmap_assets_get_mem(mmap_assets_handle_t handle, int index)
{
    if (!handle || index < 0 || index >= handle->qtd) return NULL;
    return handle->dados[index];
}

int mmap_assets_get_size(mmap_assets_handle_t handle, int index)
{
    if (!handle || index < 0 || index >= handle->qtd) return 0;
    /* + 2: ver a nota sobre os dois bytes no topo deste arquivo. */
    return (int) handle->tam[index] + 2;
}
```

- [ ] **Step 4: Escrever o `lv_conf.h` do host**

Partir do template do LVGL e mudar exatamente estes valores. Os das fontes vêm de `firmware/sdkconfig.defaults:52-58`; os de cor e formato existem para o host bater com a placa, que é RGB565 com suporte a RGB565A8.

```bash
cp firmware/managed_components/lvgl__lvgl/lv_conf_template.h sim/lv_conf.h
```

Depois editar em `sim/lv_conf.h`:

| linha a mudar | de | para |
|---|---|---|
| `#if 0` no topo (guarda do template) | `#if 0` | `#if 1` |
| `LV_COLOR_DEPTH` | `16` | `16` (confirmar) |
| `LV_DRAW_SW_SUPPORT_RGB565A8` | `1` | `1` (confirmar) |
| `LV_USE_SDL` | `0` | `1` |
| `LV_FONT_MONTSERRAT_16` | `0` | `1` |
| `LV_FONT_MONTSERRAT_20` | `0` | `1` |
| `LV_FONT_MONTSERRAT_24` | `0` | `1` |
| `LV_FONT_MONTSERRAT_28` | `0` | `1` |
| `LV_FONT_MONTSERRAT_32` | `0` | `1` |
| `LV_FONT_MONTSERRAT_38` | `0` | `1` |
| `LV_FONT_MONTSERRAT_48` | `0` | `1` |
| `LV_USE_TILEVIEW` | `1` | `1` (confirmar) |
| `LV_DEF_REFR_PERIOD` | `33` | `15` |

O `LV_DEF_REFR_PERIOD` importa: `ui.c:127` documenta que o timer de animação usa 16ms para acompanhar `LV_DEF_REFR_PERIOD=15`, e com 33 a luz só se move em metade dos quadros. Com o valor errado aqui o simulador mostraria uma animação travada que na placa é fluida.

- [ ] **Step 5: Escrever o `main.c` do simulador**

```c
/* Simulador da tela do Wisp.
 *
 * Roda firmware/main/ui.c sem modificação: os headers de ESP-IDF que ele
 * inclui resolvem para sim/shim/, que vem antes na ordem de include.
 *
 * NÃO PROVA CUSTO DE RENDER. O Mac tem CPU e RAM de sobra e nenhuma das
 * restrições da placa existe aqui. Isto serve para ver layout, expressão e
 * composição; número de FPS e RAM se medem na placa. */
#include <pthread.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#include <SDL2/SDL.h>

#include "lvgl.h"
#include "ui.h"

/* Recursivo de propósito: na placa este lock é um semáforo do BSP que o
 * firmware chama de vários pontos, e um mutex simples transformaria um
 * aninhamento inofensivo em travamento silencioso. */
static pthread_mutex_t g_lvgl;

bool bsp_display_lock(uint32_t timeout_ms)
{
    (void) timeout_ms;   /* a placa recebe -1 = para sempre */
    return pthread_mutex_lock(&g_lvgl) == 0;
}

void bsp_display_unlock(void)
{
    pthread_mutex_unlock(&g_lvgl);
}

int main(void)
{
    /* Linha a linha, sempre. Com stdout redirecionado para arquivo ou pipe o
     * libc passa a bufferizar por bloco, e aí o log — que existe para ser
     * comparado com o monitor serial da placa — só aparece quando o processo
     * morre. O simulador não morre: o laço é infinito de propósito. */
    setvbuf(stdout, NULL, _IOLBF, 0);

    pthread_mutexattr_t attr;
    pthread_mutexattr_init(&attr);
    pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_RECURSIVE);
    pthread_mutex_init(&g_lvgl, &attr);

    lv_init();
    lv_tick_set_cb(SDL_GetTicks);
    lv_delay_set_cb(SDL_Delay);

    lv_display_t *disp = lv_sdl_window_create(480, 480);
    lv_sdl_window_set_title(disp, "Wisp — simulador da placa");
    lv_sdl_mouse_create();

    /* ui.h: ui_create() precisa ser chamada com o mutex do LVGL na mão. */
    bsp_display_lock(0);
    ui_create();
    bsp_display_unlock();

    printf("I (sim) janela aberta; ctrl-c para sair\n");

    for (;;) {
        bsp_display_lock(0);
        uint32_t espera = lv_timer_handler();
        bsp_display_unlock();
        if (espera < 1)  espera = 1;
        if (espera > 20) espera = 20;
        SDL_Delay(espera);
    }
    return 0;
}
```

- [ ] **Step 6: Escrever o CMake do simulador**

```cmake
cmake_minimum_required(VERSION 3.16)
project(wisp-sim C)

set(CMAKE_C_STANDARD 11)

# LVGL. Preferimos a cópia que o build do firmware já baixou, porque aí o
# simulador e a placa rodam exatamente a mesma versão. Ela é gitignored, então
# num clone limpo caímos na v9.5.0 do upstream — mesma versão, origem
# diferente. Se este `if` cair no `else`, a paridade é por número de versão e
# não por bytes; está anotado porque importa saber qual dos dois se está vendo.
set(LVGL_LOCAL "${CMAKE_CURRENT_SOURCE_DIR}/../firmware/managed_components/lvgl__lvgl")
if(EXISTS "${LVGL_LOCAL}/lvgl.h")
    message(STATUS "LVGL: cópia local do firmware (paridade exata)")
    add_subdirectory("${LVGL_LOCAL}" lvgl EXCLUDE_FROM_ALL)
else()
    message(STATUS "LVGL: buscando v9.5.0 do upstream")
    include(FetchContent)
    FetchContent_Declare(lvgl
        GIT_REPOSITORY https://github.com/lvgl/lvgl.git
        GIT_TAG v9.5.0)
    FetchContent_MakeAvailable(lvgl)
endif()

target_compile_definitions(lvgl PUBLIC
    LV_CONF_PATH="${CMAKE_CURRENT_SOURCE_DIR}/lv_conf.h")

find_package(SDL2 REQUIRED)

# O driver SDL do LVGL também precisa dos headers e da lib.
target_include_directories(lvgl PUBLIC ${SDL2_INCLUDE_DIRS})
target_link_libraries(lvgl PUBLIC ${SDL2_LIBRARIES})

add_executable(wisp-sim
    main.c
    shim/esp_mmap_assets.c
    shim/esp_lv_decoder.c
    ../firmware/main/ui.c          # sem modificação nenhuma
)

# A ORDEM IMPORTA. shim/ vem primeiro para que esp_log.h, bsp/esp-bsp.h,
# esp_mmap_assets.h e esp_lv_decoder.h resolvam para os nossos. É isso que
# permite compilar o ui.c do firmware sem um único #ifdef.
target_include_directories(wisp-sim PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/shim
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/../firmware/main
    ${SDL2_INCLUDE_DIRS}
)

target_link_libraries(wisp-sim PRIVATE lvgl ${SDL2_LIBRARIES})
```

- [ ] **Step 7: Compilar**

```bash
cmake -S sim -B sim/build && cmake --build sim/build -j
```

Esperado: `wisp-sim` gerado, e na configuração a linha `LVGL: cópia local do firmware (paridade exata)`. Zero aviso vindo de `ui.c` — se aparecer, é sinal de que o shim divergiu de uma assinatura do IDF, e a correção é no shim, nunca em `ui.c`.

- [ ] **Step 8: Rodar**

O binário precisa rodar **da raiz do repositório**, porque o shim de assets usa o caminho relativo `firmware/build/mmap_build/assets/storage`:

```bash
./sim/build/wisp-sim
```

Esperado: janela de 480×480, fundo preto, um mascote no centro em estado `idle`, e no terminal a cada 5 segundos a linha `I (ui) FPS: <n>  (1 sessao/oes)`. Se o firmware já foi compilado localmente, `I (sim) 8 assets lidos de …` e o mascote é a arte do computador; se não, o aviso de assets e o mascote vetorial animado. **Os dois são resultados corretos** — o segundo é o caminho de degradação real da placa.

- [ ] **Step 9: Escrever o `sim/README.md` e o `sim/.gitignore`**

`sim/.gitignore`:

```
build/
shots/
```

`sim/README.md` cobre, em prosa curta: o que o simulador é e o que ele **não** é (não prova custo de render), o `brew install sdl2`, os dois comandos acima, a exigência de rodar da raiz, e a explicação de que os quatro headers em `shim/` existem para que `ui.c` compile sem modificação — com o aviso de que qualquer include novo de ESP-IDF em `ui.c` quebra o simulador e a correção é acrescentar o shim.

- [ ] **Step 10: Commit**

```bash
git add sim/
git commit -m "Run the board's screen on the Mac, without touching the firmware"
```

---

### Task 2: Alimentação sintética e comandos por stdin

Sem isto o simulador mostra um estado só. O controle é por linha de comando em stdin, não por tecla: o driver de teclado do LVGL consome os eventos do SDL para dentro do sistema de grupos, e disputar `SDL_PollEvent` com ele daria um bug difícil de ver. Stdin é trivial, e de brinde vira roteirizável — o que a Task 3 aproveita.

**Files:**
- Create: `sim/cena.h`
- Create: `sim/cena.c`
- Modify: `sim/main.c`
- Modify: `sim/CMakeLists.txt`

**Interfaces:**
- Consumes: `wisp_data_t` e `ui_update()` de `firmware/main/ui.h`; `ui_state_from_text()` para traduzir nomes de estado.
- Produces:
  - `void cena_init(void)` — zera a cena e aplica os padrões.
  - `void cena_comando(const char *linha)` — aplica um comando; desconhecido imprime a ajuda.
  - `const wisp_data_t *cena_atual(void)` — a cena montada, para passar a `ui_update()`.
  - `void cena_ajuda(void)` — imprime a lista de comandos.

- [ ] **Step 1: Escrever `sim/cena.h`**

```c
/* Monta um wisp_data_t sintético a partir de comandos de texto.
 *
 * Os dados são inventados de propósito, pelo mesmo motivo que mac/Shots faz
 * isso: uma captura com dados reais carrega nomes de projeto e números de uso
 * de quem gerou, e não se pode publicar. */
#pragma once

#include "ui.h"

void                  cena_init(void);
void                  cena_comando(const char *linha);
const wisp_data_t    *cena_atual(void);
void                  cena_ajuda(void);
```

- [ ] **Step 2: Escrever `sim/cena.c`**

Os comandos, um por linha:

| comando | efeito |
|---|---|
| `n <1-4>` | número de sessões em cena |
| `s <estado> [i]` | estado da sessão `i` (padrão 0). Nomes: `idle working tool asking waiting done error offline` |
| `todos <estado>` | o mesmo estado em todas as sessões em cena |
| `rest` / `wake` | entra e sai do modo repouso (mexe em `age_s` contra `rest_s`) |
| `tile <0\|1>` | desliza para a tela de mascotes ou de limites |
| `bat <0-100>` / `bat -1` | bateria; -1 = desconhecida |
| `lim` / `nolim` | limites presentes ou indisponíveis |
| `?` | ajuda |

```c
#include "cena.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static wisp_data_t d;

/* Nomes de projeto e detalhes fixos, escolhidos para exercitar a largura do
 * texto: um curto, um longo, um com número. É neles que o layout de 4 sessões
 * costuma estourar. */
static const char *PROJ[WISP_MAX_SESSIONS] = {"wisp-ai", "clawdmeter", "esp32-c6", "brain"};
static const char *DET[WISP_MAX_SESSIONS]  = {"Bash", "approve plan", "Deploy +1", "Read"};

void cena_init(void)
{
    memset(&d, 0, sizeof(d));
    d.session_count = 1;
    for (int i = 0; i < WISP_MAX_SESSIONS; i++) {
        d.sessions[i].state = WISP_IDLE;
        snprintf(d.sessions[i].project, sizeof(d.sessions[i].project), "%s", PROJ[i]);
        snprintf(d.sessions[i].detail,  sizeof(d.sessions[i].detail),  "%s", DET[i]);
        snprintf(d.sessions[i].model,   sizeof(d.sessions[i].model),   "opus");
        d.sessions[i].age_s = 3;
    }
    d.battery_pct = 72;
    d.battery_charging = false;

    d.limit_count = 3;
    const char *rot[3] = {"5h window", "7 day", "opus 7 day"};
    const int   pct[3] = {41, 88, 12};
    const int   rit[3] = {55, 70, 30};
    for (int i = 0; i < 3; i++) {
        snprintf(d.limits[i].label, sizeof(d.limits[i].label), "%s", rot[i]);
        d.limits[i].pct = pct[i];
        d.limits[i].elapsed_pct = rit[i];
        snprintf(d.limits[i].resets_in, sizeof(d.limits[i].resets_in), "3h");
        snprintf(d.limits[i].severity, sizeof(d.limits[i].severity), "ok");
        d.limits[i].active = (i == 0);
    }
    d.limits_age_s = 40;

    snprintf(d.clock, sizeof(d.clock), "18:08");
    snprintf(d.day, sizeof(d.day), "Sun 23 Aug");
    d.rest_s = 300;
    d.age_s = 3;          /* acordado */
    d.has_weather = true;
    d.temp = 21; d.temp_max = 25; d.temp_min = 16;
    snprintf(d.condition, sizeof(d.condition), "partly cloudy");
    snprintf(d.icon, sizeof(d.icon), "cloudsun");
}

void cena_ajuda(void)
{
    printf("comandos: n <1-4> | s <estado> [i] | todos <estado> | rest | wake\n"
           "          tile <0|1> | bat <pct|-1> | lim | nolim | ?\n"
           "estados : idle working tool asking waiting done error offline\n");
}

const wisp_data_t *cena_atual(void) { return &d; }

void cena_comando(const char *linha)
{
    char cmd[32] = "", arg[32] = "";
    int  i = 0;
    if (sscanf(linha, "%31s %31s %d", cmd, arg, &i) < 1) return;

    if (!strcmp(cmd, "n")) {
        int q = atoi(arg);
        d.session_count = q < 1 ? 1 : (q > WISP_MAX_SESSIONS ? WISP_MAX_SESSIONS : q);
    } else if (!strcmp(cmd, "s")) {
        if (i < 0 || i >= WISP_MAX_SESSIONS) i = 0;
        d.sessions[i].state = ui_state_from_text(arg);
    } else if (!strcmp(cmd, "todos")) {
        wisp_state_t e = ui_state_from_text(arg);
        for (int k = 0; k < WISP_MAX_SESSIONS; k++) d.sessions[k].state = e;
    } else if (!strcmp(cmd, "rest")) {
        d.age_s = d.rest_s + 60;
    } else if (!strcmp(cmd, "wake")) {
        d.age_s = 3;
    } else if (!strcmp(cmd, "tile")) {
        ui_swipe(atoi(arg) == 1 ? +1 : -1);
        return;                       /* ui_swipe já desenha */
    } else if (!strcmp(cmd, "bat")) {
        d.battery_pct = atoi(arg);
    } else if (!strcmp(cmd, "lim")) {
        d.limit_count = 3; d.limits_age_s = 40;
    } else if (!strcmp(cmd, "nolim")) {
        d.limit_count = 0; d.limits_age_s = -1;
    } else {
        cena_ajuda();
        return;
    }
    ui_update(&d);
}
```

- [ ] **Step 3: Ligar stdin no laço do `main.c`**

Acrescentar ao topo os includes `<poll.h>`, `<unistd.h>` e `"cena.h"`; depois do `ui_create()`, chamar `cena_init()`, `cena_ajuda()` e `ui_update(cena_atual())` — este último **sem** o lock na mão, porque `ui_update()` pega o mutex sozinho (`ui.h`). Dentro do laço, antes do `SDL_Delay`:

```c
        /* stdin sem bloquear: se houver linha, aplica. */
        struct pollfd p = {.fd = 0, .events = POLLIN};
        if (poll(&p, 1, 0) > 0 && (p.revents & POLLIN)) {
            char linha[128];
            if (fgets(linha, sizeof(linha), stdin)) cena_comando(linha);
        }
```

- [ ] **Step 4: Acrescentar `cena.c` ao CMake**

Em `add_executable(wisp-sim …)`, incluir `cena.c` na lista.

- [ ] **Step 5: Compilar e exercitar**

```bash
cmake --build sim/build -j
printf 'n 4\ntodos asking\n' | ./sim/build/wisp-sim
```

Esperado: a janela abre já com quatro mascotes em `asking`, e o log mostra `FPS: <n>  (4 sessao/oes)`. Depois, interativo:

```bash
./sim/build/wisp-sim
```

e digitar `n 2`, `s done 1`, `rest`, `tile 1`, `nolim` — conferindo na janela que cada um muda o que promete. `tile 1` tem de mostrar o painel de limites; `nolim` tem de mostrar "limits unavailable".

- [ ] **Step 6: Commit**

```bash
git add sim/
git commit -m "Drive the simulated screen with typed commands"
```

---

### Task 3: Modo de captura e folha de contato

Esta task existe para a Task 4. Sem uma imagem determinística de antes e depois, "o Terminal continua igual" é opinião.

**Files:**
- Create: `sim/captura.h`
- Create: `sim/captura.c`
- Create: `sim/folha.sh`
- Modify: `sim/main.c`
- Modify: `sim/CMakeLists.txt`

**Interfaces:**
- Consumes: o renderer SDL, via `lv_sdl_window_get_renderer(disp)`.
- Produces:
  - `bool captura_bmp(lv_display_t *disp, const char *caminho)` — grava a janela em BMP de 24 bits.
  - Comando novo em `cena_comando()`: `shot <arquivo>`.
  - `sim/folha.sh` — roteiro que gera a folha de contato completa em `sim/shots/`.

- [ ] **Step 1: Escrever `sim/captura.h`**

```c
/* Grava a janela em BMP.
 *
 * BMP e não PNG porque o LVGL não tem codificador de PNG e trazer libpng para
 * o simulador seria uma dependência a mais para um arquivo temporário. O
 * `sips` que já vem no macOS converte no fim. */
#pragma once

#include <stdbool.h>
#include "lvgl.h"

bool captura_bmp(lv_display_t *disp, const char *caminho);
```

- [ ] **Step 2: Escrever `sim/captura.c`**

Usa `SDL_RenderReadPixels` para ler o framebuffer e `SDL_SaveBMP`, que já vem no SDL2 — não há formato de arquivo para escrever à mão:

```c
#include "captura.h"

#include <stdio.h>
#include <SDL2/SDL.h>

bool captura_bmp(lv_display_t *disp, const char *caminho)
{
    SDL_Renderer *r = (SDL_Renderer *) lv_sdl_window_get_renderer(disp);
    if (!r) return false;

    int w = 0, h = 0;
    if (SDL_GetRendererOutputSize(r, &w, &h) != 0) return false;

    SDL_Surface *s = SDL_CreateRGBSurfaceWithFormat(0, w, h, 24, SDL_PIXELFORMAT_RGB24);
    if (!s) return false;

    bool ok = SDL_RenderReadPixels(r, NULL, SDL_PIXELFORMAT_RGB24,
                                   s->pixels, s->pitch) == 0
           && SDL_SaveBMP(s, caminho) == 0;
    SDL_FreeSurface(s);
    if (ok) printf("I (sim) %s\n", caminho);
    else    printf("E (sim) falhou gravar %s: %s\n", caminho, SDL_GetError());
    return ok;
}
```

- [ ] **Step 3: Ligar o comando `shot`**

`cena.c` passa a expor um ponteiro para o display, setado pelo `main.c` (`void cena_display(lv_display_t *)`), e `cena_comando()` ganha:

```c
    } else if (!strcmp(cmd, "shot")) {
        /* Dois ciclos de lv_timer_handler antes de ler o framebuffer: o
         * primeiro aplica o estado, o segundo desenha. Sem isso a captura
         * pega o quadro anterior — e o sintoma é uma folha de contato em que
         * cada imagem mostra o estado ANTERIOR ao pedido, que é exatamente o
         * tipo de erro que passa despercebido. */
        for (int k = 0; k < 2; k++) { lv_timer_handler(); SDL_Delay(20); }
        captura_bmp(g_disp, arg);
        return;
```

E a ajuda passa a listar `shot <arquivo.bmp>`.

- [ ] **Step 4: Escrever `sim/folha.sh`**

```bash
#!/bin/bash
#
# Folha de contato da tela da placa: cada estado, em cada disposição de sessão,
# mais repouso e o painel de limites.
#
#   ./sim/folha.sh [pasta]
#
# É a rede de proteção da extração do personagem: gere antes de refatorar,
# gere depois, compare byte a byte. Igual significa que o Terminal não mudou.
set -euo pipefail
RAIZ="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
OUT="${1:-$RAIZ/sim/shots}"
mkdir -p "$OUT"
cd "$RAIZ"

ESTADOS="idle working tool asking waiting done error offline"

{
    for n in 1 2 4; do
        echo "n $n"
        for e in $ESTADOS; do
            echo "todos $e"
            echo "shot $OUT/n${n}-${e}.bmp"
        done
    done
    echo "n 1"; echo "rest";   echo "shot $OUT/repouso.bmp"
    echo "wake"; echo "tile 1"; echo "shot $OUT/limites.bmp"
    echo "nolim";              echo "shot $OUT/limites-indisponivel.bmp"
} | ./sim/build/wisp-sim &

SIM=$!
# O simulador não sai sozinho: o laço é infinito de propósito, para o modo
# interativo. Espera o último arquivo aparecer e encerra.
for _ in $(seq 1 120); do
    [[ -f "$OUT/limites-indisponivel.bmp" ]] && break
    sleep 1
done
sleep 1
kill "$SIM" 2>/dev/null || true
wait "$SIM" 2>/dev/null || true

echo "$(ls -1 "$OUT"/*.bmp | wc -l | tr -d ' ') imagens em $OUT"
```

- [ ] **Step 5: Gerar a folha e conferir**

```bash
chmod +x sim/folha.sh && ./sim/folha.sh
ls sim/shots/ | wc -l      # esperado: 27
sips -s format png sim/shots/n1-asking.bmp --out /tmp/olha.png && open /tmp/olha.png
```

Conferir a olho: 27 arquivos, e a imagem aberta mostra de fato o estado `asking` com uma sessão. Se mostrar outro estado, o problema é o número de ciclos no `shot` do passo 3.

- [ ] **Step 6: Commit**

```bash
git add sim/
git commit -m "Capture the simulated screen, and a contact sheet of every state"
```

---

### Task 4: Extrair a interface de personagem

Refatoração pura. Nada muda na tela, e a Task 3 é como isso se prova. Nenhum comportamento novo entra aqui — é a task mais fácil de estragar sem ninguém notar, e por isso a única com critério de aceitação binário.

**Files:**
- Create: `firmware/main/mascote.h`
- Create: `firmware/main/mascote_terminal.c`
- Modify: `firmware/main/ui.c` — remover `criar_mascote()` (`:1004-1179`), `animar_um()` (`:547-762`), as tabelas `COR[]` (`:136`), `ALVO[]` (`:179`), `NOME[]` (`:148`), o `boca_t`/`alvo_t`/`cor_t` e `carregar_fotos()` (`:62-126`), passando tudo para `mascote_terminal.c`
- Modify: `firmware/main/CMakeLists.txt` — acrescentar `mascote_terminal.c` aos `SRCS`
- Modify: `sim/CMakeLists.txt` — acrescentar `../firmware/main/mascote_terminal.c`

**Interfaces:**
- Consumes: `wisp_state_t` de `ui.h`; os helpers de desenho `disco()` (`ui.c:290`), `barra()` (`:303`) e `so_decoracao()` (`:283`), que passam a ser declarados em `mascote.h` e definidos em `ui.c` como não-estáticos.
- Produces:

```c
/* firmware/main/mascote.h — a interface que separa PERSONAGEM de LAYOUT.
 *
 * Existe para que um segundo personagem não signifique um segundo ui.c. O que
 * fica em ui.c: onde cada mascote vai, de que tamanho, quando aparece, e as
 * outras telas. O que fica no personagem: como ele é desenhado e como ele
 * reage ao estado.
 *
 * A escolha é DADO. Não há e não pode haver #if de target aqui: as duas placas
 * têm o mesmo painel e as duas rodam os dois personagens. */
#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "lvgl.h"
#include "ui.h"

/* Parte comum a qualquer personagem: o que o LAYOUT precisa mexer. */
typedef struct {
    lv_obj_t     *raiz;          /* o objeto que aplicar_layout() posiciona */
    lv_obj_t     *detail;        /* rótulo de detalhe, sob o mascote */
    lv_obj_t     *project;       /* rótulo de projeto */
    wisp_state_t  alvo, anterior;
    int16_t       d;             /* diâmetro atual, vindo de vaga_de() */
    char          ult_detalhe[40], ult_projeto[28];
    void         *interno;       /* bloco privado do personagem */
} mascote_t;

typedef struct {
    const char *nome;            /* "terminal", "pixel" — o valor da NVS */
    bool        usa_assets;      /* true = tenta a partição storage */

    /* Chamada uma vez por mascote, na construção da tela. */
    void (*criar)(lv_obj_t *pai, mascote_t *m);

    /* Chamada a cada quadro pelo timer de animação, com o mutex do LVGL JÁ
     * na mão — não chamar bsp_display_lock() aqui dentro, é deadlock. */
    void (*animar)(mascote_t *m, uint32_t agora, bool sozinho);

    /* Chamada quando o layout muda de tamanho ou de visibilidade. `mostrar`
     * false esconde tudo o que pertence ao personagem. */
    void (*dispor)(mascote_t *m, int16_t d, int16_t x, int16_t y, bool mostrar);
} personagem_t;

/* O registro. Índice 0 é o padrão. */
extern const personagem_t *const MASCOTES[];
extern const int MASCOTES_QTD;

/* Nome -> personagem. Desconhecido ou NULL devolve MASCOTES[0]. */
const personagem_t *mascote_por_nome(const char *nome);

/* Escolhe o personagem ativo. Chamar ANTES de ui_create(). */
void mascote_escolher(const personagem_t *p);
const personagem_t *mascote_ativo(void);

/* Helpers de desenho compartilhados, definidos em ui.c. */
lv_obj_t *disco(lv_obj_t *pai, int d, lv_color_t cor, int x, int y);
lv_obj_t *barra(lv_obj_t *pai, int w, int h, lv_color_t cor, int x, int y);
void      so_decoracao(lv_obj_t *o);

/* Os personagens que existem. */
extern const personagem_t MASCOTE_TERMINAL;
```

- [ ] **Step 1: Gerar a folha de referência, ANTES de mexer em nada**

```bash
./sim/folha.sh sim/shots-antes
ls sim/shots-antes/*.bmp | wc -l     # esperado: 27
```

Estes 27 arquivos são a definição de "não mudou". Não versionar: entram no `.gitignore` do `sim/`.

- [ ] **Step 2: Escrever `firmware/main/mascote.h`**

Exatamente o conteúdo do bloco *Produces* acima.

- [ ] **Step 3: Mover o Terminal para `mascote_terminal.c`**

Corte e cole, sem reescrever lógica. Vão para lá: `carregar_fotos()` com `s_assets`/`s_dsc`/`s_tem_fotos`/`IDX`/`ASSETS_QTD`, `cor_t`/`COR[]`, `NOME[]`, `boca_t`/`alvo_t`/`ALVO[]`, `criar_mascote()` renomeada para `terminal_criar()`, `animar_um()` para `terminal_animar()`, e a parte de `aplicar_layout()` que mexe em objetos do computador, virando `terminal_dispor()`.

Os campos hoje em `mascote_t` que são do computador — `corpo`, `olho`, `wisp`, `pupila`, `brilho`, `sobrancelha`, `boca`, `chama`, `tela`, `braco`, `moldura`, `luz`, `scan`, `topo`, `foto`, `p_boca`, `p_esc`, `sob_pts`, `olho_alt`, `olho_dx`, `olho_dy`, `p_alt`, `p_dx`, `p_dy`, `ang`, `vel`, `prox_piscada`, `inicio_piscada` — passam para um `typedef struct { … } terminal_t;` privado do arquivo, alocado em `terminal_criar()` e guardado em `m->interno`.

Atenção ao `sob_pts`: o comentário em `ui.c:204-206` avisa que `lv_line` guarda o **ponteiro** e não copia, então esse array tem de continuar vivo pelo tempo do objeto. Dentro do `terminal_t` alocado uma vez por mascote, continua.

No fim do arquivo, a tabela:

```c
const personagem_t MASCOTE_TERMINAL = {
    .nome       = "terminal",
    .usa_assets = true,
    .criar      = terminal_criar,
    .animar     = terminal_animar,
    .dispor     = terminal_dispor,
};
```

- [ ] **Step 4: Escrever o registro e a escolha, no fim de `mascote_terminal.c`**

Fica aqui, e não em `ui.c`, porque é a lista de personagens e não parte do layout:

```c
static const personagem_t *g_ativo = &MASCOTE_TERMINAL;

const personagem_t *const MASCOTES[] = { &MASCOTE_TERMINAL };
const int MASCOTES_QTD = (int) (sizeof(MASCOTES) / sizeof(MASCOTES[0]));

const personagem_t *mascote_por_nome(const char *nome)
{
    if (!nome || !*nome) return MASCOTES[0];
    for (int i = 0; i < MASCOTES_QTD; i++)
        if (strcmp(MASCOTES[i]->nome, nome) == 0) return MASCOTES[i];
    ESP_LOGW(TAG, "personagem \"%s\" nao existe — usando %s", nome, MASCOTES[0]->nome);
    return MASCOTES[0];
}

void mascote_escolher(const personagem_t *p) { if (p) g_ativo = p; }
const personagem_t *mascote_ativo(void) { return g_ativo; }
```

- [ ] **Step 5: Trocar as chamadas em `ui.c`**

Três pontos, e só três:

| onde | de | para |
|---|---|---|
| `ui_create()` (`:1207-1208`) | `carregar_fotos(); … criar_mascote(tela, &g_m[i]);` | `for (…) mascote_ativo()->criar(tela, &g_m[i]);` |
| `animar()` (`:772`) | `animar_um(&g_m[i], agora, g_qtd == 1);` | `mascote_ativo()->animar(&g_m[i], agora, g_qtd == 1);` |
| `aplicar_layout()` (`:416`) | o bloco que esconde/posiciona objetos do computador | `mascote_ativo()->dispor(m, v.d, v.x, v.y, ativo);` |

O `carregar_fotos()` sai de `ui_create()` e passa a ser chamado de dentro do `terminal_criar()` na primeira vez — é o personagem que sabe se precisa de assets, não o layout.

- [ ] **Step 6: Tornar os helpers de desenho não-estáticos**

Em `ui.c`, remover `static` de `disco()`, `barra()` e `so_decoracao()`. Já estão declarados em `mascote.h`.

- [ ] **Step 7: Compilar nos três alvos**

```bash
cmake --build sim/build -j                                   # host
. ~/esp/esp-idf/export.sh && cd firmware
rm -rf build sdkconfig && idf.py set-target esp32c6 && idf.py build
rm -rf build sdkconfig && idf.py set-target esp32s3 && idf.py build
cd ..
```

O `rm -rf build sdkconfig` não é zelo: `set-target` recusa um diretório de build do outro alvo, e o IDF não reaplica `sdkconfig.defaults` sobre um `sdkconfig` existente. Está nas armadilhas do `CLAUDE.md`.

- [ ] **Step 8: Provar que nada mudou na tela**

O build do C6 tem de rodar antes, para o `mmap_build/` existir e o caminho de imagem ser exercitado:

```bash
./sim/folha.sh sim/shots-depois
for f in sim/shots-antes/*.bmp; do
    cmp -s "$f" "sim/shots-depois/$(basename "$f")" || echo "MUDOU: $(basename "$f")"
done
echo "fim da comparacao"
```

Esperado: nenhuma linha `MUDOU:`. Qualquer arquivo diferente é uma regressão na extração — para, acha, corrige. Não seguir para a Task 5 com uma diferença "pequena": o Terminal não tinha nenhuma razão para mudar.

- [ ] **Step 9: Verificar que a regra do `CLAUDE.md` continua valendo**

```bash
grep -n "CONFIG_IDF_TARGET" firmware/main/ui.c firmware/main/mascote_terminal.c firmware/main/mascote.h
```

Esperado: nenhuma saída.

- [ ] **Step 10: Commit**

```bash
git add firmware/main/mascote.h firmware/main/mascote_terminal.c \
        firmware/main/ui.c firmware/main/CMakeLists.txt sim/CMakeLists.txt
git commit -m "Separate the character from the layout, with the Terminal unchanged"
```

---

### Task 5: O personagem pixel — silhueta e olhos

Primeira metade do personagem novo: o corpo e os olhos, nos oito estados. Sem sobrancelha, sem boca, sem props — isso é a Task 6 e a Task 7. Ao fim desta task o personagem já é escolhível no simulador e já se distingue `idle` de `offline`.

**A silhueta em degraus, sem bitmap.** Um quadrado arredondado de pixel art é, geometricamente, retângulos empilhados com o canto cortado em degraus. Três retângulos de raio zero dão dois degraus por canto, que é o que a referência mostra:

```
      ┌────────┐            rect B (estreito, alto)
   ┌──┴────────┴──┐         rect C (médio)
   │              │
 ┌─┴──────────────┴─┐       rect A (largo, baixo)
 │                  │
 └─┬──────────────┬─┘
   │              │
   └──┬────────┬──┘
      └────────┘
```

**Files:**
- Create: `firmware/main/mascote_pixel.c`
- Modify: `firmware/main/mascote_terminal.c` — acrescentar `&MASCOTE_PIXEL` a `MASCOTES[]`
- Modify: `firmware/main/mascote.h` — declarar `extern const personagem_t MASCOTE_PIXEL;`
- Modify: `firmware/main/CMakeLists.txt` e `sim/CMakeLists.txt`
- Modify: `sim/cena.c` — comando `char <nome>`

**Interfaces:**
- Consumes: `personagem_t`, `mascote_t`, `mascote_por_nome()`, `mascote_escolher()` de `mascote.h`.
- Produces: `const personagem_t MASCOTE_PIXEL` com `.nome = "pixel"` e `.usa_assets = false`. Comando novo no simulador: `char terminal` / `char pixel`.

- [ ] **Step 1: A paleta e a tabela de estados**

```c
/* firmware/main/mascote_pixel.c — o personagem em pixel art.
 *
 * A cara é feita de OBJETOS LVGL, não de bitmap. O motivo está medido no
 * projeto irmão: escalar bitmap por software custa ~0,76 µs por pixel de
 * saída, o que no C6 de núcleo único dá 100–220 ms por quadro. Objeto o LVGL
 * move e recolore sem transformar nada, e é por isso que o mascote vetorial
 * anima nesta placa e o de imagem não.
 *
 * MEDIDAS EM MILÉSIMOS DE `d`
 * ---------------------------
 * Nada aqui é px absoluto: `d` chega como 306, 178 ou 140 (ui.c:388) e todas as
 * medidas são fração dele. A grade da referência tem 22 unidades de largura na
 * cara, então uma "unidade de arte" é d/22.
 *
 * A COR NÃO MUDA COM O ESTADO
 * ---------------------------
 * Diferente do Terminal, onde a tela troca de cor e a informação chega como luz.
 * Aqui a referência é amarela nos oito estados, e quem diz o estado é a CARA
 * mais o adorno. Consequência assumida: perde-se a leitura periférica por cor.
 * Se na placa isso se mostrar pior, o lugar de corrigir é o tom de sombra —
 * escurecer para `error`, esfriar para `offline` — sem mexer no corpo. */
#include <string.h>

#include "esp_log.h"
#include "mascote.h"

static const char *TAG = "pixel";

#define ART_W 22                 /* unidades de arte na largura da cara */
#define U(d, n) ((int) ((d) * (n) / ART_W))    /* n unidades de arte, em px */

static const lv_color_t C_CLARO  = LV_COLOR_MAKE(255, 209,  74);
static const lv_color_t C_BASE   = LV_COLOR_MAKE(255, 184,  28);
static const lv_color_t C_SOMBRA = LV_COLOR_MAKE(224, 138,   0);
static const lv_color_t C_OLHO   = LV_COLOR_MAKE( 17,  17,  17);

typedef enum { OLHO_NORMAL, OLHO_ARCO, OLHO_X, OLHO_TRISTE } olho_t;

typedef struct {
    olho_t  olho;
    uint8_t respira;      /* amplitude em milésimos de d */
    int8_t  inclina;      /* graus, fixo */
} pixel_alvo_t;

/* Um por estado. A ordem é a de wisp_state_t (ui.h). */
static const pixel_alvo_t ALVO[WISP_COUNT] = {
    [WISP_IDLE]    = {OLHO_NORMAL,  6,  0},
    [WISP_WORKING] = {OLHO_NORMAL,  4,  0},
    [WISP_TOOL]    = {OLHO_NORMAL,  3,  0},
    [WISP_ASKING]  = {OLHO_NORMAL,  7,  0},
    [WISP_WAITING] = {OLHO_TRISTE, 10,  0},
    [WISP_DONE]    = {OLHO_ARCO,    9,  0},
    [WISP_ERROR]   = {OLHO_TRISTE,  3, -4},
    [WISP_OFFLINE] = {OLHO_X,       2,  0},
};
```

- [ ] **Step 2: O bloco privado e a construção**

```c
/* 7 objetos: 3 do corpo, 2 de sombreado, 2 de olho. A conta de quantos
 * objetos por mascote importa: com quatro sessões isto é multiplicado por
 * quatro, e a Task 9 mede o custo. */
typedef struct {
    lv_obj_t *corpo[3];      /* A largo-baixo, B estreito-alto, C médio */
    lv_obj_t *topo, *base;   /* faixa clara em cima, escura embaixo */
    lv_obj_t *olho[2];
    lv_obj_t *pupila[2];     /* usadas por OLHO_X: a segunda barra do X */
    int16_t   p_d;           /* guarda: só refaz geometria se `d` mudou */
    /* `int`, e não os enums, porque -1 é o valor de "invalidado" que força a
     * reaplicação. Enum recebendo -1 é comportamento que depende do compilador. */
    int       p_olho;
} pixel_t;

static lv_obj_t *retangulo(lv_obj_t *pai, lv_color_t cor)
{
    lv_obj_t *o = lv_obj_create(pai);
    /* so_decoracao() só cuida de scroll, clique e bolha de evento — NÃO zera
     * borda nem padding. Ver disco() em ui.c:290, que também zera os dois à
     * mão. Sem isto, todo retângulo sai com a borda cinza padrão do LVGL e a
     * silhueta em degraus fica desenhada com contorno. */
    so_decoracao(o);
    lv_obj_set_style_border_width(o, 0, 0);
    lv_obj_set_style_pad_all(o, 0, 0);
    lv_obj_set_style_radius(o, 0, 0);      /* canto quadrado: o degrau é a forma */
    lv_obj_set_style_bg_color(o, cor, 0);
    lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
    return o;
}

static void pixel_criar(lv_obj_t *pai, mascote_t *m)
{
    pixel_t *p = lv_malloc_zeroed(sizeof(pixel_t));
    m->interno = p;

    /* A raiz é um contêiner transparente: é ele que o layout move e escala, e
     * é nele que a inclinação de `error` é aplicada — girar o contêiner gira a
     * cara inteira de uma vez, em vez de girar sete objetos em sincronia. */
    m->raiz = lv_obj_create(pai);
    so_decoracao(m->raiz);
    lv_obj_set_style_bg_opa(m->raiz, LV_OPA_TRANSP, 0);

    for (int i = 0; i < 3; i++) p->corpo[i] = retangulo(m->raiz, C_BASE);
    p->topo = retangulo(m->raiz, C_CLARO);
    p->base = retangulo(m->raiz, C_SOMBRA);
    for (int i = 0; i < 2; i++) {
        p->olho[i]   = retangulo(m->raiz, C_OLHO);
        p->pupila[i] = retangulo(m->raiz, C_OLHO);
        lv_obj_add_flag(p->pupila[i], LV_OBJ_FLAG_HIDDEN);
    }
    p->p_d = -1;
    ESP_LOGI(TAG, "mascote pixel criado");
}
```

- [ ] **Step 3: A geometria, refeita só quando `d` muda**

```c
/* Um degrau tem 3 unidades de arte — é o que a referência mostra nos cantos. */
#define DEG 3

static void geometria(pixel_t *p, int16_t d)
{
    if (p->p_d == d) return;
    p->p_d = d;
    /* Invalida as guardas de rosto. Sem isto há um bug silencioso: a geometria
     * redefine olhos e adorno no tamanho NEUTRO, e aplicar_olho()/aplicar_prop()
     * veem o mesmo estado de antes e não corrigem — o personagem perde o olho
     * em arco, o X e o prop assim que o layout troca de número de sessões. */
    p->p_olho = -1;

    const int deg = U(d, DEG);

    /* A: largura cheia, altura menos dois degraus. */
    lv_obj_set_size(p->corpo[0], d, d - 2 * deg);
    lv_obj_align(p->corpo[0], LV_ALIGN_CENTER, 0, 0);
    /* B: altura cheia, largura menos dois degraus. */
    lv_obj_set_size(p->corpo[1], d - 2 * deg, d);
    lv_obj_align(p->corpo[1], LV_ALIGN_CENTER, 0, 0);
    /* C: o segundo degrau, meio caminho entre os dois. */
    lv_obj_set_size(p->corpo[2], d - deg, d - deg);
    lv_obj_align(p->corpo[2], LV_ALIGN_CENTER, 0, 0);

    /* Faixa clara em cima e escura embaixo: uma unidade de arte de espessura,
     * recuada dos degraus para não vazar no canto cortado. */
    lv_obj_set_size(p->topo, d - 2 * deg, U(d, 1));
    lv_obj_align(p->topo, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_set_size(p->base, d - 2 * deg, U(d, 2));
    lv_obj_align(p->base, LV_ALIGN_BOTTOM_MID, 0, 0);

    /* Olhos: 2 unidades de largura, 5 de altura, a 4 unidades do centro. */
    for (int i = 0; i < 2; i++) {
        lv_obj_set_size(p->olho[i], U(d, 2), U(d, 5));
        lv_obj_align(p->olho[i], LV_ALIGN_CENTER, (i ? 1 : -1) * U(d, 4), -U(d, 1));
        lv_obj_set_size(p->pupila[i], U(d, 5), U(d, 2));
        lv_obj_align(p->pupila[i], LV_ALIGN_CENTER, (i ? 1 : -1) * U(d, 4), -U(d, 1));
    }
}
```

- [ ] **Step 4: Os quatro olhos**

```c
static void aplicar_olho(pixel_t *p, olho_t o, int16_t d)
{
    /* Só o olho na guarda: geometria() já invalidou p_olho quando `d` mudou. */
    if (p->p_olho == (int) o) return;
    p->p_olho = (int) o;

    for (int i = 0; i < 2; i++) {
        lv_obj_remove_flag(p->olho[i], LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(p->pupila[i], LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_style_transform_rotation(p->olho[i], 0, 0);
        lv_obj_set_size(p->olho[i], U(d, 2), U(d, 5));
    }

    switch (o) {
    case OLHO_NORMAL:
        break;                                   /* o padrão da geometria */
    case OLHO_ARCO:
        /* Arco para cima: uma barra baixa e larga é o que lê como olho
         * fechado de contentamento em pixel art — não há curva a 2px. */
        for (int i = 0; i < 2; i++) lv_obj_set_size(p->olho[i], U(d, 4), U(d, 1));
        break;
    case OLHO_TRISTE:
        /* Olho um pouco mais baixo e estreito: sozinho já entristece; a
         * sobrancelha da Task 6 é o que fecha a expressão. */
        for (int i = 0; i < 2; i++) lv_obj_set_size(p->olho[i], U(d, 2), U(d, 4));
        break;
    case OLHO_X:
        /* Duas barras cruzadas por olho. A rotação é de OBJETO, aplicada uma
         * vez na troca de estado — não é transformação por quadro. */
        for (int i = 0; i < 2; i++) {
            lv_obj_set_size(p->olho[i], U(d, 5), U(d, 2));
            lv_obj_set_style_transform_rotation(p->olho[i], 450, 0);    /* 45° */
            lv_obj_remove_flag(p->pupila[i], LV_OBJ_FLAG_HIDDEN);
            lv_obj_set_style_transform_rotation(p->pupila[i], -450, 0);
        }
        break;
    }
}
```

- [ ] **Step 5: Animar e dispor**

```c
static void pixel_animar(mascote_t *m, uint32_t agora, bool sozinho)
{
    (void) sozinho;
    pixel_t *p = m->interno;
    if (!p) return;

    const pixel_alvo_t *a = &ALVO[m->alvo];
    geometria(p, m->d);
    aplicar_olho(p, a->olho, m->d);

    /* Respiração: escala vertical em milésimos, com o volume conservado —
     * o que estica na vertical encolhe na horizontal, senão o boneco INFLA.
     * Âncora embaixo, porque o personagem se apoia no chão do quadro. */
    const int32_t fase = lv_trigo_sin((int16_t) ((agora / 8) % 360));  /* -32767..32767 */
    const int32_t amp  = a->respira;
    const int32_t sy   = 256 + fase * amp / 32767 / 4;
    const int32_t sx   = 256 * 256 / (sy ? sy : 256);
    lv_obj_set_style_transform_scale_y(m->raiz, (int32_t) sy, 0);
    lv_obj_set_style_transform_scale_x(m->raiz, (int32_t) sx, 0);
    lv_obj_set_style_transform_pivot_y(m->raiz, m->d, 0);
    lv_obj_set_style_transform_rotation(m->raiz, a->inclina * 10, 0);
}

static void pixel_dispor(mascote_t *m, int16_t d, int16_t x, int16_t y, bool mostrar)
{
    if (!m->raiz) return;
    if (!mostrar) { lv_obj_add_flag(m->raiz, LV_OBJ_FLAG_HIDDEN); return; }
    lv_obj_remove_flag(m->raiz, LV_OBJ_FLAG_HIDDEN);
    m->d = d;
    lv_obj_set_size(m->raiz, d, d);
    lv_obj_align(m->raiz, LV_ALIGN_CENTER, x, y);
    geometria(m->interno, d);
}

const personagem_t MASCOTE_PIXEL = {
    .nome       = "pixel",
    .usa_assets = false,
    .criar      = pixel_criar,
    .animar     = pixel_animar,
    .dispor     = pixel_dispor,
};
```

- [ ] **Step 6: Registrar e permitir trocar no simulador**

Em `mascote_terminal.c`, `MASCOTES[]` passa a `{ &MASCOTE_TERMINAL, &MASCOTE_PIXEL }`. Em `mascote.h`, acrescentar `extern const personagem_t MASCOTE_PIXEL;`.

Em `sim/cena.c`, comando novo. Trocar de personagem exige reconstruir a tela, porque os objetos foram criados pelo personagem antigo:

```c
    } else if (!strcmp(cmd, "char")) {
        mascote_escolher(mascote_por_nome(arg));
        /* Tela limpa e reconstruída: os objetos pertencem ao personagem que
         * os criou, e reaproveitar não daria um híbrido, daria lixo. */
        lv_obj_clean(lv_screen_active());
        ui_create();
        ui_update(&d);
        return;
```

- [ ] **Step 7: Compilar e olhar os oito estados**

```bash
cmake --build sim/build -j
printf 'char pixel\nn 1\ns idle\nshot /tmp/px-idle.bmp\ns done\nshot /tmp/px-done.bmp\ns offline\nshot /tmp/px-offline.bmp\n' | ./sim/build/wisp-sim
sips -s format png /tmp/px-idle.bmp --out /tmp/px-idle.png && open /tmp/px-idle.png
```

Esperado: quadrado amarelo de cantos em degrau, faixa clara em cima e escura embaixo, dois olhos verticais escuros. `done` com olhos em arco baixo, `offline` com dois X. O corpo respira, e em `error` fica torto.

- [ ] **Step 8: Conferir que o Terminal não regrediu**

```bash
./sim/folha.sh sim/shots-t5
for f in sim/shots-antes/*.bmp; do
    cmp -s "$f" "sim/shots-t5/$(basename "$f")" || echo "MUDOU: $(basename "$f")"
done
```

Esperado: nenhuma linha. A folha usa o personagem padrão, que continua sendo o Terminal.

- [ ] **Step 9: Commit**

```bash
git add firmware/main/mascote_pixel.c firmware/main/mascote.h \
        firmware/main/mascote_terminal.c firmware/main/CMakeLists.txt \
        sim/CMakeLists.txt sim/cena.c
git commit -m "Draw the pixel character's body and eyes from LVGL objects"
```

---

### Task 6: Expressão — sobrancelha, boca e piscar

Os oito estados já se distinguem, mas dois pares ainda se confundem: `waiting` de `error`, e `working` de `idle`. É a sobrancelha que separa preocupado de bravo, e é a boca que separa contentamento de aflição — a mesma lição que o Terminal já aprendeu (`ui.c:161-177`).

**Files:**
- Modify: `firmware/main/mascote_pixel.c`

**Interfaces:**
- Consumes: `pixel_t`, `pixel_alvo_t`, `ALVO[]`, `U()` da Task 5.
- Produces: `pixel_alvo_t` cresce com `boca_t boca`, `bool sobrancelha` e `bool pisca`; `pixel_t` cresce com `lv_obj_t *sobrancelha[2]`, `*boca`, `uint32_t prox_piscada, inicio_piscada`.

- [ ] **Step 1: Estender a tabela**

```c
typedef enum { BOCA_NENHUMA, BOCA_ABERTA, BOCA_TRISTE } boca_t;

/* pixel_alvo_t ganha três campos: */
    boca_t  boca;
    bool    sobrancelha;   /* preocupada: ponta INTERNA para cima */
    bool    pisca;         /* estados vivos piscam; offline e error não */

static const pixel_alvo_t ALVO[WISP_COUNT] = {
    /*                    olho        resp incl | boca          sobr   pisca */
    [WISP_IDLE]    = {OLHO_NORMAL,  6,  0, BOCA_NENHUMA, false, true },
    [WISP_WORKING] = {OLHO_NORMAL,  4,  0, BOCA_NENHUMA, false, true },
    [WISP_TOOL]    = {OLHO_NORMAL,  3,  0, BOCA_NENHUMA, false, true },
    [WISP_ASKING]  = {OLHO_NORMAL,  7,  0, BOCA_NENHUMA, false, true },
    [WISP_WAITING] = {OLHO_TRISTE, 10,  0, BOCA_NENHUMA, true,  true },
    [WISP_DONE]    = {OLHO_ARCO,    9,  0, BOCA_ABERTA,  false, false},
    [WISP_ERROR]   = {OLHO_TRISTE,  3, -4, BOCA_TRISTE,  true,  false},
    [WISP_OFFLINE] = {OLHO_X,       2,  0, BOCA_NENHUMA, false, false},
};
```

`done` não pisca porque os olhos já estão em arco: piscar um olho fechado não comunica nada. `error` e `offline` não piscam porque a imobilidade é parte do que eles dizem.

- [ ] **Step 2: Criar os três objetos novos**

Em `pixel_criar()`, depois dos olhos:

```c
    for (int i = 0; i < 2; i++) {
        p->sobrancelha[i] = retangulo(m->raiz, C_OLHO);
        lv_obj_add_flag(p->sobrancelha[i], LV_OBJ_FLAG_HIDDEN);
    }
    p->boca = retangulo(m->raiz, C_OLHO);
    lv_obj_add_flag(p->boca, LV_OBJ_FLAG_HIDDEN);
```

Total agora: 10 objetos por mascote.

- [ ] **Step 3: A sobrancelha preocupada**

```c
/* A diferença entre parecer PREOCUPADO e parecer BRAVO é qual ponta sobe.
 * Sobe a ponta INTERNA — a que fica perto do centro da cara. Invertido, o
 * personagem culpa quem está olhando, e o `error` do Wisp existe justamente
 * para dizer o contrário. */
static void aplicar_sobrancelha(pixel_t *p, bool mostrar, int16_t d)
{
    for (int i = 0; i < 2; i++) {
        if (!mostrar) { lv_obj_add_flag(p->sobrancelha[i], LV_OBJ_FLAG_HIDDEN); continue; }
        lv_obj_remove_flag(p->sobrancelha[i], LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_size(p->sobrancelha[i], U(d, 3), U(d, 1));
        lv_obj_align(p->sobrancelha[i], LV_ALIGN_CENTER,
                     (i ? 1 : -1) * U(d, 4), -U(d, 5));
        /* +18° no olho esquerdo, -18° no direito: os dois com a ponta de
         * dentro para cima. */
        lv_obj_set_style_transform_rotation(p->sobrancelha[i], (i ? -180 : 180), 0);
    }
}
```

- [ ] **Step 4: As duas bocas**

```c
static void aplicar_boca(pixel_t *p, boca_t b, int16_t d)
{
    if (b == BOCA_NENHUMA) { lv_obj_add_flag(p->boca, LV_OBJ_FLAG_HIDDEN); return; }
    lv_obj_remove_flag(p->boca, LV_OBJ_FLAG_HIDDEN);
    if (b == BOCA_ABERTA) {
        /* Bloco cheio: em pixel art, boca aberta de alegria é um retângulo. */
        lv_obj_set_size(p->boca, U(d, 5), U(d, 3));
        lv_obj_align(p->boca, LV_ALIGN_CENTER, 0, U(d, 4));
        lv_obj_set_style_radius(p->boca, U(d, 1), 0);
    } else {
        /* Traço fino, virado: a curvatura é o que separa contentamento de
         * aflição, e a 2px de espessura a "curva" é uma inclinação. */
        lv_obj_set_size(p->boca, U(d, 5), U(d, 1));
        lv_obj_align(p->boca, LV_ALIGN_CENTER, 0, U(d, 5));
        lv_obj_set_style_radius(p->boca, 0, 0);
    }
}
```

- [ ] **Step 5: Piscar**

```c
#define PISCADA_MS 170     /* o mesmo do Terminal (ui.c:129) */

/* Chamado de pixel_animar(). Piscar é a animação mais barata que existe:
 * um objeto muda de altura por 170ms. */
static void piscar(pixel_t *p, const pixel_alvo_t *a, uint32_t agora, int16_t d)
{
    if (!a->pisca) return;
    if (p->prox_piscada == 0) p->prox_piscada = agora + 2600;

    if (agora >= p->prox_piscada && p->inicio_piscada == 0) {
        p->inicio_piscada = agora;
        /* Intervalo irregular: 2,2 a 4,6s. Uma piscada em cadência exata lê
         * como pisca-pisca, não como olho. */
        p->prox_piscada = agora + 2200 + (agora % 2400);
    }
    if (p->inicio_piscada) {
        if (agora - p->inicio_piscada < PISCADA_MS) {
            for (int i = 0; i < 2; i++) lv_obj_set_height(p->olho[i], U(d, 1));
        } else {
            p->inicio_piscada = 0;
            p->p_olho = -1;          /* força aplicar_olho a restaurar */
        }
    }
}
```

Em `pixel_animar()`, depois de `aplicar_olho()`, chamar `aplicar_sobrancelha(p, a->sobrancelha, m->d)`, `aplicar_boca(p, a->boca, m->d)` e `piscar(p, a, agora, m->d)`.

- [ ] **Step 6: Compilar e conferir os pares que se confundiam**

```bash
cmake --build sim/build -j
printf 'char pixel\nn 1\ns waiting\nshot /tmp/px-waiting.bmp\ns error\nshot /tmp/px-error.bmp\ns done\nshot /tmp/px-done.bmp\n' | ./sim/build/wisp-sim
for f in waiting error done; do sips -s format png /tmp/px-$f.bmp --out /tmp/px-$f.png; done
open /tmp/px-waiting.png /tmp/px-error.png /tmp/px-done.png
```

Esperado, e este é o critério: **`waiting` e `error` têm de ser distinguíveis num relance**, sem ler rótulo. `error` está torto e com a boca virada para baixo; `waiting` está reto e sem boca. Se os dois parecerem a mesma coisa, o problema é a expressão e não o código — ajustar os valores de `ALVO[]` até separar.

- [ ] **Step 7: Conferir o piscar**

Rodar `./sim/build/wisp-sim`, digitar `char pixel` e olhar por trinta segundos: as piscadas têm de ser irregulares. Depois `s offline` — não pode piscar.

- [ ] **Step 8: Commit**

```bash
git add firmware/main/mascote_pixel.c
git commit -m "Put the expression in the pixel character's brow, mouth and blink"
```

---

### Task 7: Props

Os adornos da folha de referência: bolha de pensamento, laptop com mãos, `?`, mãos juntas, faíscas e wifi cortado. São a parte que não sai de parâmetro — dedos e um laptop não são uma tabela de ângulos.

**Onde eles vivem:** arrays C no binário do app, gerados em tempo de build a partir de mapas ASCII. Não na partição `storage`. Três razões: a partição já está com 2,2MB de arte do Terminal em 3MB disponíveis (mais apertada do que o comentário em `partitions.esp32c6.csv` diz, que fala de 1,3MB); um prop de 24×16 são ~1,2KB, que em flash de app não se sente; e mapa ASCII em texto é editável, diffável e revisável, o que binário não é. O padrão é o mesmo do projeto irmão, que versiona o `.h` gerado e proíbe editar à mão.

**Files:**
- Create: `firmware/props/README.md`
- Create: `firmware/props/paleta.txt`
- Create: `firmware/props/bolha.txt`, `laptop.txt`, `pergunta.txt`, `maos.txt`, `faiscas.txt`, `wifi.txt`
- Create: `firmware/tools/props_to_c.py`
- Create: `firmware/main/mascote_pixel_props.c` (gerado, versionado)
- Create: `firmware/main/mascote_pixel_props.h`
- Modify: `firmware/main/mascote_pixel.c`
- Modify: `firmware/main/CMakeLists.txt`, `sim/CMakeLists.txt`

**Interfaces:**
- Produces:
  - `firmware/main/mascote_pixel_props.h` declara `typedef enum { PROP_NENHUM, PROP_BOLHA, PROP_LAPTOP, PROP_PERGUNTA, PROP_MAOS, PROP_FAISCAS, PROP_WIFI, PROP_QTD } prop_t;` e `const lv_image_dsc_t *prop_dsc(prop_t p);`
  - `pixel_alvo_t` cresce com `prop_t prop;` e a posição do prop (`int8_t prop_x, prop_y;` em unidades de arte).

- [ ] **Step 1: Formato do mapa e a paleta**

`firmware/props/paleta.txt` — um caractere por cor, em hex RGB. `.` é sempre transparente:

```
. transparente
K 1a1a1a
k 3a3a3a
w ffffff
y ffb81c
Y ffd14a
o e08a00
```

`firmware/props/laptop.txt` — 18×8 unidades de arte. A tampa preta com um ponto de luz, e as mãos amarelas nas pontas:

```
..................
....KKKKKKKKKK....
...KkkkkkkkkkkK...
...KkkkkkwkkkkK...
...KkkkkkkkkkkK...
..KKKKKKKKKKKKKK..
.yy............yy.
.YY............YY.
```

Os outros cinco, no mesmo formato. São ponto de partida a acertar olhando o simulador contra a folha de referência — o critério é ler a 140px, com cada unidade de arte em ~6px. Editar mapa de texto e rodar o gerador é um ciclo de segundos, e é aí que o desenho acontece.

`firmware/props/bolha.txt` — 7×6:

```
...yyy.
..y...y
..y...y
...yyy.
.yy....
y......
```

`firmware/props/pergunta.txt` — 6×9:

```
..yyy.
.y...y
.y...y
....y.
...y..
..y...
..y...
......
..y...
```

`firmware/props/maos.txt` — 9×6:

```
...o.o...
..oyoyo..
.oyyoyyo.
.oyyyyyo.
..ooooo..
.........
```

`firmware/props/faiscas.txt` — 7×6:

```
....yy.
...yy..
y..y...
yy.....
...yy..
....yy.
```

`firmware/props/wifi.txt` — 9×7:

```
..yyyyy..
.y.....y.
...yyy...
..y...y..
....y....
.o.....o.
..o...o..
```

- [ ] **Step 2: Escrever o gerador**

`firmware/tools/props_to_c.py`:

```python
#!/usr/bin/env python3
"""
Converte os mapas ASCII de firmware/props/ em lv_image_dsc_t de RGB565A8.

    python3 firmware/tools/props_to_c.py

Escreve firmware/main/mascote_pixel_props.c. NÃO EDITAR O .c À MÃO — rode isto.

POR QUE EM TEMPO DE BUILD
-------------------------
O ui.c do projeto documenta, com medição, que decodificar imagem em tempo de
execução nesta placa custa o firmware inteiro: FPS de 62 para 1-7 e RAM interna
em 12 bytes de mínimo histórico. A regra do repositório é que a conversão
acontece antes. Isto obedece.

FORMATO RGB565A8
----------------
O plano de cor inteiro (w*h uint16, little-endian), seguido do plano de alfa
(w*h uint8). O stride é w*2 — é somando stride*h ao mesmo ponteiro que o LVGL
encontra o alfa.
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


def converter(nome: str, pal: dict) -> tuple:
    linhas = [l for l in (PROPS / f"{nome}.txt").read_text().splitlines() if l.strip("\n")]
    h = len(linhas)
    w = max(len(l) for l in linhas)
    cor, alfa = [], []
    for y in range(h):
        linha = linhas[y].ljust(w, ".")
        for x in range(w):
            v = pal.get(linha[x], None)
            cor.append(0 if v is None else rgb565(v))
            alfa.append(0 if v is None else 255)
    return w, h, cor, alfa


def main() -> None:
    pal = paleta()
    partes = [
        '/* GERADO por firmware/tools/props_to_c.py — não editar à mão.\n'
        ' * Fonte: firmware/props/*.txt. Para mudar um adorno, edite o mapa\n'
        ' * ASCII e rode o gerador de novo. */\n'
        '#include "mascote_pixel_props.h"\n'
    ]
    tabela = []
    for nome in ORDEM:
        w, h, cor, alfa = converter(nome, pal)
        bytes_ = []
        for c in cor:
            bytes_ += [c & 0xFF, (c >> 8) & 0xFF]      # little-endian
        bytes_ += alfa
        corpo = ",".join(str(b) for b in bytes_)
        partes.append(
            f"\nstatic const uint8_t {nome}_dados[] = {{{corpo}}};\n"
            f"static const lv_image_dsc_t {nome}_dsc = {{\n"
            f"    .header = {{ .magic = LV_IMAGE_HEADER_MAGIC,\n"
            f"                 .cf = LV_COLOR_FORMAT_RGB565A8,\n"
            f"                 .w = {w}, .h = {h}, .stride = {w * 2} }},\n"
            f"    .data = {nome}_dados,\n"
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
    print(f"{SAIDA.relative_to(RAIZ)}: {len(ORDEM)} props")


if __name__ == "__main__":
    main()
```

- [ ] **Step 3: Escrever o header à mão e rodar o gerador**

`firmware/main/mascote_pixel_props.h`:

```c
/* Adornos do personagem pixel. O .c é gerado — ver firmware/tools/props_to_c.py */
#pragma once

#include "lvgl.h"

typedef enum {
    PROP_NENHUM = 0,
    PROP_BOLHA, PROP_LAPTOP, PROP_PERGUNTA, PROP_MAOS, PROP_FAISCAS, PROP_WIFI,
    PROP_QTD
} prop_t;

const lv_image_dsc_t *prop_dsc(prop_t p);
```

```bash
python3 firmware/tools/props_to_c.py
wc -c firmware/main/mascote_pixel_props.c
```

Esperado: os seis props declarados, e o arquivo na ordem de dezenas de KB — não de MB. Se passar de 100KB, algum mapa saiu grande demais.

- [ ] **Step 4: Mostrar o prop no personagem**

`pixel_alvo_t` ganha `prop_t prop;` e `int8_t prop_x, prop_y;` (unidades de arte a partir do centro). A tabela:

| estado | prop | x | y |
|---|---|---|---|
| `idle` | `PROP_NENHUM` | 0 | 0 |
| `working` | `PROP_BOLHA` | 9 | -12 |
| `tool` | `PROP_LAPTOP` | 0 | 8 |
| `asking` | `PROP_PERGUNTA` | 10 | -11 |
| `waiting` | `PROP_MAOS` | 0 | 9 |
| `done` | `PROP_FAISCAS` | 9 | -11 |
| `error` | `PROP_NENHUM` | 0 | 0 |
| `offline` | `PROP_WIFI` | 0 | -13 |

`mascote_pixel.c` passa a incluir `"mascote_pixel_props.h"`. `pixel_t` ganha `lv_obj_t *prop;` e `int p_prop;` — `int` pelo mesmo motivo do `p_olho`, e `geometria()` passa a invalidar os dois (`p->p_olho = -1; p->p_prop = -1;`). Criado em `pixel_criar()` como filho da **raiz**, para acompanhar respiração e inclinação:

```c
    p->prop = lv_image_create(m->raiz);
    so_decoracao(p->prop);
    lv_image_set_antialias(p->prop, false);   /* pixel duro, não borrão */
    lv_obj_add_flag(p->prop, LV_OBJ_FLAG_HIDDEN);
    p->p_prop = PROP_NENHUM;
```

E a aplicação, chamada de `pixel_animar()`:

```c
/* A escala é INTEIRA e é calculada aqui, sem tocar no layout: `d` vem de
 * vaga_de() e é geometria afinada à mão. Uma unidade de arte tem d/22 px, então
 * a escala do prop é esse número arredondado para baixo, no mínimo 1.
 *
 * lv_image_set_scale TRANSFORMA por software. Custa ~0,76 µs por pixel de
 * saída, medido no projeto irmão — inaceitável por quadro, irrelevante uma vez
 * por troca de estado. A guarda p_prop é o que garante "uma vez". */
static void aplicar_prop(pixel_t *p, const pixel_alvo_t *a, int16_t d)
{
    /* Mesma regra do olho: geometria() invalida, aqui só se compara o prop. */
    if (p->p_prop == (int) a->prop) return;
    p->p_prop = (int) a->prop;

    const lv_image_dsc_t *dsc = prop_dsc(a->prop);
    if (!dsc) { lv_obj_add_flag(p->prop, LV_OBJ_FLAG_HIDDEN); return; }

    int escala = U(d, 1);
    if (escala < 1) escala = 1;

    lv_image_set_src(p->prop, dsc);
    lv_image_set_scale(p->prop, escala * 256);
    lv_obj_set_size(p->prop, dsc->header.w * escala, dsc->header.h * escala);
    lv_obj_align(p->prop, LV_ALIGN_CENTER, U(d, a->prop_x), U(d, a->prop_y));
    lv_obj_remove_flag(p->prop, LV_OBJ_FLAG_HIDDEN);
}
```

- [ ] **Step 5: Compilar e olhar a folha do personagem novo**

```bash
python3 firmware/tools/props_to_c.py && cmake --build sim/build -j
{ echo "char pixel"; for e in idle working tool asking waiting done error offline; do
    echo "s $e"; echo "shot /tmp/px-$e.bmp"; done; } | ./sim/build/wisp-sim
for e in idle working tool asking waiting done error offline; do
    sips -s format png /tmp/px-$e.bmp --out /tmp/px-$e.png >/dev/null; done
open /tmp/px-*.png
```

Esperado: os oito estados com os adornos nos lugares, cada prop com pixel duro e sem borrão. Comparar a olho com a folha de referência.

- [ ] **Step 6: Conferir com quatro sessões, que é o caso apertado**

```bash
printf 'char pixel\nn 4\ntodos tool\nshot /tmp/px4-tool.bmp\ntodos done\nshot /tmp/px4-done.bmp\n' | ./sim/build/wisp-sim
sips -s format png /tmp/px4-tool.bmp --out /tmp/px4-tool.png && open /tmp/px4-tool.png
```

Esperado: quatro mascotes de 140px, cada um com o seu laptop, sem invasão entre vagas e sem prop encostando no rótulo de projeto. Se invadir, ajustar `prop_x`/`prop_y` — não `vaga_de()`.

- [ ] **Step 7: Compilar para as duas placas**

```bash
. ~/esp/esp-idf/export.sh && cd firmware
rm -rf build sdkconfig && idf.py set-target esp32c6 && idf.py build
rm -rf build sdkconfig && idf.py set-target esp32s3 && idf.py build
cd ..
```

Anotar o tamanho do binário antes e depois dos props, da saída do próprio `idf.py build`.

- [ ] **Step 8: Commit**

```bash
git add firmware/props/ firmware/tools/props_to_c.py \
        firmware/main/mascote_pixel_props.c firmware/main/mascote_pixel_props.h \
        firmware/main/mascote_pixel.c firmware/main/CMakeLists.txt sim/CMakeLists.txt
git commit -m "Give the pixel character its adornments, generated from ASCII maps"
```

---

### Task 8: Escolher o personagem na placa

**Files:**
- Modify: `firmware/main/main.c` — ler a chave e escolher antes de `ui_create()`
- Modify: `bridge/provision_wifi.py` — perguntar o personagem
- Modify: `sim/README.md` e `firmware/README.md`

**Interfaces:**
- Consumes: `mascote_por_nome()` e `mascote_escolher()` de `mascote.h`.
- Produces: chave `mascot` no namespace `wisp` da NVS. Ausente, vazia ou desconhecida = `terminal`.

- [ ] **Step 1: Ler a chave no boot**

Em `firmware/main/main.c`, junto da leitura de `token` e `host` (`:656-665`):

```c
    char personagem[16] = "";
    size_t pn = sizeof(personagem);
    if (nvs_get_str(h, "mascot", personagem, &pn) != ESP_OK) personagem[0] = '\0';
```

E antes da chamada de `ui_create()`, que já roda com o mutex na mão (`:374`):

```c
    /* Antes de ui_create(): é o personagem que cria os objetos. */
    mascote_escolher(mascote_por_nome(personagem));
```

`mascote_por_nome()` já loga o aviso quando o nome não existe, e devolve o padrão — a placa nunca fica sem mascote por causa de uma chave errada.

- [ ] **Step 2: Perguntar no provisionamento**

Em `bridge/provision_wifi.py`, na função `ask()`, acrescentar ao dict devolvido:

```python
    # O personagem. Enter aceita o padrão: a maioria não quer escolher, e
    # `terminal` é o que a documentação mostra.
    escolha = _prompt("character (terminal / pixel) [terminal]: ").strip().lower()
    if escolha not in ("terminal", "pixel"):
        escolha = "terminal"
```

`write()` não muda: itera o dict.

Documentar a consequência: como `write()` grava a partição NVS **inteira**, trocar de personagem significa refazer o provisionamento, senha do WiFi incluída. É o preço de ter deixado o canal em runtime fora de escopo, e está anotado no spec.

- [ ] **Step 3: Provar a leitura, no simulador, sem placa**

O simulador já troca de personagem por comando, mas o caminho da NVS não passa por ali. Para exercitá-lo sem regravar, `sim/main.c` lê a variável de ambiente antes de `ui_create()`:

```c
    /* Espelha o que main.c faz com a chave da NVS, para que a tradução
     * nome -> personagem seja exercitada no host e não só na placa. */
    mascote_escolher(mascote_por_nome(getenv("WISP_MASCOT")));
```

```bash
cmake --build sim/build -j
WISP_MASCOT=pixel ./sim/build/wisp-sim       # abre no pixel
WISP_MASCOT=nao-existe ./sim/build/wisp-sim  # avisa e abre no terminal
./sim/build/wisp-sim                         # abre no terminal
```

Esperado no segundo: `W (pixel) personagem "nao-existe" nao existe — usando terminal` e a tela com o computador.

- [ ] **Step 4: Compilar as duas placas e commitar**

```bash
. ~/esp/esp-idf/export.sh && cd firmware
rm -rf build sdkconfig && idf.py set-target esp32c6 && idf.py build && cd ..
git add firmware/main/main.c bridge/provision_wifi.py sim/main.c sim/README.md firmware/README.md
git commit -m "Pick the character from NVS, with the Terminal as the fallback"
```

---

### Task 9: Medir na placa

O simulador não prova custo de render. Esta é a task que pode invalidar o desenho, e por isso ela existe como task e não como observação.

**Files:**
- Modify: `firmware/README.md` — a seção de medidas
- Modify: `CLAUDE.md` — o que ficou verificado em hardware e o que não

- [ ] **Step 1: Gravar com o personagem pixel**

```bash
cd firmware && rm -rf build sdkconfig && idf.py set-target esp32c6 && idf.py build && cd ..
./flash.sh --no-backup      # responder `pixel` na pergunta do personagem
```

Se o esptool recusar: desconectar e reconectar o cabo USB-C — esta placa não tem botão de reset, o cabo é o reset. Não forçar DTR/RTS, que deixa a porta muda.

- [ ] **Step 2: Medir FPS nos três layouts**

```bash
idf.py -C firmware monitor
```

Com uma, duas e quatro sessões ativas, ler as linhas `I (ui) FPS: <n>  (<q> sessao/oes)`. Registrar os três números. Comparar com os mesmos três do Terminal vetorial, regravando com `terminal` — comparação contra o vetorial, não contra um número absoluto, porque é o vetorial que já se sabe aceitável nesta placa.

- [ ] **Step 3: Medir a RAM interna mínima**

No monitor, a linha de heap mínimo. O número histórico do projeto é **4,7KB**; se o pixel com quatro sessões chegar perto disso, o desenho não fecha e a saída é reduzir objetos — menos degraus na silhueta — e não voltar para bitmap.

- [ ] **Step 4: Exercitar o swipe, que é onde props e transformação concorrem**

Com quatro sessões em `tool` (quatro laptops em cena), deslizar repetidamente entre o tile de mascotes e o de limites. Procurar: rastro, meio-quadro, prop que fica para trás. Se aparecer, o suspeito é a transformação do prop sobrevivendo à invalidação parcial — o mesmo sintoma que o projeto irmão descreve como borrão.

- [ ] **Step 5: Escrever o que foi medido**

Em `firmware/README.md`, uma tabela com os seis números (FPS × 3 layouts × 2 personagens) e o heap mínimo. Números reais, com a data. Em `CLAUDE.md`, atualizar a seção de expectativas de verificação: o C6 fica verificado em hardware, o S3 por compilação — não há placa S3 nesta bancada, e dizer o contrário seria mentir na documentação que existe justamente para isso.

- [ ] **Step 6: Commit**

```bash
git add firmware/README.md CLAUDE.md
git commit -m "Record what the pixel character costs on the C6"
```

---

## Notas de execução

**A ordem não é sugestão.** As tasks 1 a 3 constroem a rede de proteção que a Task 4 usa. Fazer a Task 4 antes é possível e é como se perde a única prova de que o Terminal não mudou.

**Uma inversão em relação ao spec, de propósito.** O spec lista a medição na placa antes da seleção por NVS, com o argumento de que a medição é a única coisa que pode invalidar o desenho. Na prática a ordem não fecha: **a seleção por NVS é o que permite gravar a placa com o personagem novo**, então medir antes exigiria trocar o padrão no código só para medir e destrocar depois. A Task 8 vem antes por isso, e o argumento do spec continua valendo em outra forma: a Task 8 não entrega valor nenhum ao usuário sozinha, e nada é anunciado como pronto antes dos números da Task 9.

**Onde parar e pensar.** Se a Task 9 mostrar FPS ruim com quatro sessões, não seguir ajustando: o desenho previu isso como o risco central e a saída prevista é reduzir a contagem de objetos. Se mostrar borrão no swipe, o problema é a transformação do prop e a saída é pré-escalar o prop no gerador em vez de escalar em runtime — o que custa flash e resolve de vez.

**O que não fazer em nenhuma task.** Mexer em `vaga_de()`. Acrescentar `#if CONFIG_IDF_TARGET_*` a `ui.c`, `main.c` ou aos arquivos de personagem. Decodificar imagem em tempo de execução. Editar `mascote_pixel_props.c` à mão.
