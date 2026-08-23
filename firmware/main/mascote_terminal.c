/* O TERMINAL: o computador retrô, o personagem de fábrica do Wisp.
 *
 * Este arquivo saiu de ui.c por extração — o desenho é o mesmo, linha por
 * linha. O que mudou é a fronteira: as partes do computador vivem num
 * `terminal_t` privado, apontado por `mascote_t.interno`, e o layout não sabe
 * mais o que é uma carcaça, uma tela ou um braço.
 *
 * Duas renderizações do MESMO personagem convivem aqui:
 *
 *   imagem   os oito PNG convertidos para RGB565A8 e mapeados da partição
 *            `storage`. É o padrão quando a partição monta. Não anima: uma
 *            imagem com alpha reinvalidada a cada quadro dava 6 FPS.
 *   vetorial  o mesmo computador desenhado com primitivas do LVGL. É o
 *            fallback quando o mmap falha, e ESSE anima.
 *
 * "O mascote parou de animar" quase sempre significa que a partição de assets
 * voltou a montar, não que a animação quebrou.
 */
#include <math.h>
#include <string.h>

#include "esp_log.h"
#include "esp_mmap_assets.h"
#include "esp_lv_decoder.h"
#include "lvgl.h"
#include "mascote.h"

static const char *TAG = "terminal";

/* ————————————————————————————————————————————————
 *  Mascotes de imagem, vindos da particao `storage`
 *
 *  Os PNG sao empacotados no build e ficam MAPEADOS na flash — o ponteiro
 *  aponta direto para os bytes, sem copia para a RAM. Isso importa muito
 *  aqui: a RAM interna e o recurso mais escasso desta placa, e ja chegou a
 *  4,7KB de minimo historico.
 *
 *  Os assets sao acessados por INDICE, em ordem alfabetica — ver IDX abaixo.
 *
 *  SOBRE O CHECKSUM, que ja custou tempo a duas pessoas: nao passamos nenhum, e
 *  isso e deliberado. Este componente gera um `mmap_generate_storage.h` com o
 *  valor certo, mas quando o binario comeca com o magic "MMAP" — o formato atual
 *  — ele usa o checksum do PROPRIO cabecalho e IGNORA o que vem na config
 *  (esp_mmap_assets.c: `stored_chksum = header.checksum`; a config so entra no
 *  fallback do formato legado). Havia um 23455 fixo aqui que nao validava nada e
 *  contradizia o header gerado depois que a arte mudou: numero magico inerte
 *  parece verificacao e nao e. Zero diz a verdade — quem valida e o binario. */
#define ASSETS_QTD      8

/* Ordem alfabetica dos arquivos na particao, mapeada para os nossos estados. */
static const int IDX[WISP_COUNT] = {
    [WISP_IDLE]      = 3,   /* idle    */
    [WISP_WORKING] = 7,   /* working */
    [WISP_TOOL]  = 5,   /* tool    */
    [WISP_ASKING] = 0,   /* asking  */
    [WISP_WAITING]   = 6,   /* waiting */
    [WISP_DONE]   = 1,   /* done    */
    [WISP_ERROR]        = 2,   /* error   */
    [WISP_OFFLINE]    = 4,   /* offline */
};

static mmap_assets_handle_t s_assets;
static lv_image_dsc_t s_dsc[WISP_COUNT];
static bool s_tem_fotos = false;

static void carregar_fotos(void)
{
    esp_lv_decoder_handle_t dec = NULL;
    if (esp_lv_decoder_init(&dec) != ESP_OK) {
        ESP_LOGW(TAG, "decoder nao subiu — segue no vetor");
        return;
    }
    const mmap_assets_config_t cfg = {
        .partition_label = "storage",
        .max_files = ASSETS_QTD,
        .checksum = 0,        /* ver a nota acima: o binario e quem manda */
        .flags = {.mmap_enable = true},
    };
    if (mmap_assets_new(&cfg, &s_assets) != ESP_OK) {
        ESP_LOGW(TAG, "particao de mascotes nao abriu — segue no vetor");
        return;
    }
    for (int e = 0; e < WISP_COUNT; e++) {
        int i = IDX[e];
        const uint8_t *dados = mmap_assets_get_mem(s_assets, i);
        /* get_size conta os 2 bytes de magic do empacotador, que o get_mem ja
         * pulou — sem descontar, sobra lixo no fim de cada imagem. */
        int tam = mmap_assets_get_size(s_assets, i) - 2;
        if (!dados || tam <= (int) sizeof(lv_image_header_t)) return;

        /* RGB565A8 CRU, convertido no build. Sem decodificacao: o LVGL le
         * direto da flash mapeada.
         *
         * A primeira versao usava RAW_ALPHA, que decodifica PNG em tempo de
         * execucao. Medido: FPS caiu de 62 para 1-7 e a RAM interna chegou a
         * 12 bytes de minimo historico. Nesta placa nao ha folga para
         * decodificar imagem — a conversao tem que acontecer antes.
         *
         * O .bin comeca com o cabecalho do LVGL: 12 bytes com magic, formato,
         * largura, altura e stride. ELE NAO E PIXEL. Apontar `data` para o
         * inicio do arquivo desloca o plano de cor em 6 pixels (12 bytes a 2
         * por pixel) e o alfa em 12, porque o LVGL acha o alfa somando
         * stride*h ao mesmo ponteiro. Na tela isso aparece como o boneco
         * cortado de um lado e reaparecendo do outro, com as bordas picotadas
         * — a cor e o alfa fora de fase entre si.
         *
         * Ler o cabecalho do proprio arquivo, em vez de reescrever 236 aqui,
         * tira a medida do codigo de quebra: trocar a arte por outra
         * resolucao deixa de exigir uma edicao que ninguem lembra de fazer. */
        memcpy(&s_dsc[e].header, dados, sizeof(lv_image_header_t));
        s_dsc[e].data      = dados + sizeof(lv_image_header_t);
        s_dsc[e].data_size = tam - sizeof(lv_image_header_t);

        if (s_dsc[e].header.magic != LV_IMAGE_HEADER_MAGIC ||
            s_dsc[e].header.cf != LV_COLOR_FORMAT_RGB565A8) {
            ESP_LOGW(TAG, "asset %d nao e RGB565A8 do LVGL — segue no vetor", i);
            return;
        }
    }
    s_tem_fotos = true;
    ESP_LOGI(TAG, "mascotes de imagem carregados (%d estados)", WISP_COUNT);
}

