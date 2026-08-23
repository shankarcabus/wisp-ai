/* O PIXEL: cara amarela arredondada, olhos preto sólido.
 *
 * POR QUE OBJETOS LVGL E NÃO BITMAP
 * ---------------------------------
 * O caminho óbvio para pixel art seria um buffer pequeno ampliado por escala de
 * imagem. Está medido, no projeto irmão desta bancada: escalar bitmap por
 * software custa ~0,76 µs por pixel de saída, o que no C6 de núcleo único dá
 * 100–220 ms POR QUADRO — e invalidação parcial de imagem transformada não
 * recorta a transformação, ela borra.
 *
 * É a mesma razão pela qual, neste firmware, o mascote de imagem não anima e o
 * vetorial anima: objeto o LVGL move e recolore sem transformar bitmap nenhum.
 * Um personagem que precisa animar, nesta placa, é feito de objetos.
 *
 * A SILHUETA EM DEGRAUS, SEM BITMAP
 * ---------------------------------
 * Um quadrado arredondado de pixel art é, geometricamente, retângulos
 * empilhados com o canto cortado em degraus. Três retângulos de raio ZERO dão
 * dois degraus por canto — que é o que a referência mostra — e o canto fica
 * duro, sem o antialias que um raio de LVGL traria.
 *
 *         ┌────────┐        B: estreito e alto
 *      ┌──┴────────┴──┐     C: médio, o segundo degrau
 *      │              │
 *    ┌─┴──────────────┴─┐   A: largo e baixo
 *    │                  │
 *    └─┬──────────────┬─┘
 *      └──┬────────┬──┘
 *         └────────┘
 *
 * MEDIDAS EM FRAÇÃO DE `d`
 * ------------------------
 * Nada aqui é px absoluto. Hoje a placa desenha um mascote de 306px e só
 * (ui_update força n = 1), mas medir em fração mantém possível voltar a dividir
 * a tela — o que fez o autor desistir daquilo foi a proporção fixa do PNG, e
 * personagem feito de objetos não tem proporção fixa. É uma porta que fica
 * aberta de graça.
 *
 * A COR NÃO MUDA COM O ESTADO
 * ---------------------------
 * Diferente do Terminal, onde a tela troca de cor e a informação chega como luz
 * de dentro. Aqui a referência é amarela nos oito estados, e quem diz o estado é
 * a CARA. Consequência assumida: perde-se a leitura periférica por cor. Se na
 * placa isso se mostrar pior, o lugar de mexer é o tom de sombra — escurecer
 * para `error`, esfriar para `offline` — sem tocar no corpo.
 */
#include <math.h>
#include <string.h>

#include "esp_log.h"
#include "mascote.h"

static const char *TAG = "pixel";

/* Unidades de arte na largura da cara, medidas na folha de referência. */
#define ART_W 22
#define U(d, n) ((int16_t) ((int32_t) (d) * (n) / ART_W))

/* Duas unidades de arte por degrau. Com três o canto ficava grosso e a
 * silhueta lia como cruz, não como quadrado arredondado. */
#define DEG 2

#define C_CLARO  lv_color_make(255, 209,  74)
#define C_BASE   lv_color_make(255, 184,  28)
#define C_SOMBRA lv_color_make(224, 138,   0)
#define C_OLHO   lv_color_make( 17,  17,  17)

typedef enum { OLHO_NORMAL, OLHO_ARCO, OLHO_X, OLHO_TRISTE } olho_t;
typedef enum { BOCA_NENHUMA, BOCA_ABERTA, BOCA_TRISTE } boca_t;

typedef struct {
    olho_t  olho;
    boca_t  boca;
    bool    sobrancelha;  /* preocupada: ponta INTERNA para cima */
    bool    pisca;
    uint8_t respira;      /* amplitude da respiração, em 256-avos de escala */
    int8_t  inclina;      /* graus, fixo */
} pixel_alvo_t;

/* Um por estado, na ordem de wisp_state_t (ui.h). Os oito estados da folha de
 * referência caem um a um nos do Wisp. */
