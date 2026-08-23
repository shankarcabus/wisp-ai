/* Primitivas de desenho compartilhadas entre o layout e os personagens.
 *
 * Estavam dentro do ui.c e declaradas no mascote.h, o que fazia o header cuja
 * função declarada é "a fronteira entre PERSONAGEM e LAYOUT" apontar de volta
 * para o layout: os personagens dependiam do ui.c e o ui.c dependia deles. Aqui
 * a dependência é de mão única — personagem -> primitivas, layout -> primitivas.
 *
 * `so_decoracao()` não é cosmética: ela tira o objeto do caminho do gesto, e sem
 * ela um enfeite engole o arrasto de que o tileview precisa para deslizar. Por
 * isso ela é a primeira coisa que `disco()` e `barra()` chamam, e por isso todo
 * objeto decorativo devia nascer por uma destas funções. */
#pragma once

#include "lvgl.h"

/* Tira o objeto do caminho: sem scroll, sem clique, e o gesto sobe para o pai. */
void so_decoracao(lv_obj_t *o);

/* Círculo de diâmetro `d`, cor cheia, centrado em (x, y) relativo ao pai. */
lv_obj_t *disco(lv_obj_t *pai, int d, lv_color_t cor, int x, int y);

/* Retângulo de cantos arredondados (pílula), centrado em (x, y). */
lv_obj_t *barra(lv_obj_t *pai, int w, int h, lv_color_t cor, int x, int y);