#define QTD_INTERROG 3
#define PISCADA_MS 170

/* ——— paleta por estado ——— */
typedef struct { uint8_t r, g, b, dr, dg, db; } cor_t;

/* O corpo do computador nao muda de cor — quem muda e a TELA, como num
 * monitor de verdade. Isso e mais fiel ao personagem e mais legivel: a cor
 * chega como LUZ vindo de dentro, nao como o boneco inteiro trocando de
 * tinta. */
#define C_CARCACA_T lv_color_make(238, 230, 210)
#define C_CARCACA_B lv_color_make(206, 194, 172)

static const cor_t COR[WISP_COUNT] = {
    [WISP_IDLE]      = {232, 132,  90, 176,  78,  52},
    [WISP_WORKING] = {232, 132,  90, 176,  78,  52},
    [WISP_TOOL]  = {232, 152,  82, 172,  96,  44},
    [WISP_ASKING] = {186, 142, 234, 122,  84, 172},
    [WISP_WAITING]   = {232, 193,  90, 168, 132,  48},
    [WISP_DONE]   = { 95, 207, 142,  52, 132,  90},
    [WISP_ERROR]        = { 96, 150, 205,  58,  96, 140},
    [WISP_OFFLINE]    = { 90,  99, 112,  52,  58,  68},
};


/* Alvos por estado. As medidas do olho são para o mascote em tamanho cheio;
 * com várias sessões elas são escaladas proporcionalmente. */
/* Bocas. A curvatura e o que separa contentamento de aflicao. */
typedef enum { BOCA_SORRISO, BOCA_SORRISAO, BOCA_PEQUENA, BOCA_O,
               BOCA_RETA, BOCA_ONDA } boca_t;

typedef struct {
    int16_t olho_alt, olho_dx, olho_dy;
    uint8_t respira;
    uint8_t orbita;      /* 0 = luz parada, 255 = órbita rápida */
    bool    interrog;
    /* —— expressao ——
     * sobrancelha: angulo em graus. Positivo levanta a ponta EXTERNA, que le
     * como surpresa; negativo abaixa, que le como concentracao. Com
     * `sob_invertida` quem sobe e a ponta INTERNA — e essa e a diferenca
     * entre parecer bravo e parecer preocupado. */
    int16_t sob_ang;
    int16_t sob_dy;
    bool    sob_invertida;
    boca_t  boca;
    int16_t olhar_x, olhar_y;   /* direcao da pupila, -100 a 100 */
    bool    olhos_felizes;      /* arcos para cima, o sorriso que mora no olho */
} alvo_t;

static const alvo_t ALVO[WISP_COUNT] = {
    /*                 alt  dx   dy  resp orb interr | sob_ang dy inv | boca         olhar_x y | felizes */
    [WISP_IDLE]      = {40,  0,  -6,  6,  20, false,        0,  0, false, BOCA_SORRISO,   0,   0, false},
    [WISP_WORKING] = {36,  0,  -2,  4, 170, false,       -6,  4, false, BOCA_PEQUENA,  45, -50, false},
    [WISP_TOOL]  = {24, -4,   4,  3, 255, false,      -16, -5, false, BOCA_PEQUENA,   0,  15, false},
    [WISP_ASKING] = {46,  0,  -9,  7,   0, true,        14, 10, false, BOCA_O,         0, -10, false},
    [WISP_WAITING]   = {44,  0,  -8, 10,  60, false,       20,  8, false, BOCA_ONDA,      0,  10, false},
    [WISP_DONE]   = {12,  0,  -2,  9,  40, false,        8,  6, false, BOCA_SORRISAO,  0,   0, true },
    [WISP_ERROR]        = {32,  0,   6,  3,  25, false,      -22,  2, true,  BOCA_ONDA,      0,  25, false},
    [WISP_OFFLINE]    = { 8,  0,   0,  2,   0, false,        0, -3, false, BOCA_RETA,      0,   0, false},
};

/* ————————————————————————————————————————————————
 *  As partes do computador. Privadas: o layout nunca vê isto.
 * ———————————————————————————————————————————————— */
typedef struct {
    lv_obj_t *corpo, *olho[2], *wisp;
    /* Rosto: o que estava faltando para os oito estados nao serem o mesmo
     * boneco em oito cores. Olho vira BRANCO com pupila e brilho; sobrancelha
     * e boca entram porque e nelas que a expressao mora. */
    lv_obj_t *pupila[2], *brilho[2], *sobrancelha[2], *boca, *chama;
    lv_obj_t *tela, *braco[2];       /* carcaca de computador */
    lv_obj_t *moldura, *luz, *scan[2], *topo;  /* profundidade */
    lv_obj_t *foto;          /* mascote de imagem; NULL = desenhado */
    int16_t p_boca, p_esc;   /* guardas do rosto: estado e escala */
    int16_t p_escala_dv;     /* guarda da escala da FOTO: o dv aplicado */
    int16_t p_chama_esc, p_chama_x, p_chama_y;  /* guardas da chama */
    /* Pontos da sobrancelha. lv_line guarda o PONTEIRO, nao copia — se este
     * array sair de escopo, o LVGL desenha lixo. Por isso vive aqui. */
    lv_point_precise_t sob_pts[2][2];
    int16_t olho_alt, olho_dx, olho_dy;
    int16_t p_alt, p_dx, p_dy;          /* guardas: só redesenha se mudou */
    float   ang, vel;                    /* órbita acumulada da luz */
    uint32_t prox_piscada, inicio_piscada;
} terminal_t;