static const pixel_alvo_t ALVO[WISP_COUNT] = {
    /*                    olho        boca          sobr   pisca resp incl */
    [WISP_IDLE]    = {OLHO_NORMAL, BOCA_NENHUMA, false, true,   6,   0},
    [WISP_WORKING] = {OLHO_NORMAL, BOCA_NENHUMA, false, true,   4,   0},
    [WISP_TOOL]    = {OLHO_NORMAL, BOCA_NENHUMA, false, true,   3,   0},
    [WISP_ASKING]  = {OLHO_NORMAL, BOCA_NENHUMA, false, true,   7,   0},
    [WISP_WAITING] = {OLHO_TRISTE, BOCA_NENHUMA, true,  true,  10,   0},
    [WISP_DONE]    = {OLHO_ARCO,   BOCA_ABERTA,  false, false,  9,   0},
    [WISP_ERROR]   = {OLHO_TRISTE, BOCA_TRISTE,  true,  false,  3,  -4},
    [WISP_OFFLINE] = {OLHO_X,      BOCA_NENHUMA, false, false,  2,   0},
};

/* `done` não pisca porque os olhos já estão em arco: piscar um olho fechado não
 * comunica nada. `error` e `offline` não piscam porque a imobilidade é parte do
 * que eles dizem. */
#define PISCADA_MS 170

/* Dezesseis objetos: três do corpo, quatro de relevo, dois de olho, duas barras
 * do X, duas sobrancelhas e três de boca. A contagem importa porque é ela que a
 * medição na placa vai cobrar — para comparação, o Terminal vetorial usa cerca
 * de vinte e anima bem nesta placa. */
typedef struct {
    /* O contêiner transparente que o layout move e escala. Fica aqui, e não num
     * global, porque há um bloco privado por mascote e um global serviria a um
     * só — mesmo que hoje só exista um em cena. */
    lv_obj_t *raiz;
    lv_obj_t *corpo[3];      /* A largo-baixo, B estreito-alto, C médio */
    /* O relevo. Na referência a luz pega o TOPO e a ESQUERDA e a sombra o
     * BAIXO e a DIREITA — é esse par de lados adjacentes que dá o volume, e não
     * uma faixa clara em cima com uma escura embaixo, que lê como duas barras
     * soltas sobre o corpo. */
    lv_obj_t *luz[2];        /* topo, esquerda */
    lv_obj_t *sombra[2];     /* baixo, direita */
    lv_obj_t *olho[2];
    lv_obj_t *cruz[2];       /* a segunda barra do X */
    lv_obj_t *sobrancelha[2];
    /* A boca em três peças: o centro mais as duas pontas. É o que permite a
     * curva virada para baixo de `error` — em pixel art, curva com dois pixels
     * de espessura é degrau, e um único retângulo rotacionado só dá uma boca
     * torta, não uma triste. */
    lv_obj_t *boca, *boca_ponta[2];
    uint32_t  prox_piscada, inicio_piscada;
    int16_t   p_d;           /* guarda: só refaz geometria se `d` mudou */
    /* `int`, e não olho_t, porque -1 é o valor de "invalidado" que força a
     * reaplicação. Enum recebendo -1 é comportamento que depende do
     * compilador. */
    int       p_olho;
} pixel_t;

static lv_obj_t *retangulo(lv_obj_t *pai, lv_color_t cor)
{
    lv_obj_t *o = lv_obj_create(pai);
    /* so_decoracao() cuida de scroll, clique e bolha de evento — NÃO zera
     * borda nem padding. disco() em ui.c zera os dois à mão pelo mesmo motivo.
     * Sem isto cada retângulo sai com a borda cinza padrão do LVGL e a
     * silhueta em degraus aparece contornada. */
    so_decoracao(o);
    lv_obj_set_style_border_width(o, 0, 0);
    lv_obj_set_style_pad_all(o, 0, 0);
    lv_obj_set_style_radius(o, 0, 0);   /* canto quadrado: o degrau é a forma */
    lv_obj_set_style_bg_color(o, cor, 0);
    lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
    return o;
}

