/*
 * Wisp — desenho e animação.
 *
 * Três modos na mesma tela (tile 0):
 *   1. mascotes  — um por sessão do Claude, 1 a 4, dividindo o espaço
 *   2. repouso   — relógio + tempo, quando tudo está ocioso há tempo
 *   3. limites   — segunda página, acessível deslizando (tile 1)
 *
 * A rede só define o ESTADO ALVO. Um lv_timer interpola a cada quadro, então
 * nenhuma troca corta. O timer roda dentro da task do LVGL, que JÁ segura o
 * mutex — chamar bsp_display_lock() lá dentro seria deadlock.
 */

#include <string.h>
#include <stdio.h>
#include <math.h>
#include "esp_log.h"
#include "lvgl.h"
#include "bsp/esp-bsp.h"
#include "mascote.h"
#include "ui.h"

static const char *TAG = "ui";





static mascote_t g_m[WISP_MAX_SESSIONS];
static int g_qtd = 1;


/* —— modo repouso —— */
static lv_obj_t *g_hora, *g_dia, *g_temp, *g_cond, *g_maxmin, *g_icone;

/* —— bateria ——
 * Fica FORA da lista de objetos do repouso de proposito: e o unico elemento
 * que vale nos dois modos. Mascote na tela ou relogio na tela, a pergunta
 * "da para ficar sem cabo?" continua valendo. */
static lv_obj_t *g_bateria;
static bool g_em_repouso = false;
static char g_icone_atual[12] = "";

/* —— painel de limites (tile 1) —— */
#define MAX_BARRAS 4
static lv_obj_t *g_tile_painel;
/* O tileview e os tiles em ordem, para os botoes navegarem. Guardar a LISTA,
 * e nao so um indice nosso, porque o dedo tambem troca de tela: um contador
 * proprio dessincronizaria na primeira deslizada. A verdade e sempre o tile
 * ativo no momento da consulta. */
static lv_obj_t *g_tv;
static lv_obj_t *g_telas[2];
#define QTD_TELAS ((int) (sizeof(g_telas) / sizeof(g_telas[0])))
static lv_obj_t *g_bar_rotulo[MAX_BARRAS], *g_bar_pct[MAX_BARRAS];
static lv_obj_t *g_bar[MAX_BARRAS], *g_bar_reset[MAX_BARRAS];
static lv_obj_t *g_bar_ritmo[MAX_BARRAS];   /* veu de ritmo medio sobre a barra */
static lv_obj_t *g_bar_marca[MAX_BARRAS];   /* par de tiques na ponta do veu */
static lv_obj_t *g_card[MAX_BARRAS];
static lv_obj_t *g_frescor;

/* —— medidas do painel de limites ——
 * Cada limite e um cartao: porcentagem grande a esquerda, pilula com o nome a
 * direita, barra grossa e o reset embaixo. Os numeros fecham para caber QUATRO
 * cartoes na tela: 4*88 + 3*8 de respiro = 376, dentro dos 378 que sobram
 * entre o titulo e o rodape. */
#define CARD_X    24
#define CARD_L    432
#define CARD_A    96
#define CARD_GAP  24    /* respiro entre cartoes; cai para CARD_GAP_MIN com quatro */
#define CARD_GAP_MIN 8
#define CARD_PAD  14
#define BARRA_A   14    /* era 8: a barra e o que se le de longe */
#define RITMO_TIQUE_L 2 /* largura do tique opaco na ponta do veu de ritmo */
#define PAINEL_Y0 46    /* primeira linha util, logo abaixo do titulo */
#define PAINEL_Y1 440   /* ultima linha util com rodape na tela */
#define PAINEL_Y1_CHEIO 466  /* ate onde vai quando o rodape sai de cena */

static uint32_t g_refrescos, g_ultima_medida;

/* ————————————————————————————————————————————————
 *  Ícones de tempo, desenhados por primitivas
 * ———————————————————————————————————————————————— */
#define C_SOL    lv_color_make(240, 176,  72)
#define C_LUA    lv_color_make(226, 232, 242)
#define C_NUVEM  lv_color_make(150, 160, 176)
#define C_CHUVA  lv_color_make(104, 162, 214)

/* lv_obj_create devolve o objeto CLICAVEL por padrao, e objeto clicavel
 * captura o arrasto antes que ele chegue ao tileview — que e quem rola para
 * o painel de limites. Como a tela e coberta por corpos, olhos e luzes,
 * bastava um deles clicavel para o deslize morrer. Tudo que e decoracao
 * passa por aqui. */