/* As interrogações que sobem no estado `asking`. Três objetos compartilhados
 * por todos os mascotes, criados uma vez — só existem no modo de sessão única,
 * que é o único que a placa tem. */
static lv_obj_t *g_interrog[QTD_INTERROG];
static bool g_interrog_prontas = false;

static inline int16_t aproximar(int16_t atual, int16_t alvo, int passo)
{
    int16_t d = alvo - atual;
    if (d == 0) return atual;
    int16_t inc = d / passo;
    return inc ? atual + inc : alvo;
}

/* ————————————————————————————————————————————————
 *  Animação
 * ———————————————————————————————————————————————— */
static void terminal_animar(mascote_t *m, uint32_t agora, bool sozinho)
{
    terminal_t *T = m->interno;
    if (!T) return;
    const alvo_t *A = &ALVO[m->alvo];

    /* Com foto, o quadro a quadro nao existe.
     *
     * A respiracao a 60fps foi feita para um boneco DESENHADO: era ela que
     * dava vida a formas geometricas. Uma imagem ja e o personagem inteiro, e
     * so precisa mudar quando o estado muda.
     *
     * Manter o laco rodando custava caro: cada quadro reinvalidava uma imagem
     * de 236x236 com alfa, que atravessa o buffer de 8 linhas em 30 tiras.
     * Medido: 6 FPS e 1,6KB de RAM interna no minimo. Parando o laco, a tela
     * simplesmente fica quieta ate ter noticia nova — que e o comportamento
     * certo para um indicador de status. */
    if (T->foto) {
        if (T->p_boca != m->alvo || T->p_esc != (int16_t) m->d) {
            T->p_boca = m->alvo;
            T->p_esc  = m->d;
            lv_image_set_src(T->foto, &s_dsc[m->alvo]);
        }
        return;
    }
    const cor_t *C = &COR[m->alvo];
    /* Escala das medidas do olho em relação ao mascote cheio (236px). */
    const int esc = m->d;

    /* Cor: troca direta. Interpolar repintava o corpo a cada passo, e cada
     * repintura varre a tela em faixas — 20 varreduras por transição viravam
     * listras visíveis. Uma varredura por mudança é o mínimo possível. */
    if (m->alvo != m->anterior) {
        m->anterior = m->alvo;
        /* A cor do estado e a LUZ da tela, nao a tinta do boneco. */
        lv_obj_set_style_bg_color(T->tela, lv_color_make(C->r, C->g, C->b), 0);
        lv_obj_set_style_bg_grad_color(T->tela, lv_color_make(C->dr, C->dg, C->db), 0);
    }

    /* Respiração vai nos OLHOS, não no corpo: qualquer mudança no corpo
     * invalida a área toda e o painel a entrega em faixas sequenciais,
     * desenhando costuras. Os olhos são pequenos e não têm esse custo. */
    float fase = (agora % 2600) / 2600.0f * 2.0f * (float) M_PI;
    int16_t respiro = (int16_t)(sinf(fase) * (A->respira / 2 + 1)) * esc / 236;

    /* Piscada por fase decorrida: fecha e abre simetricamente. */
    if (agora > T->prox_piscada) {
        T->inicio_piscada = agora;
        T->prox_piscada = agora + 3000 + (agora % 4000);
    }
    int16_t fechamento = 256;
    uint32_t dec = agora - T->inicio_piscada;
    if (T->inicio_piscada && dec < PISCADA_MS) {
        const uint32_t meio = PISCADA_MS / 2;
        fechamento = dec < meio ? 256 - (int16_t)(dec * 256 / meio)
                                : (int16_t)((dec - meio) * 256 / meio);
    }

    T->olho_alt = aproximar(T->olho_alt, A->olho_alt, 6);
    T->olho_dx  = aproximar(T->olho_dx,  A->olho_dx,  6);
    T->olho_dy  = aproximar(T->olho_dy,  A->olho_dy,  6);

    int16_t alt = T->olho_alt * fechamento / 256 * esc / 236;
    int16_t larg = 25 * esc / 236;
    if (alt < 2) alt = 2;
    if (larg < 6) larg = 6;
    int16_t dx = T->olho_dx * esc / 236, dy = (T->olho_dy + respiro) * esc / 236;

    if (alt != T->p_alt || dx != T->p_dx || dy != T->p_dy) {
        T->p_alt = alt; T->p_dx = dx; T->p_dy = dy;

        /* SO o branco do olho muda por quadro.
         *
         * Esta guarda dispara quase todo quadro, porque `alt` acompanha a
         * respiracao. Na primeira versao do rosto eu coloquei pupila,
         * sobrancelha e chama aqui dentro — e as sobras voltaram na hora.
         * Objeto girado reposicionado 60 vezes por segundo, em modo parcial
         * com buffer de 16 linhas, deixa rastro: a invalidacao da posicao
         * antiga nao cobre o que a rotacao desenhou fora da caixa.
         *
         * O comentario no topo deste arquivo ja dizia isso, e eu passei por
         * cima dele. Fica registrado. */
        int16_t sep = 30 * esc / 236;
        for (int i = 0; i < 2; i++) {
            lv_obj_set_size(T->olho[i], larg, alt);
            lv_obj_align(T->olho[i], LV_ALIGN_CENTER,
                         (i == 0 ? -sep : sep) + dx, dy);
            /* A pupila some quando o olho fecha, senao vaza pela palpebra. */
            lv_obj_set_style_opa(T->pupila[i],
                                 alt > larg / 3 ? LV_OPA_COVER : LV_OPA_TRANSP, 0);
        }
    }

    /* —— rosto: so muda quando o ESTADO ou a ESCALA mudam ——
     * Nada aqui precisa acompanhar a respiracao. Sobrancelha e boca sao
     * expressao, e expressao muda quando o Claude muda de estado, nao 60
     * vezes por segundo. */
    if (T->p_boca != m->alvo || T->p_esc != esc) {
        T->p_boca = m->alvo;
        T->p_esc  = esc;


        int16_t sep = 42 * esc / 236;
        int16_t lg  = 25 * esc / 236;
        int16_t rp  = lg * 52 / 100, rb = lg * 20 / 100;
        if (rp < 3) rp = 3;
        if (rb < 2) rb = 2;

        for (int i = 0; i < 2; i++) {
            int16_t ox = A->olhar_x * (lg / 3) / 100;
            int16_t oy = A->olhar_y * (lg / 3) / 100;
            lv_obj_set_size(T->pupila[i], rp, rp);
            lv_obj_align(T->pupila[i], LV_ALIGN_CENTER, ox, oy);
            lv_obj_set_size(T->brilho[i], rb, rb);
            lv_obj_align(T->brilho[i], LV_ALIGN_CENTER, -rp / 4, -rp / 4);

            /* A inclinacao vira diferenca de altura entre as duas pontas.
             * `externa` espelha o lado esquerdo; com sob_invertida quem sobe
             * e a ponta INTERNA — e e so isso que separa bravo de
             * preocupado. */
            int16_t sl = 46 * esc / 236, sh = 8 * esc / 236;
            if (sh < 2) sh = 2;
            int externa = (i == 0) ? -1 : 1;
            int giro = (A->sob_invertida ? -externa : externa) * A->sob_ang;
            int16_t queda = (int16_t)(sl * giro / 90);   /* aprox. de tan */
            T->sob_pts[i][0].x = 0;
            T->sob_pts[i][0].y = sh + queda / 2;
            T->sob_pts[i][1].x = sl;
            T->sob_pts[i][1].y = sh - queda / 2;
            lv_obj_set_style_line_width(T->sobrancelha[i], sh, 0);
            lv_line_set_points(T->sobrancelha[i], T->sob_pts[i], 2);
            lv_obj_align(T->sobrancelha[i], LV_ALIGN_CENTER,
                         (i == 0 ? -sep : sep),
                         -(A->olho_alt / 2) - (18 - A->sob_dy) * esc / 236);
        }

        int16_t bd, bw, a1, a2;

    /* —— boca ——
     * Um arco so, reposicionado. Angulos do LVGL: 0 grau aponta para as 3
     * horas e cresce no sentido horario, entao 90 e embaixo. Arco embaixo
     * curva para cima e vira sorriso; arco em cima vira aflicao. */
        switch (A->boca) {
            case BOCA_SORRISAO: bd = 74; bw = 9; a1 = 35;  a2 = 145; break;
            case BOCA_SORRISO:  bd = 62; bw = 7; a1 = 55;  a2 = 125; break;
            case BOCA_PEQUENA:  bd = 44; bw = 6; a1 = 68;  a2 = 112; break;
            case BOCA_O:        bd = 26; bw = 9; a1 = 0;   a2 = 359; break;
            case BOCA_ONDA:     bd = 58; bw = 7; a1 = 235; a2 = 305; break;
            default:            bd = 96; bw = 6; a1 = 82;  a2 = 98;  break;
        }
        int16_t dbd = bd * esc / 236, dbw = bw * esc / 236;
        if (dbw < 2) dbw = 2;
        lv_obj_set_size(T->boca, dbd, dbd);
        lv_obj_set_style_arc_width(T->boca, dbw, LV_PART_MAIN);
        lv_arc_set_bg_angles(T->boca, a1, a2);
        /* A onda e um arco de cima: sobe o objeto para a curva cair onde a
         * boca deve estar, em vez de ficar no meio do rosto. */
        int16_t by = (A->boca == BOCA_ONDA ? 58 : 30) * esc / 236;
        lv_obj_align(T->boca, LV_ALIGN_CENTER, 0, by);
    }

    /* Luz do topo: paira acima da cabeca e tremula. Morre no offline —
     * sem o outro lado, nao ha o que arder. */
    if (m->alvo == WISP_OFFLINE) {
        lv_obj_add_flag(T->chama, LV_OBJ_FLAG_HIDDEN);
    } else {
        /* Parada de proposito. Tremular era mover um objeto sobre o fundo a
         * cada quadro — mais uma fonte de rastro, pelo mesmo motivo das
         * sobrancelhas. Quem se mexe aqui e a luz em orbita, que ja da
         * o sinal de movimento.
         *
         * E PARADA TEM DE SIGNIFICAR PARADA. Estas três linhas escreviam os
         * MESMOS valores a cada quadro, e `lv_obj_align` não é guardado pelo
         * LVGL — ele sempre escreve LV_STYLE_ALIGN, que carrega
         * LAYOUT_UPDATE, então invalidava o objeto e sujava o layout do pai
         * 62 vezes por segundo. Medido no simulador: 600px por quadro, 55% de
         * tudo o que este personagem redesenhava, e uma transação de flush
         * extra por quadro. Este arquivo já documenta o que invalidação por
         * quadro custou aqui uma vez: 6 FPS. */
        lv_obj_remove_flag(T->chama, LV_OBJ_FLAG_HIDDEN);
        if (T->p_chama_esc != esc || T->p_chama_x != m->x || T->p_chama_y != m->y) {
            T->p_chama_esc = esc;
            T->p_chama_x = m->x;
            T->p_chama_y = m->y;
            int16_t cd = 16 * esc / 236;
            if (cd < 4) cd = 4;
            lv_obj_set_size(T->chama, cd, cd * 3 / 2);
            lv_obj_set_style_radius(T->chama, cd / 2, 0);
            lv_obj_align(T->chama, LV_ALIGN_CENTER, m->x, m->y - esc / 2 - cd);
        }
    }

    /* Luz: ângulo ACUMULADO e velocidade interpolada. Derivar de
     * (tempo % periodo) fazia a bolinha teleportar ao mudar de estado. */
    float vel_alvo = A->orbita / 255.0f * 5.0f;
    T->vel += (vel_alvo - T->vel) * 0.05f;
    T->ang += T->vel * (PERIODO_MS / 1000.0f);
    if (T->ang > 2.0f * (float) M_PI) T->ang -= 2.0f * (float) M_PI;

    if (A->orbita) {
        lv_obj_remove_flag(T->wisp, LV_OBJ_FLAG_HIDDEN);
        int rx = (esc * 152) / 236, ry = (esc * 94) / 236;
        lv_obj_align(T->wisp, LV_ALIGN_CENTER,
                     m->x + (int16_t)(cosf(T->ang) * rx),
                     m->y + (int16_t)(sinf(T->ang) * ry));
        lv_obj_set_style_bg_color(T->wisp, lv_color_make(C->r, C->g, C->b), 0);
    } else {
        lv_obj_add_flag(T->wisp, LV_OBJ_FLAG_HIDDEN);
    }

    /* Interrogações subindo: só no modo de sessão única. */
    if (sozinho && A->interrog) {
        for (int i = 0; i < QTD_INTERROG; i++) {
            lv_obj_remove_flag(g_interrog[i], LV_OBJ_FLAG_HIDDEN);
            uint32_t ciclo = (agora + i * 1000) % 3000;
            float sobe = ciclo / 3000.0f;
            lv_obj_align(g_interrog[i], LV_ALIGN_CENTER,
                         (int16_t)(sinf(sobe * 3.4f + i * 2.2f) * 24) + (i - 1) * 13,
                         -132 - (int16_t)(sobe * 96));
            lv_obj_set_style_opa(g_interrog[i],
                                 (lv_opa_t)(sinf(sobe * (float) M_PI) * 255), 0);
            lv_obj_set_style_text_color(g_interrog[i],
                                        lv_color_make(C->r, C->g, C->b), 0);
        }
    } else if (sozinho) {
        for (int i = 0; i < QTD_INTERROG; i++)
            lv_obj_add_flag(g_interrog[i], LV_OBJ_FLAG_HIDDEN);
    }
}

