/* A fronteira entre PERSONAGEM e LAYOUT.
 *
 * Existe para que um segundo personagem não signifique um segundo ui.c.
 *
 *   layout (ui.c)      onde o mascote fica, de que tamanho, quando aparece,
 *                      os rótulos de detalhe e projeto, e as outras telas.
 *   personagem         como ele é desenhado e como reage ao estado.
 *
 * A escolha do personagem é DADO — vem da NVS. Não há e não pode haver `#if`
 * de target aqui: as duas placas têm o mesmo painel de 480x480 e as duas rodam
 * os dois personagens. A regra do CLAUDE.md continua valendo inteira.
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "lvgl.h"
#include "ui.h"

/* A parte de um mascote que o LAYOUT conhece. Tudo o mais que um personagem
 * precisa vive no bloco opaco `interno`, alocado por ele em criar(). */
/* Período do timer de animação. 16ms para acompanhar o refresh do LVGL
 * (LV_DEF_REFR_PERIOD=15) — com 33ms a luz só se movia em metade dos quadros e
 * parecia travada. Vive aqui porque é o contrato entre o timer, que é do
 * layout, e a animação, que é do personagem. */
#define PERIODO_MS 16

typedef struct {
    lv_obj_t     *detail;        /* rótulo de detalhe, sob o mascote */
    lv_obj_t     *project;       /* rótulo de projeto: uma linha por sessão */
    wisp_state_t  alvo, anterior;
    int16_t       d;             /* diâmetro atual, vindo de vaga_de() */
    /* Deslocamento do centro da vaga. Guardado aqui porque o personagem
     * precisa dele para posicionar o que orbita o corpo, e buscá-lo chamando
     * vaga_de() de dentro do personagem seria o layout vazando para o lado
     * errado da fronteira. */
    int16_t       x, y;
    char          ult_detalhe[40], ult_projeto[28];
    void         *interno;
} mascote_t;

typedef struct {
    const char *nome;            /* "terminal", "bytelo" — o valor da NVS */
    bool        usa_assets;      /* true = tenta a partição `storage` */
    /* Se o layout deve mostrar os rótulos de detalhe e projeto sob o mascote.
     *
     * É propriedade do PERSONAGEM porque é decisão de composição dele, mas os
     * rótulos continuam sendo do layout: qualquer personagem os teria iguais, e
     * quem os cria e posiciona é o ui.c.
     *
     * Desligado custa informação: aqueles dois textos são a ferramenta em
     * execução e a lista de projetos, então sem eles a tela diz o ESTADO e não
     * diz qual sessão. */
    bool        rotulos;

    /* Uma vez por mascote, na construção da tela. */
    void (*criar)(lv_obj_t *pai, mascote_t *m);

    /* A cada quadro, pelo timer de animação, com o mutex do LVGL JÁ na mão —
     * não chamar bsp_display_lock() aqui dentro, é deadlock. */
    void (*animar)(mascote_t *m, uint32_t agora, bool sozinho);

    /* Quando o layout muda de tamanho, de posição ou de visibilidade.
     * `mostrar` false esconde tudo o que pertence ao personagem. */
    void (*dispor)(mascote_t *m, int16_t d, int16_t x, int16_t y, bool mostrar);

    /* Desfaz o que criar() fez: apaga os objetos e libera o bloco `interno`.
     * Chamada com o mutex do LVGL JÁ na mão.
     *
     * Cuidado com o que NÃO morre por herança: objeto criado como IRMÃO da raiz
     * do personagem — a chama do Terminal, o adorno do Bytelo — tem de ser
     * apagado à mão, e é a mesma armadilha que os `dispor` dos dois já
     * documentam. */
    void (*destruir)(mascote_t *m);
} personagem_t;

/* O registro. Índice 0 é o padrão. */
extern const personagem_t *const MASCOTES[];
extern const int MASCOTES_QTD;

/* Nome -> personagem. Vazio, NULL ou desconhecido devolve MASCOTES[0], e avisa
 * no log quando o nome veio errado: um personagem coerente ganha de um estado
 * inválido na tela. */
const personagem_t *mascote_por_nome(const char *nome);

/* Escolhe o personagem ativo. Chamar ANTES de ui_create(). */
void mascote_escolher(const personagem_t *p);
const personagem_t *mascote_ativo(void);

/* —— helpers de desenho, definidos em ui.c —— */
void      so_decoracao(lv_obj_t *o);
lv_obj_t *disco(lv_obj_t *pai, int d, lv_color_t cor, int x, int y);
lv_obj_t *barra(lv_obj_t *pai, int w, int h, lv_color_t cor, int x, int y);

/* —— os personagens que existem —— */
extern const personagem_t MASCOTE_TERMINAL;
extern const personagem_t MASCOTE_BYTELO;