void so_decoracao(lv_obj_t *o)
{
    lv_obj_remove_flag(o, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(o, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(o, LV_OBJ_FLAG_EVENT_BUBBLE);   /* gesto sobe para o pai */
}

lv_obj_t *disco(lv_obj_t *pai, int d, lv_color_t cor, int x, int y)
{
    lv_obj_t *o = lv_obj_create(pai);
    lv_obj_set_size(o, d, d);
    lv_obj_set_style_radius(o, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_border_width(o, 0, 0);
    lv_obj_set_style_bg_color(o, cor, 0);
    lv_obj_set_style_pad_all(o, 0, 0);
    so_decoracao(o);
    lv_obj_align(o, LV_ALIGN_CENTER, x, y);
    return o;
}

lv_obj_t *barra(lv_obj_t *pai, int w, int h, lv_color_t cor, int x, int y)
{
    lv_obj_t *o = lv_obj_create(pai);
    lv_obj_set_size(o, w, h);
    lv_obj_set_style_radius(o, h < w ? h / 2 : w / 2, 0);
    lv_obj_set_style_border_width(o, 0, 0);
    lv_obj_set_style_bg_color(o, cor, 0);
    lv_obj_set_style_pad_all(o, 0, 0);
    so_decoracao(o);
    lv_obj_align(o, LV_ALIGN_CENTER, x, y);
    return o;
}

static void nuvem(lv_obj_t *p, int dx, int dy, lv_color_t c)
{
    disco(p, 34, c, dx - 16, dy);
    disco(p, 46, c, dx + 4,  dy - 8);
    disco(p, 30, c, dx + 24, dy + 2);
    barra(p, 74, 26, c, dx + 4, dy + 10);
}

static void montar_icone(const char *nome)
{
    lv_obj_clean(g_icone);
    if (!nome || !*nome) return;

    const bool tem_lua = strstr(nome, "moon") != NULL;
    const bool tem_sol = strstr(nome, "sun")  != NULL;

    if (!strcmp(nome, "sun") || !strcmp(nome, "moon")) {
        if (tem_sol) {
            disco(g_icone, 52, C_SOL, 0, 0);
            barra(g_icone, 10, 34, C_SOL,  0, -44);
            barra(g_icone, 10, 34, C_SOL,  0,  44);
            barra(g_icone, 34, 10, C_SOL, -44,  0);
            barra(g_icone, 34, 10, C_SOL,  44,  0);
            barra(g_icone, 10, 22, C_SOL, -31, -31);
            barra(g_icone, 10, 22, C_SOL,  31,  31);
            barra(g_icone, 22, 10, C_SOL,  31, -31);
            barra(g_icone, 22, 10, C_SOL, -31,  31);
        } else {
            /* Crescente: disco cheio + disco preto deslocado. Só funciona
             * porque é AMOLED — o preto tem o pixel desligado e vira recorte
             * de verdade, não uma mancha escura. */
            disco(g_icone, 62, C_LUA, 0, 0);
            disco(g_icone, 54, lv_color_black(), 18, -8);
        }
        return;
    }

    if (tem_sol || tem_lua) {
        if (tem_sol) {
            disco(g_icone, 38, C_SOL, 20, -22);
            barra(g_icone, 8, 20, C_SOL, 20, -50);
            barra(g_icone, 20, 8, C_SOL, 48, -22);
        } else {
            disco(g_icone, 40, C_LUA, 22, -22);
            disco(g_icone, 34, lv_color_black(), 34, -30);
        }
        nuvem(g_icone, -6, 14, C_NUVEM);
        return;
    }

    nuvem(g_icone, 0, !strcmp(nome, "cloud") ? 0 : -12, C_NUVEM);

    if (!strcmp(nome, "rain")) {
        for (int i = 0; i < 3; i++)
            barra(g_icone, 7, 22, C_CHUVA, -24 + i * 24, 34);
    } else if (!strcmp(nome, "snow")) {
        for (int i = 0; i < 3; i++)
            disco(g_icone, 12, C_LUA, -24 + i * 24, 34);
    } else if (!strcmp(nome, "storm")) {
        barra(g_icone, 12, 30, lv_color_make(240, 200, 80), -4, 30);
        barra(g_icone, 12, 22, lv_color_make(240, 200, 80),  8, 42);
    }
}

/* ————————————————————————————————————————————————
 *  Layout: quantos mascotes, de que tamanho, onde
 * ———————————————————————————————————————————————— */
/* Texto EXIBIDO em inglês: as Montserrat do LVGL não têm acento. */
static const char *NOME[WISP_COUNT] = {
    [WISP_IDLE] = "idle",        [WISP_WORKING] = "thinking",
    [WISP_TOOL] = "working", [WISP_ASKING] = "asking you",
    [WISP_WAITING] = "needs you",[WISP_DONE] = "done",
    [WISP_ERROR] = "failed",        [WISP_OFFLINE] = "offline",
};

/* Os mesmos oito estados em português.
 *
 * NENHUMA das oito leva acento, e isso é escolha e não sorte: as Montserrat do
 * LVGL não têm acento — é a restrição que o comentário acima registra — e uma
 * palavra acentuada sairia como quadrado vazio. Foram tiradas da folha de
 * referência do Bytelo, onde a única acentuada era "sem conexão", e "desligado"
 * diz o mesmo.
 *
 * Quem acrescentar palavra com acento aqui vai ver o quadrado, e precisa saber
 * por quê. */
static const char *NOME_PT[WISP_COUNT] = {
    [WISP_IDLE] = "parado",           [WISP_WORKING] = "pensando",
    [WISP_TOOL] = "trabalhando",      [WISP_ASKING]  = "perguntando",
    [WISP_WAITING] = "pedindo ajuda", [WISP_DONE]    = "pronto",
    [WISP_ERROR] = "falhou",          [WISP_OFFLINE] = "desligado",
};

/* Os ajustes em vigor. Os padrões são o comportamento de antes de haver ajuste. */
static wisp_cfg_t g_cfg = {.acao = true, .projetos = true, .pt = false, .tamanho = 1};

typedef struct { int16_t d, x, y; const lv_font_t *f_det, *f_proj; } vaga_t;

/* Uma sessão ocupa a tela toda; a partir de duas, divide.
 * 3 e 4 usam a mesma grade 2x2 — com 3, a última vaga fica vazia, que é
 * melhor do que uma fileira de três achatados. */
static void vaga_de(int total, int i, vaga_t *v)
{
    if (total <= 1) {
        /* 306px, e a conta vertical fecha justa — por isso esta escrita.
         *
         * A arte em assets/ tem exatamente esse tamanho, e os dois numeros andam
         * juntos: o objeto da foto recebe v.d como tamanho, e imagem MAIOR que o
         * objeto sai CORTADA, nao reduzida. Mexer em um sem o outro corta o
         * mascote ou deixa moldura vazia em volta dele.
         *
         * Centrado em 190 (y=-50) ocupa 37..343; o detalhe cai em 369 e a lista
         * de sessoes comeca em 395 — quatro linhas de 19 terminam em 471, com 9px
         * de sobra. Os 37px de folga no topo sao deliberados: a moldura come a
         * beirada do vidro, mais ainda nos cantos arredondados.
         *
         * Vale para as duas placas: a tela e a mesma 480x480 nas duas. */
        *v = (vaga_t){306, 0, -50, &lv_font_montserrat_32, &lv_font_montserrat_20};
    } else if (total == 2) {
        *v = (vaga_t){178, (i == 0 ? -118 : 118), -10,
                      &lv_font_montserrat_24, &lv_font_montserrat_16};
    } else {
        const int16_t px[4] = {-118, 118, -118, 118};
        const int16_t py[4] = {-118, -118, 108, 108};
        *v = (vaga_t){140, px[i], py[i],
                      &lv_font_montserrat_20, &lv_font_montserrat_16};
    }
}

/* O layout decide ONDE e DE QUE TAMANHO. O personagem decide a forma.
 *
 * Antes desta separação esta função conhecia carcaça, tela, moldura e braço —
 * e por isso um segundo personagem exigiria um segundo layout. */
static void aplicar_layout(int total)
{
    for (int i = 0; i < WISP_MAX_SESSIONS; i++) {
        mascote_t *m = &g_m[i];
        bool ativo = i < total;

        /* O personagem SUGERE, o ajuste MANDA. Um personagem cuja composição é
         * uma cara sozinha no quadro (`rotulos` false) some com os dois por
         * padrão; quem quiser um deles de volta pede no painel.
         *
         * Dois independentes, e não um: eles já são dois objetos, e querer a
         * ação sem a lista de projetos é pedido legítimo. */
        const bool sugere = mascote_ativo()->rotulos;
        const bool ver_acao = ativo && sugere && g_cfg.acao;
        const bool ver_proj = ativo && sugere && g_cfg.projetos;
        if (m->detail) {
            if (ver_acao) lv_obj_remove_flag(m->detail, LV_OBJ_FLAG_HIDDEN);
            else          lv_obj_add_flag(m->detail, LV_OBJ_FLAG_HIDDEN);
        }
        if (m->project) {
            if (ver_proj) lv_obj_remove_flag(m->project, LV_OBJ_FLAG_HIDDEN);
            else          lv_obj_add_flag(m->project, LV_OBJ_FLAG_HIDDEN);
        }

        vaga_t v; vaga_de(total, i, &v);

        /* Sem rótulos, o deslocamento vertical que abria espaço para eles perde
         * a razão de ser: com uma sessão, o mascote volta ao centro da tela em
         * vez de ficar alto com o vazio embaixo.
         *
         * O ajuste é AQUI, e não no personagem: quem sabe por que o offset
         * existia é o layout. vaga_de() fica intocada. */
        int16_t cy = v.y;
        if (!ver_acao && !ver_proj && total <= 1) cy = 0;

        /* x e y ficam guardados porque a animação do personagem precisa deles
         * para o que orbita o corpo, e chamar vaga_de() de lá seria o layout
         * atravessando a fronteira na direção errada. */
        m->d = v.d; m->x = v.x; m->y = cy;
        mascote_ativo()->dispor(m, v.d, v.x, cy, ativo, g_cfg.tamanho);
        if (!ativo) continue;

    lv_obj_set_style_text_font(m->detail, v.f_det, 0);
        lv_obj_align(m->detail, LV_ALIGN_CENTER, v.x, v.y + v.d / 2 + 26);
        /* O rotulo de projeto e uma LISTA: uma linha por sessao, ate quatro.
         *
         * Ancorada pelo TOPO, nao pelo centro. Com LV_ALIGN_CENTER um label que
         * cresce se abre para os dois lados, e a primeira linha sobe ate encostar
         * no detalhe. Do topo, a lista cresce para baixo, que e onde sobra
         * espaco.
         *
         * A fonte comeca em 24 e quem manda nela e a QUANTIDADE de sessoes, no
         * update — ver a nota la. Aqui fica o caso de uma sessao, que e o comum.
         *
         * line_space -1 comprime as linhas de 20 para 19px. Parece detalhe e e o
         * que garante a quarta linha dentro da tela. */
        lv_obj_set_style_text_font(m->project, &lv_font_montserrat_24, 0);
        lv_obj_set_style_text_align(m->project, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_style_text_line_space(m->project, -1, 0);
        lv_obj_align(m->project, LV_ALIGN_TOP_MID, v.x, 240 + v.y + v.d / 2 + 52);

    }
}

void ui_configurar(const wisp_cfg_t *c)
{
    if (!c) return;
    if (c->acao == g_cfg.acao && c->projetos == g_cfg.projetos
        && c->pt == g_cfg.pt && c->tamanho == g_cfg.tamanho) return;

    bsp_display_lock(-1);
    g_cfg = *c;
    /* Reaplicar o layout é o que faz os rótulos aparecerem ou sumirem, o mascote
     * recentrar e o tamanho valer. O idioma entra no ui_update() que vem logo
     * depois, quando o texto é reescrito. */
    g_qtd = -1;
    bsp_display_unlock();
    ESP_LOGI(TAG, "ajustes: acao=%d projetos=%d pt=%d tamanho=%u",
             c->acao, c->projetos, c->pt, (unsigned) c->tamanho);
}

void ui_personagem(const char *nome)
{
    if (!nome || !*nome) return;

    const personagem_t *novo = mascote_por_nome(nome);
    if (novo == mascote_ativo()) return;

    bsp_display_lock(-1);

    /* A ordem importa: destruir TODOS antes de trocar o ativo, porque é o
     * personagem ANTIGO que sabe como desfazer o que ele fez. */
    for (int i = 0; i < WISP_MAX_SESSIONS; i++)
        if (mascote_ativo()->destruir) mascote_ativo()->destruir(&g_m[i]);

    mascote_escolher(novo);

    for (int i = 0; i < WISP_MAX_SESSIONS; i++)
        novo->criar(g_telas[0], &g_m[i]);

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

wisp_state_t ui_state_from_text(const char *s)
{
    if (!s) return WISP_IDLE;
    if (!strcmp(s, "working")) return WISP_WORKING;
    if (!strcmp(s, "tool"))    return WISP_TOOL;
    if (!strcmp(s, "asking"))  return WISP_ASKING;
    if (!strcmp(s, "waiting")) return WISP_WAITING;
    if (!strcmp(s, "done"))    return WISP_DONE;
    if (!strcmp(s, "error"))   return WISP_ERROR;
    return WISP_IDLE;
}


static void ao_refrescar(lv_event_t *e) { (void) e; g_refrescos++; }


static void animar(lv_timer_t *t)
{
    (void) t;
    if (g_em_repouso) return;   /* mascotes escondidos: animar é desperdício */

    const uint32_t agora = lv_tick_get();
    for (int i = 0; i < g_qtd && i < WISP_MAX_SESSIONS; i++)
        mascote_ativo()->animar(&g_m[i], agora, g_qtd == 1);

    if (agora - g_ultima_medida >= 5000) {
        ESP_LOGI(TAG, "FPS: %lu  (%d sessao/oes)",
                 (unsigned long)(g_refrescos * 1000 / (agora - g_ultima_medida)), g_qtd);
        g_refrescos = 0;
        g_ultima_medida = agora;
    }
}

/* ————————————————————————————————————————————————
 *  Construção
 * ———————————————————————————————————————————————— */
/* Cor pela FAIXA de uso, nao pelo campo `severity` do bridge.
 *
 * Cinco faixas de 20 pontos, percorrendo o espectro de frio para quente. A
 * leitura pretendida e periferica: da para saber onde se esta sem ler o numero,
 * e a passagem de amarelo para laranja marca a metade da segunda metade — o
 * ponto em que ainda da tempo de mudar de plano.
 *
 * O `severity` continua chegando do bridge e nao decide cor aqui. Quem diz qual
 * limite esta VALENDO e o campo `active`, e essa e outra pergunta — respondida
 * pela pilula acesa, nao pela cor da barra. */
static lv_color_t cor_do_pct(int pct)
{
    if (pct > 80) return lv_color_make(226,  74,  62);   /* vermelho        81-100 */
    if (pct > 60) return lv_color_make(240, 146,  58);   /* laranja          61-80 */
    if (pct > 40) return lv_color_make(238, 206,  70);   /* amarelo          41-60 */
    if (pct > 20) return lv_color_make(166, 214,  86);   /* verde-amarelado  21-40 */
    return lv_color_make(76, 200, 176);                  /* verde-azulado     0-20 */
}

/* Empilha os cartoes CENTRADOS no espaco util.
 *
 * Chamado quando a quantidade de limites muda, e nao a cada consulta: com tres
 * limites (o caso comum — sessao de 5h, semana, semana Fable) um bloco ancorado
 * no topo deixaria um vazio grande sobre o rodape. Centrar custa uma conta.
 *
 * COM QUATRO LIMITES A CONTA NAO FECHA com o respiro cheio, e a saida esta
 * escrita aqui em vez de escondida num numero: o respiro cai para 8px e o
 * rodape de frescor sai de cena para o quarto cartao entrar. Some a informacao
 * menos urgente da tela (ha quanto tempo os numeros foram lidos) em favor de
 * nao esconder um limite inteiro. Quatro cartoes de 96 mais tres de 8 dao 408,
 * dentro dos 420 que sobram sem o rodape. */
static void posicionar_cards(int quantos)
{
    if (quantos < 1) quantos = 1;
    if (quantos > MAX_BARRAS) quantos = MAX_BARRAS;

    const bool apertado = (quantos >= MAX_BARRAS);
    const int gap = apertado ? CARD_GAP_MIN : CARD_GAP;
    const int y1  = apertado ? PAINEL_Y1_CHEIO : PAINEL_Y1;

    if (g_frescor) {
        if (apertado) lv_obj_add_flag(g_frescor, LV_OBJ_FLAG_HIDDEN);
        else          lv_obj_remove_flag(g_frescor, LV_OBJ_FLAG_HIDDEN);
    }

    const int alt = quantos * CARD_A + (quantos - 1) * gap;
    int y = PAINEL_Y0 + ((y1 - PAINEL_Y0) - alt) / 2;
    if (y < PAINEL_Y0) y = PAINEL_Y0;
    for (int i = 0; i < MAX_BARRAS; i++) {
        lv_obj_align(g_card[i], LV_ALIGN_TOP_LEFT, CARD_X, y + i * (CARD_A + gap));
    }
}

/* Estica o veu por `pct` da trilha e encosta a marca na ponta dele.
 *
 * Esconde os dois quando nao ha marca (-1, que o bridge manda para janela
 * expirada ou sem tamanho conhecido) e quando a marca nao chega a um tique de
 * largura — ali o "veu" seria so o tique sentado na ponta arredondada da barra.
 *
 * A marca comeca um tique ANTES da borda do veu, para o branco cair sobre o
 * veu e o preto imediatamente fora dele. Perto de 100% o preto sai da trilha e
 * o LVGL o recorta; o branco, que e o que marca a posicao, continua visivel. */
static void posicionar_ritmo(lv_obj_t *veu, lv_obj_t *marca, int pct)
{
    if (!veu || !marca) return;
    if (pct < 0 || pct > 100) {
        lv_obj_add_flag(veu, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(marca, LV_OBJ_FLAG_HIDDEN);
        return;
    }
    const int trilha = CARD_L - 2 * CARD_PAD;
    const int l = (trilha * pct + 50) / 100;
    if (l < RITMO_TIQUE_L) {
        lv_obj_add_flag(veu, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(marca, LV_OBJ_FLAG_HIDDEN);
        return;
    }
    lv_obj_set_width(veu, l);
    lv_obj_set_pos(marca, l - RITMO_TIQUE_L, 0);
    lv_obj_remove_flag(veu, LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_flag(marca, LV_OBJ_FLAG_HIDDEN);
}

static void criar_painel(lv_obj_t *pai)
{
    lv_obj_t *titulo = lv_label_create(pai);
    lv_label_set_text(titulo, "USAGE LIMITS");
    lv_obj_set_style_text_font(titulo, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(titulo, lv_color_make(150, 158, 172), 0);
    lv_obj_align(titulo, LV_ALIGN_TOP_MID, 0, 16);

    for (int i = 0; i < MAX_BARRAS; i++) {
        /* O CARTAO.
         *
         * lv_obj_create nasce com fundo, borda e padding do tema, e com rolagem
         * propria — tudo zerado aqui. O so_decoracao e o que mantem o DESLIZE
         * funcionando: sem o EVENT_BUBBLE o cartao engole o arrasto e o
         * tileview nunca recebe o gesto, porque os cartoes cobrem justamente a
         * faixa da tela onde o dedo passa. */
        lv_obj_t *card = lv_obj_create(pai);
        lv_obj_set_size(card, CARD_L, CARD_A);
        lv_obj_set_style_bg_color(card, lv_color_make(24, 24, 30), 0);
        lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
        lv_obj_set_style_radius(card, 16, 0);
        lv_obj_set_style_border_width(card, 0, 0);
        lv_obj_set_style_pad_all(card, 0, 0);
        lv_obj_set_scrollbar_mode(card, LV_SCROLLBAR_MODE_OFF);
        so_decoracao(card);
        g_card[i] = card;

        /* Porcentagem: o maior elemento do cartao, no canto de leitura. */
        g_bar_pct[i] = lv_label_create(card);
        lv_obj_set_style_text_font(g_bar_pct[i], &lv_font_montserrat_32, 0);
        lv_obj_set_style_text_color(g_bar_pct[i], lv_color_make(238, 242, 248), 0);
        lv_obj_align(g_bar_pct[i], LV_ALIGN_TOP_LEFT, CARD_PAD, 4);
        lv_label_set_text(g_bar_pct[i], "");
        so_decoracao(g_bar_pct[i]);

        /* Nome do limite como PILULA: fundo no proprio label, com padding e
         * raio. Um container separado daria o mesmo desenho e um objeto a mais
         * por cartao — e objeto a mais aqui e area a mais para redesenhar. */
        g_bar_rotulo[i] = lv_label_create(card);
        lv_obj_set_style_text_font(g_bar_rotulo[i], &lv_font_montserrat_16, 0);
        lv_obj_set_style_text_color(g_bar_rotulo[i], lv_color_make(228, 232, 240), 0);
        lv_obj_set_style_bg_color(g_bar_rotulo[i], lv_color_make(86, 78, 128), 0);
        lv_obj_set_style_bg_opa(g_bar_rotulo[i], LV_OPA_COVER, 0);
        lv_obj_set_style_radius(g_bar_rotulo[i], LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_pad_hor(g_bar_rotulo[i], 12, 0);
        lv_obj_set_style_pad_ver(g_bar_rotulo[i], 5, 0);
        lv_obj_align(g_bar_rotulo[i], LV_ALIGN_TOP_RIGHT, -CARD_PAD, 14);
        lv_label_set_text(g_bar_rotulo[i], "");
        so_decoracao(g_bar_rotulo[i]);

        g_bar[i] = lv_bar_create(card);
        lv_obj_set_size(g_bar[i], CARD_L - 2 * CARD_PAD, BARRA_A);
        lv_obj_align(g_bar[i], LV_ALIGN_TOP_LEFT, CARD_PAD, 46);
        lv_bar_set_range(g_bar[i], 0, 100);
        lv_obj_set_style_bg_color(g_bar[i], lv_color_make(62, 56, 88), 0);
        lv_obj_set_style_radius(g_bar[i], BARRA_A / 2, 0);
        lv_obj_set_style_radius(g_bar[i], BARRA_A / 2, LV_PART_INDICATOR);
        so_decoracao(g_bar[i]);

        /* O VEU DE RITMO.
         *
         * Termina onde o preenchimento estaria se a janela tivesse sido gasta
         * em ritmo constante. Le-se um contra o outro: veu sobrando a direita
         * do preenchimento e folga, cor saturada sobrando a direita do veu e
         * excesso. Só um dos dois pode existir por vez.
         *
         * Deliberadamente neutro, e nao vermelho: o cor_do_pct ja gasta o
         * vermelho no preenchimento acima de 80%, e o ritmo medio e uma
         * referencia, nao um alerta.
         *
         * O clip_corner e obrigatorio — a barra tem raio BARRA_A/2, ou seja e
         * uma pilula, e sem ele os cantos quadrados do veu vazariam para fora
         * das pontas. Arredondar o veu seria pior: amaciaria justamente a borda
         * direita que carrega a leitura. */
        lv_obj_set_style_clip_corner(g_bar[i], true, LV_PART_MAIN);
        g_bar_ritmo[i] = lv_obj_create(g_bar[i]);
        lv_obj_set_pos(g_bar_ritmo[i], 0, 0);
        lv_obj_set_size(g_bar_ritmo[i], 0, LV_PCT(100));
        lv_obj_set_style_bg_color(g_bar_ritmo[i], lv_color_make(238, 242, 248), 0);
        lv_obj_set_style_bg_opa(g_bar_ritmo[i], LV_OPA_20, 0);
        lv_obj_set_style_radius(g_bar_ritmo[i], 0, 0);
        lv_obj_set_style_border_width(g_bar_ritmo[i], 0, 0);
        lv_obj_set_style_pad_all(g_bar_ritmo[i], 0, 0);
        lv_obj_add_flag(g_bar_ritmo[i], LV_OBJ_FLAG_HIDDEN);
        so_decoracao(g_bar_ritmo[i]);

        /* A MARCA: dois tiques da mesma largura e altura, branco e preto,
         * lado a lado na ponta do veu. A area do veu da a magnitude, a marca da
         * o ponto exato.
         *
         * O preto existe porque o branco sozinho DESAPARECE sobre os
         * preenchimentos claros que o cor_do_pct produz na faixa amarela. Duas
         * tintas opostas encostadas garantem que uma das duas contraste com o
         * que estiver embaixo, qualquer que seja.
         *
         * E um objeto preto de largura dupla com o branco alinhado a esquerda
         * dentro dele, e nao dois objetos posicionados: assim o par anda com
         * uma conta so, e o branco fica colado no preto por construcao.
         *
         * Irmao do veu e nao filho: o preto cai FORA da largura do veu, e o
         * LVGL recorta filho a area do pai (e o que o LV_OBJ_FLAG_OVERFLOW_VISIBLE
         * existe para desligar), entao dentro do veu ele nunca apareceria. */
        g_bar_marca[i] = lv_obj_create(g_bar[i]);
        lv_obj_set_size(g_bar_marca[i], 2 * RITMO_TIQUE_L, LV_PCT(100));
        lv_obj_set_style_bg_color(g_bar_marca[i], lv_color_make(0, 0, 0), 0);
        lv_obj_set_style_bg_opa(g_bar_marca[i], LV_OPA_COVER, 0);
        lv_obj_set_style_radius(g_bar_marca[i], 0, 0);
        lv_obj_set_style_border_width(g_bar_marca[i], 0, 0);
        lv_obj_set_style_pad_all(g_bar_marca[i], 0, 0);
        lv_obj_add_flag(g_bar_marca[i], LV_OBJ_FLAG_HIDDEN);
        so_decoracao(g_bar_marca[i]);

        lv_obj_t *tique = lv_obj_create(g_bar_marca[i]);
        lv_obj_set_size(tique, RITMO_TIQUE_L, LV_PCT(100));
        lv_obj_align(tique, LV_ALIGN_LEFT_MID, 0, 0);
        lv_obj_set_style_bg_color(tique, lv_color_make(238, 242, 248), 0);
        lv_obj_set_style_bg_opa(tique, LV_OPA_COVER, 0);
        lv_obj_set_style_radius(tique, 0, 0);
        lv_obj_set_style_border_width(tique, 0, 0);
        so_decoracao(tique);

        g_bar_reset[i] = lv_label_create(card);
        lv_obj_set_style_text_font(g_bar_reset[i], &lv_font_montserrat_20, 0);
        lv_obj_set_style_text_color(g_bar_reset[i], lv_color_make(140, 150, 164), 0);
        lv_obj_align(g_bar_reset[i], LV_ALIGN_TOP_LEFT, CARD_PAD, 66);
        lv_label_set_text(g_bar_reset[i], "");
        so_decoracao(g_bar_reset[i]);
    }
    /* Antes do posicionar_cards, que decide se ele aparece. */
    g_frescor = lv_label_create(pai);
    lv_obj_set_style_text_font(g_frescor, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(g_frescor, lv_color_make(120, 128, 140), 0);
    lv_obj_align(g_frescor, LV_ALIGN_BOTTOM_MID, 0, -14);
    lv_label_set_text(g_frescor, "");

    posicionar_cards(MAX_BARRAS);
}


/* Os rótulos são do LAYOUT, não do personagem: qualquer personagem tem um
 * detalhe e uma lista de projetos abaixo dele, e são as mesmas duas linhas de
 * texto nos dois casos. */
static void criar_rotulos(lv_obj_t *pai, mascote_t *m)
{
    m->detail = lv_label_create(pai);
    lv_obj_set_style_text_color(m->detail, lv_color_make(230, 233, 238), 0);
    lv_label_set_text(m->detail, "");

    m->project = lv_label_create(pai);
    lv_obj_set_style_text_color(m->project, lv_color_make(134, 144, 158), 0);
    lv_label_set_text(m->project, "");

    m->alvo = WISP_OFFLINE;
    m->anterior = WISP_COUNT;
    m->d = 236;
}

void ui_create(void)
{
    lv_obj_t *raiz = lv_screen_active();
    lv_obj_set_style_bg_color(raiz, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(raiz, LV_OPA_COVER, 0);
    lv_obj_remove_flag(raiz, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *tv = lv_tileview_create(raiz);
    lv_obj_set_size(tv, 480, 480);
    lv_obj_set_style_bg_color(tv, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(tv, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(tv, 0, 0);
    lv_obj_set_scrollbar_mode(tv, LV_SCROLLBAR_MODE_OFF);

    lv_obj_t *tela = lv_tileview_add_tile(tv, 0, 0, LV_DIR_RIGHT);
    g_tile_painel  = lv_tileview_add_tile(tv, 1, 0, LV_DIR_LEFT);
    g_tv = tv;
    g_telas[0] = tela;
    g_telas[1] = g_tile_painel;
    lv_obj_t *tiles[2] = {tela, g_tile_painel};
    for (int i = 0; i < 2; i++) {
        lv_obj_set_style_bg_color(tiles[i], lv_color_black(), 0);
        lv_obj_set_style_bg_opa(tiles[i], LV_OPA_COVER, 0);
        lv_obj_remove_flag(tiles[i], LV_OBJ_FLAG_SCROLLABLE);
    }
    criar_painel(g_tile_painel);

    for (int i = 0; i < WISP_MAX_SESSIONS; i++) {
        mascote_ativo()->criar(tela, &g_m[i]);
        criar_rotulos(tela, &g_m[i]);
    }


    /* —— bateria ——
     * Canto superior DIREITO. Os dois cantos de cima ficam livres nos tres
     * layouts — a grade de quatro mascotes para a 52px da borda e o relogio
     * comeca em y=84 — entao a escolha e de gosto, nao de espaco.
     *
     * Margem de 40px, nao 16: com 16 o raio de carregando encostava na borda
     * e sumia. O painel tem 480px de vidro mas nem todo ele se ve — a moldura
     * come a beirada, e mais ainda nos cantos, que sao arredondados. 40 e a
     * mesma folga com que a grade de mascotes para da borda (52px), e da o
     * mesmo respiro nos dois eixos para o canto redondo nao morder nada. */
    g_bateria = lv_label_create(tela);
    lv_obj_set_style_text_font(g_bateria, &lv_font_montserrat_20, 0);
    lv_label_set_text(g_bateria, "");
    lv_obj_align(g_bateria, LV_ALIGN_TOP_RIGHT, -40, 26);

    /* —— repouso —— */
    g_hora = lv_label_create(tela);
    lv_obj_set_style_text_font(g_hora, &lv_font_montserrat_48, 0);
    lv_obj_set_style_text_color(g_hora, lv_color_make(238, 242, 248), 0);
    lv_label_set_text(g_hora, "--:--");
    lv_obj_align(g_hora, LV_ALIGN_CENTER, 0, -156);

    g_dia = lv_label_create(tela);
    lv_obj_set_style_text_font(g_dia, &lv_font_montserrat_28, 0);
    lv_obj_set_style_text_color(g_dia, lv_color_make(140, 150, 164), 0);
    lv_label_set_text(g_dia, "");
    lv_obj_align(g_dia, LV_ALIGN_CENTER, 0, -108);

    g_icone = lv_obj_create(tela);
    lv_obj_set_size(g_icone, 150, 150);
    lv_obj_set_style_bg_opa(g_icone, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(g_icone, 0, 0);
    lv_obj_set_style_pad_all(g_icone, 0, 0);
    so_decoracao(g_icone);
    lv_obj_align(g_icone, LV_ALIGN_CENTER, 0, -6);

    g_temp = lv_label_create(tela);
    lv_obj_set_style_text_font(g_temp, &lv_font_montserrat_48, 0);
    lv_obj_set_style_text_color(g_temp, lv_color_make(232, 132, 90), 0);
    lv_label_set_text(g_temp, "");
    /* 48 é a MAIOR Montserrat embutida no LVGL. Para passar disso sem gerar
     * fonte customizada, escalamos o rótulo. Aqui o transform é barato: a
     * temperatura muda a cada 15 min, não a cada quadro. */
    lv_obj_set_style_transform_pivot_x(g_temp, lv_pct(50), 0);
    lv_obj_set_style_transform_pivot_y(g_temp, lv_pct(50), 0);
    lv_obj_set_style_transform_scale_x(g_temp, 333, 0);   /* 256 = 100% */
    lv_obj_set_style_transform_scale_y(g_temp, 333, 0);
    lv_obj_align(g_temp, LV_ALIGN_CENTER, 0, 100);

    g_cond = lv_label_create(tela);
    lv_obj_set_style_text_font(g_cond, &lv_font_montserrat_28, 0);
    lv_obj_set_style_text_color(g_cond, lv_color_make(184, 192, 204), 0);
    lv_label_set_text(g_cond, "");
    lv_obj_align(g_cond, LV_ALIGN_CENTER, 0, 148);

    g_maxmin = lv_label_create(tela);
    lv_obj_set_style_text_font(g_maxmin, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(g_maxmin, lv_color_make(122, 130, 142), 0);
    lv_label_set_text(g_maxmin, "");
    lv_obj_align(g_maxmin, LV_ALIGN_CENTER, 0, 188);

    lv_obj_t *r[] = {g_hora, g_dia, g_icone, g_temp, g_cond, g_maxmin};
    for (size_t i = 0; i < sizeof(r) / sizeof(r[0]); i++)
        lv_obj_add_flag(r[i], LV_OBJ_FLAG_HIDDEN);

    aplicar_layout(1);
    g_ultima_medida = lv_tick_get();
    lv_display_add_event_cb(lv_display_get_default(), ao_refrescar,
                            LV_EVENT_REFR_READY, NULL);
    lv_timer_create(animar, PERIODO_MS, NULL);
}

/* ————————————————————————————————————————————————
 *  Atualização
 * ———————————————————————————————————————————————— */
void ui_update(const wisp_data_t *d)
{
    if (!d) return;
    int n = d->session_count;
    if (n > WISP_MAX_SESSIONS) n = WISP_MAX_SESSIONS;

    /* Relogio so depois de repouso_s de silencio — nao assim que a lista de
     * sessoes esvazia.
     *
     * Este campo existia e NAO era usado: o bridge mandava `rest` desde
     * sempre, a placa parseava para d->rest_s, e a decisao aqui olhava
     * apenas "a lista esta vazia?". Como o bridge tira a sessao da lista
     * depois de 30s parada, o limiar de verdade era 30 segundos, e mexer em
     * REPOUSO_S do lado do bridge nao mudava nada na tela.
     *
     * No intervalo entre a sessao sair da lista e o repouso comecar, cai no
     * mascote ocioso generico logo abaixo — que e a leitura certa: nao ha
     * sessao ativa para nomear, mas tambem ainda nao e hora de desistir dela.
     *
     * idade_s < 0 = nenhuma sessao conhecida desde que o bridge subiu. Ai o
     * relogio entra direto: nao ha trabalho para esperar. */
    bool sem_sessao = (n == 0);
    bool ocioso_bastante = (d->age_s < 0 || d->age_s >= d->rest_s);
    bool repouso = sem_sessao && d->clock[0] && d->rest_s > 0 && ocioso_bastante;

    /* UM mascote, sempre — mesmo com varias sessoes.
     *
     * Dividir a tela em dois ou quatro parecia obvio e nao funciona: a
     * imagem encolhe para caber na vaga e sai CORTADA, porque o PNG tem
     * proporcao propria e a vaga nao. E quatro bonecos espremidos sao
     * quatro caras identicas em miniatura — nenhuma legivel de longe, que
     * e a unica coisa que esta tela precisa fazer.
     *
     * A sessao mais urgente e quem aparece; a lista abaixo do rotulo nomeia
     * TODAS as que estao rodando. Mesma decisao do flutuante no Mac. */
    n = 1;

    bsp_display_lock(-1);

    /* Bateria: nao depende de modo, layout nem sessao — mas depende do MUTEX.
     *
     * Este bloco ja esteve no topo da funcao, antes do bsp_display_lock(), e
     * aquilo travava a placa. Mexer em objeto do LVGL fora do mutex corre com
     * a task de render: o watchdog pegou a task de rede presa dentro de
     * lv_inv_area(), andando numa lista de invalidacao corrompida. Na tela o
     * sintoma era enganoso — a placa conectava, reportava UMA vez e emudecia,
     * o que parece problema de rede e nao e.
     *
     * Sem medida o rotulo fica VAZIO em vez de "--%". Placa no cabo, sem
     * celula instalada, e uma configuracao legitima; anunciar ignorancia ali
     * seria ruido permanente para quem nunca vai usar bateria.
     *
     * A cor e o aviso. Cinza e o mesmo tom da data no relogio, ou seja,
     * informacao de fundo que nao compete com o mascote. Abaixo de 20% vira
     * o mesmo ambar da temperatura, que ja e a cor de "olhe para mim" nesta
     * tela — nao inventamos um vermelho novo so para isto. */
    if (d->battery_pct < 0) {
        lv_label_set_text(g_bateria, "");
    } else {
        bool baixa = d->battery_pct <= 20 && !d->battery_charging;
        lv_obj_set_style_text_color(g_bateria,
            baixa ? lv_color_make(232, 132, 90) : lv_color_make(140, 150, 164), 0);
        lv_label_set_text_fmt(g_bateria, "%d%%%s", d->battery_pct,
                              d->battery_charging ? " " LV_SYMBOL_CHARGE : "");
    }

    /* Toda troca de arranjo repinta a tela inteira.
     *
     * POR QUE, em uma linha: esconder objeto no LVGL não apaga pixel — apenas
     * marca a área como suja para que ALGUÉM redesenhe por cima. Rodamos em
     * modo PARCIAL (buffer de 16 linhas, imposto pela disputa de RAM com o
     * WiFi), e nesse modo uma invalidação perdida não tem segunda chance: o
     * quadro seguinte só toca nas áreas sujas. O pixel antigo fica.
     *
     * Num LCD isso apareceria como borrão; num AMOLED os pixels são luz
     * própria e a sobra fica nítida, indistinguível de conteúdo válido. Foi o
     * que se viu: relógio, painel e mascote empilhados na mesma tela, todos
     * legíveis, todos restos de arranjos anteriores.
     *
     * O custo é um quadro cheio por TROCA — não por quadro. Trocar de modo
     * acontece na casa de segundos; redesenhar 480x480 uma vez é irrelevante
     * perto de conviver com lixo permanente na tela. */
    bool trocou_modo   = (repouso != g_em_repouso);
    bool trocou_layout = (!repouso && n != g_qtd);
    if (trocou_modo || trocou_layout) {
        lv_obj_invalidate(lv_screen_active());
    }

    if (repouso != g_em_repouso) {
        g_em_repouso = repouso;
        lv_obj_t *r[] = {g_hora, g_dia, g_icone, g_temp, g_cond, g_maxmin};
        for (size_t i = 0; i < sizeof(r) / sizeof(r[0]); i++) {
            if (repouso) lv_obj_remove_flag(r[i], LV_OBJ_FLAG_HIDDEN);
            else         lv_obj_add_flag(r[i], LV_OBJ_FLAG_HIDDEN);
        }
        if (repouso) {
            for (int i = 0; i < WISP_MAX_SESSIONS; i++) {
                /* dispor(mostrar=false) esconde TUDO o que é do personagem,
                 * inclusive o que é irmão do corpo e não some por herança — a
                 * foto e a chama ficavam pairando sobre o relógio, armadilha
                 * que já pegou o rodapé antes delas. Os rótulos são do layout,
                 * então saem daqui. */
                mascote_t *m = &g_m[i];
                mascote_ativo()->dispor(m, m->d, m->x, m->y, false, g_cfg.tamanho);
                lv_obj_t *rot[] = {m->detail, m->project};
                for (size_t k = 0; k < 2; k++)
                    if (rot[k]) lv_obj_add_flag(rot[k], LV_OBJ_FLAG_HIDDEN);
            }
        } else {
            g_qtd = -1;   /* força reaplicar o layout ao voltar */
        }
    }

    if (!repouso) {
        if (n != g_qtd) {
            g_qtd = n;
            aplicar_layout(n);
        }
        /* Sem sessão e sem relógio (o bridge caiu antes de mandar a hora):
         * mostramos um mascote ocioso EXPLÍCITO. Ler d->sessions[0] aqui
         * pegaria lixo — quem chama passa um global que guarda a última
         * sessão vista, e a tela exibiria um fantasma de algo já encerrado. */
        static const wisp_session_t VAZIA = {.state = WISP_IDLE};
        /* Prioridade: quem PAROU esperando voce fala primeiro. Trabalhando
         * pode aguardar; travado, nao. */
        static const wisp_state_t URGENCIA[] = {
            WISP_ASKING, WISP_WAITING, WISP_ERROR,
            WISP_TOOL, WISP_WORKING, WISP_DONE,
        };
        int esc = 0;
        for (size_t u = 0; u < sizeof(URGENCIA)/sizeof(URGENCIA[0]); u++) {
            bool achou = false;
            for (int k = 0; k < d->session_count; k++)
                if (d->sessions[k].state == URGENCIA[u]) { esc = k; achou = true; break; }
            if (achou) break;
        }
        for (int i = 0; i < n; i++) {
            const wisp_session_t *s = sem_sessao ? &VAZIA : &d->sessions[esc];
            mascote_t *m = &g_m[i];
            m->alvo = s->state;

            /* EM PORTUGUÊS, O ESTADO GANHA DO DETALHE.
             *
             * Em inglês a regra é a de sempre: o detalhe quando existe, o nome
             * do estado quando não — e o detalhe é mais informativo, porque diz
             * QUAL ferramenta está rodando.
             *
             * Em português não dá para manter isso: o detalhe vem do bridge e
             * não é traduzível. São nomes próprios de ferramenta ("Bash",
             * "Read", "Edit") e frases geradas em inglês ("approve plan"). Um
             * rótulo que diz "Bash" com o resto da tela em português não está em
             * português; está pela metade.
             *
             * O custo é real e está assumido: em português a tela diz o que o
             * Claude está FAZENDO e não com o quê. Quem quer a ferramenta usa
             * inglês. */
            const char *txt = g_cfg.pt
                            ? NOME_PT[s->state]
                            : (s->detail[0] ? s->detail : NOME[s->state]);
            if (strncmp(txt, m->ult_detalhe, sizeof(m->ult_detalhe)) != 0) {
                snprintf(m->ult_detalhe, sizeof(m->ult_detalhe), "%s", txt);
                lv_label_set_text(m->detail, txt);
            }
            /* TODAS as sessoes, uma por linha, em vez de "projeto  +N".
             *
             * O "+N" dizia QUANTAS outras existiam e nunca QUAIS — e saber
             * quais e justamente o que se quer de longe, quando ha varios
             * projetos abertos ao mesmo tempo. O mascote continua sendo um so
             * (ver a decisao logo acima): a lista nao divide a tela, ela nomeia
             * o que esta rodando.
             *
             * O "> " marca a sessao que o mascote esta representando — a mais
             * urgente pelo critere de URGENCIA. Sem a marca, ver quatro nomes e
             * uma cara so deixa a pergunta "a cara e de qual deles?".
             *
             * ASCII puro no marcador porque as Montserrat embutidas no LVGL
             * cobrem so ASCII: um "›" bonito sairia como quadrado vazio. */
            char lista[200];
            lista[0] = '\0';
            int usado = 0;
            int mostrar = d->session_count;
            if (mostrar > WISP_MAX_SESSIONS) mostrar = WISP_MAX_SESSIONS;
            for (int k = 0; k < mostrar; k++) {
                int w = snprintf(lista + usado, sizeof(lista) - usado, "%s%s%.24s",
                                 k ? "\n" : "", k == esc ? "> " : "  ",
                                 d->sessions[k].project);
                if (w < 0) break;
                usado += w;
                if (usado >= (int) sizeof(lista) - 1) { usado = sizeof(lista) - 1; break; }
            }
            /* Mais sessoes do que caberia na lista: o resto volta a ser contagem.
             * Nao e o caso comum — WISP_MAX_SESSIONS e 4 e o bridge raramente
             * passa disso —, mas silenciar as excedentes seria mentir sobre o
             * que esta rodando. */
            if (d->session_count > mostrar && usado < (int) sizeof(lista) - 1) {
                snprintf(lista + usado, sizeof(lista) - usado, "\n  +%d",
                         d->session_count - mostrar);
            }
            /* FONTE PELA QUANTIDADE DE LINHAS, e a conta e o motivo.
             *
             * Com o mascote em 306px a lista comeca em y=395 e tem 75px ate a
             * borda util. No montserrat_24 uma linha mede 28px: cabem duas.
             * Tres ou quatro linhas nessa fonte sairiam da tela ou entrariam por
             * baixo da moldura — e some justamente o nome que se quis ler.
             *
             * Entao: 24 ate duas sessoes (o caso comum, e o tamanho pedido), 16
             * de tres em diante, onde quatro linhas de 19px fecham em 471. A
             * alternativa seria cortar a lista com um "+N", e ai a fonte grande
             * custaria a informacao — trocar o tamanho e o menor dos dois males.
             *
             * A troca invalida a tela inteira porque mudar de fonte muda a
             * ALTURA do label: em modo parcial num AMOLED, a area que ele
             * deixa de ocupar continua acesa com o texto velho. */
            static int ult_linhas = -1;
            if (mostrar != ult_linhas) {
                ult_linhas = mostrar;
                lv_obj_set_style_text_font(m->project,
                    mostrar <= 2 ? &lv_font_montserrat_24 : &lv_font_montserrat_16, 0);
                lv_obj_invalidate(lv_screen_active());
            }

            static char ult_lista[200];
            if (strcmp(lista, ult_lista) != 0) {
                snprintf(ult_lista, sizeof(ult_lista), "%s", lista);
                lv_label_set_text(m->project, lista);
            }
        }
    } else {
        char tmp[36];
        /* A hora muda uma vez por minuto; a consulta acontece 100 vezes nesse
         * intervalo. Sem guarda, 100 invalidacoes para o mesmo texto. */
        static char ult_hora[8], ult_dia[20];
        if (strcmp(d->clock, ult_hora) != 0) {
            snprintf(ult_hora, sizeof(ult_hora), "%s", d->clock);
            lv_label_set_text(g_hora, d->clock);
        }
        if (strcmp(d->day, ult_dia) != 0) {
            snprintf(ult_dia, sizeof(ult_dia), "%s", d->day);
            lv_label_set_text(g_dia, d->day);
        }
        if (d->has_weather) {
            /* ARMADILHA DO C: \x consome todos os dígitos hex seguintes.
             * Em "%d\xC2\xB0C" o 'C' é hex válido e some junto com o grau.
             * Fechar o literal antes do 'C' encerra o escape. */
            static int ult_t = -999, ult_hi = -999, ult_lo = -999;
            if (d->temp != ult_t || d->temp_max != ult_hi || d->temp_min != ult_lo) {
                ult_t = d->temp; ult_hi = d->temp_max; ult_lo = d->temp_min;
                snprintf(tmp, sizeof(tmp), "%d\xC2\xB0" "C", d->temp);
                lv_label_set_text(g_temp, tmp);
                lv_label_set_text(g_cond, d->condition);
                snprintf(tmp, sizeof(tmp), "max %d\xC2\xB0   min %d\xC2\xB0",
                         d->temp_max, d->temp_min);
                lv_label_set_text(g_maxmin, tmp);
            }
            if (strcmp(d->icon, g_icone_atual) != 0) {
                snprintf(g_icone_atual, sizeof(g_icone_atual), "%s", d->icon);
                montar_icone(d->icon);
            }
        }
    }

    /* —— painel de limites ——
     * Com guarda de mudanca. Sem ela reescreviamos 4 barras x 4 rotulos a
     * cada consulta (600ms), cada lv_label_set_text invalidando area, tudo
     * isso SEGURANDO o mutex do LVGL. A task do LVGL, que e quem le o touch,
     * ficava sem rodar — e o deslize demorava segundos para pegar. */
    uint32_t assinatura = (uint32_t) d->limit_count * 2654435761u;
    for (int i = 0; i < d->limit_count && i < MAX_BARRAS; i++) {
        const wisp_limit_t *b = &d->limits[i];
        assinatura = assinatura * 31u + (uint32_t) b->pct;
        assinatura = assinatura * 31u + (uint32_t) b->active;
        for (const char *c = b->label; *c; c++) assinatura = assinatura * 31u + (uint8_t) *c;
        for (const char *c = b->resets_in; *c; c++) assinatura = assinatura * 31u + (uint8_t) *c;
        for (const char *c = b->severity; *c; c++) assinatura = assinatura * 31u + (uint8_t) *c;
    }
    assinatura = assinatura * 31u + (uint32_t)(d->limits_age_s / 60);

    /* PRIMEIRA VEZ EXPLICITA, e nao "ult_assinatura = 0".
     *
     * A assinatura de "nenhum limite" vale exatamente zero: com limit_count 0
     * o laco acima nao roda, e limits_age_s = -1 dividido por 60 trunca para 0
     * em C. Partindo ult_assinatura de 0, o primeiro update era descartado por
     * parecer repeticao — e o painel ficava como nasceu, quatro barras vazias
     * sem texto nenhum, nem o "limits unavailable" que existe para explicar o
     * vazio. Num aparelho sem WiFi provisionado isso nunca se desfaz, porque
     * nunca chega um estado com limites de verdade para mudar a assinatura.
     *
     * Sentinela em vez de valor impossivel porque nao existe valor impossivel:
     * a hash pode dar qualquer uint32. */
    static bool primeiro_update = true;
    static uint32_t ult_assinatura = 0;
    if (primeiro_update || assinatura != ult_assinatura) {
        primeiro_update = false;
        ult_assinatura = assinatura;

        /* Quantidade de limites mudou: reposiciona a pilha e limpa a tela.
         *
         * O invalidate nao e zelo. Em modo PARCIAL num AMOLED, esconder objeto
         * nao apaga pixel — apenas marca a area como suja para alguem
         * redesenhar. Cartao que sai de cena, ou que se move, deixa o desenho
         * antigo ACESO no lugar, indistinguivel de conteudo valido. E a mesma
         * armadilha que a troca de arranjo dos mascotes documenta. */
        static int ult_quantos = -1;
        if (d->limit_count != ult_quantos) {
            ult_quantos = d->limit_count;
            posicionar_cards(d->limit_count);
            lv_obj_invalidate(g_tile_painel);
        }

        for (int i = 0; i < MAX_BARRAS; i++) {
            if (i >= d->limit_count) {
                lv_obj_add_flag(g_card[i], LV_OBJ_FLAG_HIDDEN);
                continue;
            }
            const wisp_limit_t *b = &d->limits[i];
            lv_obj_remove_flag(g_card[i], LV_OBJ_FLAG_HIDDEN);

            char tmp[40];
            snprintf(tmp, sizeof(tmp), "%d%%", b->pct);
            lv_label_set_text(g_bar_pct[i], tmp);

            lv_label_set_text(g_bar_rotulo[i], b->label);
            /* Pilula ACESA no limite que esta valendo, apagada nos outros.
             * A cor da barra diz "quanto ja gastei"; a pilula acesa diz "e este
             * que vai te parar primeiro". Duas perguntas, dois canais — juntar
             * as duas na mesma cor perderia uma delas. */
            lv_obj_set_style_bg_color(g_bar_rotulo[i],
                b->active ? lv_color_make(96, 86, 146) : lv_color_make(52, 50, 66), 0);
            lv_obj_set_style_text_color(g_bar_rotulo[i],
                b->active ? lv_color_make(236, 238, 246) : lv_color_make(150, 156, 170), 0);

            lv_bar_set_value(g_bar[i], b->pct, LV_ANIM_OFF);
            lv_obj_set_style_bg_color(g_bar[i], cor_do_pct(b->pct), LV_PART_INDICATOR);
            posicionar_ritmo(g_bar_ritmo[i], g_bar_marca[i], b->elapsed_pct);

            if (b->resets_in[0]) {
                snprintf(tmp, sizeof(tmp), "resets in %s", b->resets_in);
                lv_label_set_text(g_bar_reset[i], tmp);
            } else {
                lv_label_set_text(g_bar_reset[i], "");
            }
        }

        char fr[48];
        if (d->limits_age_s < 0) snprintf(fr, sizeof(fr), "limits unavailable");
        else snprintf(fr, sizeof(fr), "%d min ago", d->limits_age_s / 60);
        lv_label_set_text(g_frescor, fr);
        lv_obj_set_style_text_color(g_frescor,
            d->limits_age_s > 900 ? lv_color_make(232, 193, 90)
                                     : lv_color_make(120, 128, 140), 0);
    }

    bsp_display_unlock();
}

void ui_swipe(int direcao)
{
    if (!g_tv || direcao == 0) return;

    /* MUTEX. Nao e opcional e nao e paranoia: esta funcao e chamada da task
     * dos botoes, e mexer em objeto do LVGL fora do mutex ja travou esta
     * placa uma vez hoje — o watchdog pegou a task de rede presa dentro de
     * lv_inv_area(), numa lista de invalidacao corrompida por corrida com o
     * render. O sintoma nao parece nada com "esqueci um lock". */
    bsp_display_lock(-1);

    lv_obj_t *atual = lv_tileview_get_tile_active(g_tv);
    int i = 0;
    for (int k = 0; k < QTD_TELAS; k++) if (g_telas[k] == atual) { i = k; break; }

    /* Para na ponta em vez de dar a volta. O deslizar ja se comporta assim —
     * nao ha tela a direita do painel — e dois gestos para a mesma navegacao
     * precisam concordar, senao o botao parece bugado para quem usa os dois. */
    int destino = i + direcao;
    if (destino < 0 || destino >= QTD_TELAS) { bsp_display_unlock(); return; }

    lv_tileview_set_tile(g_tv, g_telas[destino], LV_ANIM_ON);
    bsp_display_unlock();
}