/* ————————————————————————————————————————————————
 *  Layout: só a parte que é do personagem
 * ————————————————————————————————————————————————
 * Saiu de aplicar_layout() em ui.c. O que ficou lá: a vaga, os rótulos e as
 * outras telas. O que veio para cá: tudo o que tem forma de computador. */
static void terminal_dispor(mascote_t *m, bool mostrar, const wisp_cfg_t *cfg)
{
    terminal_t *T = m->interno;
    if (!T) return;

    /* A vaga vem do próprio mascote: é o layout que acabou de escrevê-la. */
    const int16_t d = m->d, x = m->x, y = m->y;

    /* A chama entra na lista porque é IRMÃ do corpo, não filha: olhos e boca
     * somem por herança, ela não, e ficaria pairando sozinha sobre o relógio.
     * A foto entra pelo mesmo motivo. Armadilha que já pegou o rodapé antes. */
    lv_obj_t *tudo[] = {T->corpo, T->wisp, T->chama, T->foto,
                        T->braco[0], T->braco[1]};
    for (size_t k = 0; k < sizeof(tudo) / sizeof(tudo[0]); k++)
        if (tudo[k]) {
            if (mostrar) lv_obj_remove_flag(tudo[k], LV_OBJ_FLAG_HIDDEN);
            else         lv_obj_add_flag(tudo[k], LV_OBJ_FLAG_HIDDEN);
        }
    for (int k = 0; k < QTD_INTERROG; k++)
        if (g_interrog[k]) lv_obj_add_flag(g_interrog[k], LV_OBJ_FLAG_HIDDEN);
    if (!mostrar) return;

    /* O DEGRAU DE TAMANHO VALE NOS DOIS CAMINHOS.
     *
     * Este personagem tem duas renderizações — a foto mapeada da partição e o
     * desenho vetorial de fallback — e o ajuste tem de valer nas duas, senão ele
     * funciona ou não dependendo de a partição de assets ter montado, o que é
     * invisível para quem escolheu.
     *
     * Mas eles escalam de formas DIFERENTES:
     *
     *   vetorial  as partes são todas fração de `dv`. Barato: objeto
     *             redimensionado, nenhuma transformação.
     *   foto      escala por lv_image_set_scale, porque imagem MAIOR que o
     *             objeto sai CORTADA e não reduzida — um pedaço de computador
     *             não é um computador menor. Isso transforma por software, e a
     *             conta que travou esta placa uma vez foram 93.636 pixels a
     *             ~0,76 µs. Aceitável aqui e só aqui porque `dispor` roda na
     *             mudança de layout ou de ajuste, nunca por quadro, e a guarda
     *             abaixo é o que faz "nunca" ser verdade.
     *
     * A ESCALA SAI DA LARGURA NATURAL DA ARTE, não de um 306 escrito à mão.
     * A versão anterior escalava pela porcentagem do ajuste e nada mais, o que
     * só funciona enquanto a vaga tiver exatamente o tamanho da arte — e
     * amarrava o 306 de vaga_de() à resolução dos PNG, através de uma interface
     * que não tem como dizer isso. `carregar_fotos()` já lê `header.w` do próprio
     * asset justamente para a medida não estar no código; aqui ela é usada. */
    static const uint8_t PCT[WISP_TAM_QTD] = {70, 100, 118};
    const int16_t dv = (int16_t) ((int32_t) d * PCT[cfg->tamanho] / 100);

    /* Com foto, o boneco desenhado inteiro sai de cena. Deixar os dois
     * visíveis não daria um híbrido, daria olhos flutuando sobre a imagem. */
    if (T->foto) {
        lv_obj_t *desenho[] = {T->corpo, T->wisp, T->chama,
                               T->braco[0], T->braco[1]};
        for (size_t k = 0; k < 5; k++)
            lv_obj_add_flag(desenho[k], LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_size(T->foto, dv, dv);
        lv_obj_align(T->foto, LV_ALIGN_CENTER, x, y);
        /* Os oito assets são um conjunto só, convertidos juntos da mesma pasta,
         * então a largura de qualquer um serve como a natural. */
        const int32_t nat = s_dsc[WISP_IDLE].header.w;
        if (T->p_escala_dv != dv && nat > 0) {
            T->p_escala_dv = dv;
            lv_image_set_scale(T->foto, (int32_t) 256 * dv / nat);
        }
    }

    /* Carcaça: quadrada com cantos generosos — o Macintosh original.
     * A tela ocupa 70% dela e fica deslocada para cima, deixando embaixo a
     * faixa onde ficaria o drive de disquete. */
    lv_obj_set_size(T->corpo, dv, dv);
    lv_obj_set_style_radius(T->corpo, dv * 22 / 100, 0);
    lv_obj_align(T->corpo, LV_ALIGN_CENTER, x, y);

    int16_t td = dv * 70 / 100, th = td * 82 / 100;
    int16_t ty = -dv * 6 / 100, tr = td * 12 / 100;

    /* Moldura 4% maior que a tela e 2% mais baixa: a sobra aparece só em cima,
     * que é onde a sombra de um vão afundado cai. */
    lv_obj_set_size(T->moldura, td + dv * 5 / 100, th + dv * 5 / 100);
    lv_obj_set_style_radius(T->moldura, tr + dv * 2 / 100, 0);
    lv_obj_align(T->moldura, LV_ALIGN_CENTER, 0, ty - dv * 1 / 100);

    lv_obj_set_size(T->tela, td, th);
    lv_obj_set_style_radius(T->tela, tr, 0);
    lv_obj_align(T->tela, LV_ALIGN_CENTER, 0, ty);

    /* Luz: cobre o terço superior da tela e some para baixo. */
    lv_obj_set_size(T->luz, td, th * 62 / 100);
    lv_obj_set_style_radius(T->luz, tr, 0);
    lv_obj_align(T->luz, LV_ALIGN_TOP_MID, 0, 0);

    int16_t sh = dv / 90; if (sh < 1) sh = 1;
    for (int i = 0; i < 2; i++) {
        lv_obj_set_size(T->scan[i], td, sh);
        lv_obj_align(T->scan[i], LV_ALIGN_CENTER, 0,
                     (i == 0 ? -1 : 1) * th * 26 / 100);
    }

    /* Faixa de luz no topo da carcaça, acompanhando o arredondamento. */
    lv_obj_set_size(T->topo, dv * 72 / 100, dv * 26 / 100);
    lv_obj_set_style_radius(T->topo, dv * 13 / 100, 0);
    lv_obj_align(T->topo, LV_ALIGN_TOP_MID, 0, dv * 4 / 100);

    /* Braços: menores, mais baixos e da cor da SOMBRA da carcaça. Antes eram
     * claros e do tamanho de asas — pareciam algodão colado. */
    int16_t bl = dv * 10 / 100, bh = dv * 20 / 100;
    for (int b = 0; b < 2; b++) {
        lv_obj_set_size(T->braco[b], bl, bh);
        lv_obj_set_style_radius(T->braco[b], bl / 2, 0);
        lv_obj_align(T->braco[b], LV_ALIGN_CENTER,
                     x + (b == 0 ? -1 : 1) * (dv / 2 + bl / 4),
                     y + dv * 22 / 100);
    }

    T->p_alt = -1;   /* força reposicionar os olhos na nova escala */
}

/* ————————————————————————————————————————————————
 *  Construção
 * ———————————————————————————————————————————————— */
static void terminal_criar(lv_obj_t *pai, mascote_t *m)
{
    terminal_t *T = lv_malloc_zeroed(sizeof(terminal_t));
    m->interno = T;
    if (!T) { ESP_LOGE(TAG, "sem memoria para o mascote"); return; }

    /* Os assets são do personagem, não do layout: quem sabe se precisa de arte
     * é quem desenha. Uma vez só, no primeiro mascote. */
    static bool tentou_assets = false;
    if (!tentou_assets) { tentou_assets = true; carregar_fotos(); }

    /* CARCACA: a caixa creme do computador. */
    T->corpo = lv_obj_create(pai);
    lv_obj_set_style_border_width(T->corpo, 0, 0);
    lv_obj_set_style_pad_all(T->corpo, 0, 0);
    lv_obj_set_style_bg_color(T->corpo, C_CARCACA_T, 0);
    lv_obj_set_style_bg_grad_color(T->corpo, C_CARCACA_B, 0);
    lv_obj_set_style_bg_grad_dir(T->corpo, LV_GRAD_DIR_VER, 0);
    so_decoracao(T->corpo);

    /* BRACOS: dois toquinhos nas laterais. Filhos da carcaca, entao somem
     * junto com ela sem precisar entrar em lista nenhuma. */
    for (int i = 0; i < 2; i++) {
        T->braco[i] = lv_obj_create(pai);
        lv_obj_set_style_border_width(T->braco[i], 0, 0);
        lv_obj_set_style_bg_color(T->braco[i], C_CARCACA_B, 0);
        lv_obj_set_style_pad_all(T->braco[i], 0, 0);
        so_decoracao(T->braco[i]);
    }

    /* MOLDURA: um retangulo escuro logo atras da tela, deslocado para baixo.
     * E o truque mais barato de profundidade que existe — o olho le a sombra
     * na borda de cima como "isto esta AFUNDADO na carcaca". Sem ela, tela e
     * carcaca parecem adesivos no mesmo plano. */
    T->moldura = lv_obj_create(T->corpo);
    lv_obj_set_style_border_width(T->moldura, 0, 0);
    lv_obj_set_style_bg_color(T->moldura, lv_color_make(150, 138, 118), 0);
    lv_obj_set_style_pad_all(T->moldura, 0, 0);
    so_decoracao(T->moldura);

    /* BRILHO DO TOPO: faixa clara na parte de cima da carcaca. Plastico
     * arredondado sob luz de cima tem essa banda; sem ela a caixa e um
     * retangulo pintado. */
    T->topo = lv_obj_create(T->corpo);
    lv_obj_set_style_border_width(T->topo, 0, 0);
    lv_obj_set_style_bg_color(T->topo, lv_color_white(), 0);
    lv_obj_set_style_bg_opa(T->topo, LV_OPA_30, 0);
    lv_obj_set_style_bg_grad_color(T->topo, lv_color_white(), 0);
    lv_obj_set_style_bg_grad_dir(T->topo, LV_GRAD_DIR_VER, 0);
    lv_obj_set_style_bg_main_opa(T->topo, LV_OPA_40, 0);
    lv_obj_set_style_bg_grad_opa(T->topo, LV_OPA_TRANSP, 0);
    lv_obj_set_style_pad_all(T->topo, 0, 0);
    so_decoracao(T->topo);

    /* TELA: o rosto. E ela que recebe a cor do estado. */
    T->tela = lv_obj_create(T->corpo);
    lv_obj_set_style_border_width(T->tela, 0, 0);
    lv_obj_set_style_pad_all(T->tela, 0, 0);
    lv_obj_set_style_bg_grad_dir(T->tela, LV_GRAD_DIR_VER, 0);
    so_decoracao(T->tela);

    /* LUZ: nucleo claro no meio da tela, esmaecendo para baixo. Um CRT nao
     * ilumina por igual — o centro estoura e as bordas caem. E o que mais
     * faz a tela parecer ACESA em vez de pintada. */
    T->luz = lv_obj_create(T->tela);
    lv_obj_set_style_border_width(T->luz, 0, 0);
    lv_obj_set_style_bg_color(T->luz, lv_color_white(), 0);
    lv_obj_set_style_bg_grad_color(T->luz, lv_color_white(), 0);
    lv_obj_set_style_bg_grad_dir(T->luz, LV_GRAD_DIR_VER, 0);
    lv_obj_set_style_bg_main_opa(T->luz, LV_OPA_40, 0);
    lv_obj_set_style_bg_grad_opa(T->luz, LV_OPA_TRANSP, 0);
    lv_obj_set_style_pad_all(T->luz, 0, 0);
    so_decoracao(T->luz);

    /* SCANLINES: duas faixas escuras finas. Sao elas que dizem "isto e uma
     * tela de varredura", nao um retangulo laranja. */
    for (int i = 0; i < 2; i++) {
        T->scan[i] = lv_obj_create(T->tela);
        lv_obj_set_style_border_width(T->scan[i], 0, 0);
        lv_obj_set_style_radius(T->scan[i], 0, 0);
        lv_obj_set_style_bg_color(T->scan[i], lv_color_black(), 0);
        lv_obj_set_style_bg_opa(T->scan[i], LV_OPA_10, 0);
        lv_obj_set_style_pad_all(T->scan[i], 0, 0);
        so_decoracao(T->scan[i]);
    }

    for (int i = 0; i < 2; i++) {
        /* BRANCO do olho. Antes o olho inteiro era escuro — duas frestas num
         * corpo redondo, que lia como focinho. Olho de verdade tem branco,
         * pupila e um ponto de brilho; sem o brilho ele vira buraco. */
        T->olho[i] = lv_obj_create(T->tela);
        /* Olho de PIXEL: quadrado com canto minimo. Numa tela CRT o
         * rosto e desenhado por blocos, nao por formas organicas. */
        lv_obj_set_style_radius(T->olho[i], 2, 0);
        lv_obj_set_style_border_width(T->olho[i], 0, 0);
        lv_obj_set_style_bg_color(T->olho[i], lv_color_make(74, 44, 18), 0);
        lv_obj_set_style_pad_all(T->olho[i], 0, 0);
        so_decoracao(T->olho[i]);

        T->pupila[i] = lv_obj_create(T->olho[i]);
        lv_obj_set_style_radius(T->pupila[i], LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_border_width(T->pupila[i], 0, 0);
        lv_obj_set_style_bg_color(T->pupila[i], lv_color_make(26, 18, 24), 0);
        lv_obj_set_style_pad_all(T->pupila[i], 0, 0);
        so_decoracao(T->pupila[i]);

        T->brilho[i] = lv_obj_create(T->pupila[i]);
        lv_obj_set_style_radius(T->brilho[i], LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_border_width(T->brilho[i], 0, 0);
        lv_obj_set_style_bg_color(T->brilho[i], lv_color_white(), 0);
        lv_obj_set_style_pad_all(T->brilho[i], 0, 0);
        so_decoracao(T->brilho[i]);

        /* Sobrancelha como LINHA, nao como retangulo girado.
         *
         * A primeira versao usava transform_rotation, e isso custou caro: no
         * LVGL 9 qualquer objeto transformado e renderizado num buffer de
         * CAMADA temporario. Oito sobrancelhas = oito camadas disputando a
         * RAM interna, que aqui e o recurso mais escasso da placa. O
         * resultado foi `Failed to allocate priv TX buffer` e o desenho
         * falhando de vez — que na tela aparece como coisa sobre coisa,
         * porque o pixel velho nunca e coberto.
         *
         * Uma linha de dois pontos ja nasce inclinada. Zero camada. */
        T->sobrancelha[i] = lv_line_create(T->tela);
        lv_obj_set_style_line_color(T->sobrancelha[i], lv_color_make(40, 24, 22), 0);
        lv_obj_set_style_line_opa(T->sobrancelha[i], LV_OPA_80, 0);
        lv_obj_set_style_line_rounded(T->sobrancelha[i], true, 0);
        so_decoracao(T->sobrancelha[i]);
    }

    /* Boca: UM arco serve para todas as formas. Voltado para baixo vira
     * sorriso, para cima vira aflicao, fechado em 360 vira o "o" de surpresa.
     * Sete widgets diferentes dariam o mesmo resultado com sete vezes mais
     * objetos na tela — e objeto custa varredura. */
    T->boca = lv_arc_create(T->tela);
    lv_obj_remove_style(T->boca, NULL, LV_PART_KNOB);
    lv_obj_set_style_arc_width(T->boca, 0, LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(T->boca, lv_color_make(40, 24, 22), LV_PART_MAIN);
    lv_obj_set_style_arc_opa(T->boca, LV_OPA_80, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(T->boca, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(T->boca, 0, 0);
    so_decoracao(T->boca);
    T->p_boca = -1;

    /* Mascote de imagem. Fica ACIMA de tudo e, quando existe, o desenho
     * inteiro e escondido — nao ha meio-termo entre os dois. */
    if (s_tem_fotos) {
        T->foto = lv_image_create(pai);
        so_decoracao(T->foto);
    }

    /* A luz que paira acima da cabeca: um will-o-the-wisp, que e de onde vem o nome. */
    T->chama = lv_obj_create(pai);
    lv_obj_set_style_radius(T->chama, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_border_width(T->chama, 0, 0);
    lv_obj_set_style_bg_color(T->chama, lv_color_make(255, 214, 130), 0);
    lv_obj_set_style_bg_grad_color(T->chama, lv_color_make(250, 140, 50), 0);
    lv_obj_set_style_bg_grad_dir(T->chama, LV_GRAD_DIR_VER, 0);
    lv_obj_set_style_pad_all(T->chama, 0, 0);
    so_decoracao(T->chama);

    T->wisp = lv_obj_create(pai);
    lv_obj_set_size(T->wisp, 14, 14);
    lv_obj_set_style_radius(T->wisp, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_border_width(T->wisp, 0, 0);
    lv_obj_add_flag(T->wisp, LV_OBJ_FLAG_HIDDEN);
    so_decoracao(T->wisp);

    if (!g_interrog_prontas) {
        g_interrog_prontas = true;
    for (int i = 0; i < QTD_INTERROG; i++) {
        g_interrog[i] = lv_label_create(pai);
        lv_label_set_text(g_interrog[i], "?");
        lv_obj_set_style_text_font(g_interrog[i], &lv_font_montserrat_38, 0);
        lv_obj_add_flag(g_interrog[i], LV_OBJ_FLAG_HIDDEN);
    }
    }

    T->olho_alt = 6;
    T->p_alt = -1;
    T->ang = -(float) M_PI / 2;
}

/* Apaga os objetos de nível superior. Tudo o que é filho deles morre por
 * herança: moldura, topo, tela, luz, scan, olho, pupila, brilho, sobrancelha e
 * boca são todos descendentes de `corpo`. */
static void terminal_destruir(mascote_t *m)
{
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
     * aparece longe da causa: lixo ou crash ao entrar em `asking`, muitas trocas
     * depois de a troca ter "funcionado".
     *
     * Quem decide é a MESMA flag que a criação usa. A primeira versão contava
     * quantos mascotes já tinham saído e apagava no último — um segundo
     * mecanismo, mais frágil, para responder o que o primeiro já respondia: o
     * contador dessincronizava para sempre se alguma vez alguém destruísse um
     * subconjunto, e passou a ser exatamente o caso quando a criação virou
     * preguiçosa. É seguro porque ui_personagem() destrói todos em sequência,
     * sob o mutex, e nada anima entre o primeiro e o último. */
    if (g_interrog_prontas) {
        for (int i = 0; i < QTD_INTERROG; i++) {
            if (g_interrog[i]) lv_obj_delete(g_interrog[i]);
            g_interrog[i] = NULL;
        }
        g_interrog_prontas = false;
    }
}

const personagem_t MASCOTE_TERMINAL = {
    .nome       = "terminal",
    .usa_assets = true,
    .criar      = terminal_criar,
    .animar     = terminal_animar,
    .dispor     = terminal_dispor,
    .destruir   = terminal_destruir,
};