static void geometria(pixel_t *p, int16_t d)
{
    if (p->p_d == d) return;
    p->p_d = d;
    /* Invalida a guarda do rosto. Sem isto há um bug silencioso: a geometria
     * redefine os olhos no tamanho NEUTRO, aplicar_olho() vê o mesmo estado de
     * antes e não corrige — o personagem perde o olho em arco e o X no instante
     * em que o layout troca de tamanho. */
    p->p_olho = -1;

    const int16_t deg = U(d, DEG);

    /* A: largura cheia, altura menos dois degraus. */
    lv_obj_set_size(p->corpo[0], d, d - 2 * deg);
    lv_obj_align(p->corpo[0], LV_ALIGN_CENTER, 0, 0);
    /* B: altura cheia, largura menos dois degraus. */
    lv_obj_set_size(p->corpo[1], d - 2 * deg, d);
    lv_obj_align(p->corpo[1], LV_ALIGN_CENTER, 0, 0);
    /* C: o segundo degrau, meio caminho entre os dois. */
    lv_obj_set_size(p->corpo[2], d - deg, d - deg);
    lv_obj_align(p->corpo[2], LV_ALIGN_CENTER, 0, 0);

    /* Relevo: uma unidade de espessura, recuada dos degraus para não vazar no
     * canto cortado. A luz vem do mesmo lado nos oito estados — trocar de lado
     * entre estados faria o personagem parecer mudar de lugar na mesa. */
    const int16_t esp = U(d, 1);
    lv_obj_set_size(p->luz[0], d - 2 * deg, esp);
    lv_obj_align(p->luz[0], LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_set_size(p->luz[1], esp, d - 2 * deg);
    lv_obj_align(p->luz[1], LV_ALIGN_LEFT_MID, 0, 0);

    /* A base é mais grossa que a lateral: é onde o personagem encosta. */
    lv_obj_set_size(p->sombra[0], d - 2 * deg, esp * 2);
    lv_obj_align(p->sombra[0], LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_size(p->sombra[1], esp, d - 2 * deg);
    lv_obj_align(p->sombra[1], LV_ALIGN_RIGHT_MID, 0, 0);

    for (int i = 0; i < 2; i++) {
        lv_obj_align(p->olho[i], LV_ALIGN_CENTER,
                     (i ? 1 : -1) * U(d, 4), -U(d, 1));
        lv_obj_set_size(p->cruz[i], U(d, 5), U(d, 2));
        lv_obj_align(p->cruz[i], LV_ALIGN_CENTER,
                     (i ? 1 : -1) * U(d, 4), -U(d, 1));
    }
}

static void aplicar_olho(pixel_t *p, olho_t o, int16_t d)
{
    /* Só o olho na guarda: geometria() já invalidou p_olho quando `d` mudou. */
    if (p->p_olho == (int) o) return;
    p->p_olho = (int) o;

    /* Prólogo: devolve as duas peças de cada olho ao estado NEUTRO — visível,
     * sem rotação, no tamanho e na POSIÇÃO padrão.
     *
     * Isto não é zelo, é a correção de um erro que apareceu três vezes: o arco
     * desloca as barras na horizontal e encolhe a segunda para formar o `^`, e o
     * X seguinte herdava as duas coisas — saía como um par de ticks, depois como
     * uma flecha. Cada caso tem de partir do MESMO ponto; quando o prólogo zera
     * só parte do estado, a aparência passa a depender do estado ANTERIOR, e o
     * bug só aparece na transição, nunca numa captura isolada. */
    for (int i = 0; i < 2; i++) {
        const int16_t cx = (i ? 1 : -1) * U(d, 4), cy = -U(d, 1);
        lv_obj_remove_flag(p->olho[i], LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(p->cruz[i], LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_style_transform_rotation(p->olho[i], 0, 0);
        lv_obj_set_style_transform_rotation(p->cruz[i], 0, 0);
        lv_obj_set_size(p->olho[i], U(d, 2), U(d, 5));
        lv_obj_set_size(p->cruz[i], U(d, 5), U(d, 2));
        lv_obj_align(p->olho[i], LV_ALIGN_CENTER, cx, cy);
        lv_obj_align(p->cruz[i], LV_ALIGN_CENTER, cx, cy);
    }

    switch (o) {
    case OLHO_NORMAL:
        break;                                   /* o padrão da geometria */
    case OLHO_ARCO:
        /* O `^` da referência, feito de duas barras inclinadas que se encontram
         * no alto — as MESMAS duas peças que formam o X de `offline`, em outro
         * ângulo. Uma barra reta e larga foi a primeira tentativa e lia como
         * olho fechado de sono, não de contentamento: o que faz o olho sorrir é
         * a ponta virada para cima. */
        for (int i = 0; i < 2; i++) {
            const int16_t bl = U(d, 3), bh = U(d, 1);
            const int16_t cx = (i ? 1 : -1) * U(d, 4);
            lv_obj_t *meia[2] = {p->olho[i], p->cruz[i]};
            for (int k = 0; k < 2; k++) {
                lv_obj_remove_flag(meia[k], LV_OBJ_FLAG_HIDDEN);
                lv_obj_set_size(meia[k], bl, bh);
                lv_obj_set_style_transform_pivot_x(meia[k], bl / 2, 0);
                lv_obj_set_style_transform_pivot_y(meia[k], bh / 2, 0);
                lv_obj_set_style_transform_rotation(meia[k], k ? 220 : -220, 0);
                lv_obj_align(meia[k], LV_ALIGN_CENTER,
                             cx + (k ? 1 : -1) * bl / 2, -U(d, 1));
            }
        }
        break;
    case OLHO_TRISTE:
        /* Mais baixo e mais curto. Sozinho já entristece; a sobrancelha é o
         * que fecha a expressão, e ela vem depois. */
        for (int i = 0; i < 2; i++)
            lv_obj_set_size(p->olho[i], U(d, 2), U(d, 4));
        break;
    case OLHO_X:
        /* Duas barras cruzadas por olho. A rotação é de OBJETO e é aplicada uma
         * vez, na troca de estado — não é transformação por quadro.
         *
         * O PIVÔ é obrigatório: o LVGL rotaciona em torno do canto superior
         * esquerdo por padrão, e sem centrá-lo as duas barras giram para longe
         * uma da outra e o X sai como um par de chevrons apontando para o lado.
         * Foi exatamente o que apareceu na primeira tentativa. */
        for (int i = 0; i < 2; i++) {
            const int16_t bl = U(d, 5), bh = U(d, 2);
            lv_obj_set_size(p->olho[i], bl, bh);
            lv_obj_set_style_transform_pivot_x(p->olho[i], bl / 2, 0);
            lv_obj_set_style_transform_pivot_y(p->olho[i], bh / 2, 0);
            lv_obj_set_style_transform_rotation(p->olho[i], 450, 0);   /* 45° */

            lv_obj_remove_flag(p->cruz[i], LV_OBJ_FLAG_HIDDEN);
            lv_obj_set_style_transform_pivot_x(p->cruz[i], bl / 2, 0);
            lv_obj_set_style_transform_pivot_y(p->cruz[i], bh / 2, 0);
            lv_obj_set_style_transform_rotation(p->cruz[i], -450, 0);
        }
        break;
    }
}

/* A diferença entre parecer PREOCUPADO e parecer BRAVO é qual ponta sobe.
 *
 * Sobe a ponta INTERNA — a que fica perto do centro da cara. Invertido, o
 * personagem culpa quem está olhando, e o `error` do Wisp existe justamente
 * para dizer o contrário: a falha não é sua. O Terminal aprendeu isso primeiro
 * e registrou no `sob_invertida` da tabela dele. */
static void aplicar_sobrancelha(pixel_t *p, bool mostrar, int16_t d)
{
    for (int i = 0; i < 2; i++) {
        if (!mostrar) {
            lv_obj_add_flag(p->sobrancelha[i], LV_OBJ_FLAG_HIDDEN);
            continue;
        }
        const int16_t sl = U(d, 4), sh = U(d, 1);
        lv_obj_remove_flag(p->sobrancelha[i], LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_size(p->sobrancelha[i], sl, sh);
        lv_obj_align(p->sobrancelha[i], LV_ALIGN_CENTER,
                     (i ? 1 : -1) * U(d, 4), -U(d, 6));
        /* Pivô centrado: sem isso a barra gira em torno do canto e sai do
         * lugar, a mesma armadilha do X de `offline`. */
        lv_obj_set_style_transform_pivot_x(p->sobrancelha[i], sl / 2, 0);
        lv_obj_set_style_transform_pivot_y(p->sobrancelha[i], sh / 2, 0);
        /* O sinal: rotação positiva no LVGL desce a ponta DIREITA. Na
         * sobrancelha esquerda (i=0) a ponta interna é a direita, então ela
         * precisa de ângulo NEGATIVO para levantar a de dentro; na direita é o
         * contrário. Invertido, saem duas sobrancelhas bravas — foi o que
         * apareceu na primeira tentativa, e "bravo" é o oposto do que o estado
         * `error` deste projeto quer dizer. */
        lv_obj_set_style_transform_rotation(p->sobrancelha[i],
                                            i ? 160 : -160, 0);   /* ±16° */
    }
}

static void aplicar_boca(pixel_t *p, boca_t b, int16_t d)
{
    lv_obj_t *tudo[] = {p->boca, p->boca_ponta[0], p->boca_ponta[1]};
    if (b == BOCA_NENHUMA) {
        for (int i = 0; i < 3; i++) lv_obj_add_flag(tudo[i], LV_OBJ_FLAG_HIDDEN);
        return;
    }

    lv_obj_remove_flag(p->boca, LV_OBJ_FLAG_HIDDEN);
    if (b == BOCA_ABERTA) {
        /* Boca aberta de alegria, em pixel art, é um bloco cheio. As pontas
         * não entram: elas existem para a curva do `error`. */
        for (int i = 0; i < 2; i++)
            lv_obj_add_flag(p->boca_ponta[i], LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_size(p->boca, U(d, 7), U(d, 3));
        lv_obj_align(p->boca, LV_ALIGN_CENTER, 0, U(d, 4));
        /* Raio só embaixo seria o ideal — o LVGL não separa por canto, então o
         * bloco arredondado inteiro é a aproximação. Larga o suficiente para
         * ler como boca aberta e não como pastilha. */
        lv_obj_set_style_radius(p->boca, U(d, 1), 0);
        return;
    }

    /* Triste é "∩": o centro ALTO e as duas pontas um degrau ABAIXO.
     *
     * O contrário — centro embaixo, pontas em cima — é "U", que é sorriso. Foi
     * o que eu desenhei primeiro, e o resultado foi um personagem sorrindo no
     * estado de falha. A curvatura é o que separa contentamento de aflição, e
     * ela não perdoa o sinal trocado. */
    lv_obj_set_style_radius(p->boca, 0, 0);
    const int16_t esp = U(d, 1);
    lv_obj_set_size(p->boca, U(d, 3), esp);
    lv_obj_align(p->boca, LV_ALIGN_CENTER, 0, U(d, 4));
    for (int i = 0; i < 2; i++) {
        lv_obj_remove_flag(p->boca_ponta[i], LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_size(p->boca_ponta[i], esp, esp);
        lv_obj_align(p->boca_ponta[i], LV_ALIGN_CENTER,
                     (i ? 1 : -1) * U(d, 2), U(d, 4) + esp);
    }
}

/* Devolve true enquanto o olho está fechado.
 *
 * Quem chama decide: piscando, mexe na altura do olho e INVALIDA a guarda de
 * aplicar_olho, para que ela restaure a forma no quadro seguinte. Se as duas
 * coisas rodassem no mesmo quadro, a guarda veria o mesmo estado de antes e o
 * olho ficaria fechado para sempre. */
static bool piscando(pixel_t *p, const pixel_alvo_t *a, uint32_t agora)
{
    if (!a->pisca) return false;
    if (p->prox_piscada == 0) p->prox_piscada = agora + 2600;

    if (agora >= p->prox_piscada && p->inicio_piscada == 0) {
        p->inicio_piscada = agora;
        /* Intervalo irregular, 2,2 a 4,6s. Cadência exata lê como pisca-pisca
         * de aparelho, não como olho. */
        p->prox_piscada = agora + 2200 + (agora % 2400);
    }
    if (p->inicio_piscada) {
        if (agora - p->inicio_piscada < PISCADA_MS) return true;
        p->inicio_piscada = 0;
        p->p_olho = -1;          /* força aplicar_olho a restaurar */
    }
    return false;
}

static void pixel_criar(lv_obj_t *pai, mascote_t *m)
{
    pixel_t *p = lv_malloc_zeroed(sizeof(pixel_t));
    m->interno = p;
    if (!p) { ESP_LOGE(TAG, "sem memoria para o mascote"); return; }

    /* A raiz recebe a respiração e a inclinação: transformar o contêiner move a
     * cara inteira de uma vez, em vez de exigir sete objetos em sincronia. */
    p->raiz = lv_obj_create(pai);
    so_decoracao(p->raiz);
    lv_obj_set_style_border_width(p->raiz, 0, 0);
    lv_obj_set_style_pad_all(p->raiz, 0, 0);
    lv_obj_set_style_bg_opa(p->raiz, LV_OPA_TRANSP, 0);
    lv_obj_set_style_radius(p->raiz, 0, 0);

    for (int i = 0; i < 3; i++) p->corpo[i] = retangulo(p->raiz, C_BASE);
    for (int i = 0; i < 2; i++) {
        p->luz[i]    = retangulo(p->raiz, C_CLARO);
        p->sombra[i] = retangulo(p->raiz, C_SOMBRA);
    }
    for (int i = 0; i < 2; i++) {
        p->olho[i] = retangulo(p->raiz, C_OLHO);
        p->cruz[i] = retangulo(p->raiz, C_OLHO);
        lv_obj_add_flag(p->cruz[i], LV_OBJ_FLAG_HIDDEN);
        p->sobrancelha[i] = retangulo(p->raiz, C_OLHO);
        lv_obj_add_flag(p->sobrancelha[i], LV_OBJ_FLAG_HIDDEN);
        p->boca_ponta[i] = retangulo(p->raiz, C_OLHO);
        lv_obj_add_flag(p->boca_ponta[i], LV_OBJ_FLAG_HIDDEN);
    }
    p->boca = retangulo(p->raiz, C_OLHO);
    lv_obj_add_flag(p->boca, LV_OBJ_FLAG_HIDDEN);

    p->p_d = -1;
    p->p_olho = -1;
}

static void pixel_animar(mascote_t *m, uint32_t agora, bool sozinho)
{
    (void) sozinho;
    pixel_t *p = m->interno;
    if (!p || !p->raiz) return;

    const pixel_alvo_t *a = &ALVO[m->alvo];
    geometria(p, m->d);

    if (piscando(p, a, agora)) {
        for (int i = 0; i < 2; i++)
            lv_obj_set_height(p->olho[i], U(m->d, 1));
        p->p_olho = -1;
    } else {
        aplicar_olho(p, a->olho, m->d);
    }
    aplicar_sobrancelha(p, a->sobrancelha, m->d);
    aplicar_boca(p, a->boca, m->d);

    /* Respiração com o VOLUME CONSERVADO: o que estica na vertical encolhe na
     * horizontal. Sem isso o boneco não respira, ele infla.
     *
     * Âncora embaixo, porque o personagem se apoia no chão do quadro — é de lá
     * que a referência mostra o peso. Um ciclo a cada ~2,2s. */
    const int32_t fase = lv_trigo_sin((int16_t) ((agora / 6) % 360));
    const int32_t sy   = 256 + fase * a->respira / 32767;
    const int32_t sx   = 256 * 256 / (sy ? sy : 256);
    lv_obj_set_style_transform_pivot_y(p->raiz, m->d, 0);
    lv_obj_set_style_transform_scale_y(p->raiz, sy, 0);
    lv_obj_set_style_transform_scale_x(p->raiz, sx, 0);
    lv_obj_set_style_transform_rotation(p->raiz, a->inclina * 10, 0);
}

static void pixel_dispor(mascote_t *m, int16_t d, int16_t x, int16_t y,
                         bool mostrar)
{
    pixel_t *p = m->interno;
    if (!p || !p->raiz) return;

    if (!mostrar) { lv_obj_add_flag(p->raiz, LV_OBJ_FLAG_HIDDEN); return; }
    lv_obj_remove_flag(p->raiz, LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_size(p->raiz, d, d);
    lv_obj_align(p->raiz, LV_ALIGN_CENTER, x, y);
    geometria(p, d);
}

const personagem_t MASCOTE_PIXEL = {
    .nome       = "pixel",
    /* Não usa a partição `storage`: a cara é desenhada, e os adornos que virão
     * moram no binário do app como arrays gerados em tempo de build. */
    .usa_assets = false,
    .criar      = pixel_criar,
    .animar     = pixel_animar,
    .dispor     = pixel_dispor,
};
