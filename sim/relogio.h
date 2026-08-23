/* Relógio virtual do simulador.
 *
 * POR QUE NÃO SDL_GetTicks
 * ------------------------
 * O mascote respira, flutua e pisca contra lv_tick_get(). Ligado ao relógio do
 * sistema, duas execuções da mesma sequência de comandos caem em fases
 * diferentes da respiração e produzem imagens diferentes — o que torna
 * impossível comparar duas capturas byte a byte.
 *
 * Essa comparação é a rede de proteção da extração do personagem: "o Terminal
 * não mudou" só é uma afirmação verificável se a mesma entrada der exatamente
 * a mesma saída. Então o tempo aqui é contado, não medido.
 *
 * O passo é 16ms — o mesmo período do timer de animação do ui.c (PERIODO_MS),
 * escolhido lá para acompanhar LV_DEF_REFR_PERIOD=15. */
#pragma once

#include <stdint.h>

#define RELOGIO_PASSO_MS 16

/* Callback para lv_tick_set_cb(). */
uint32_t relogio_agora(void);

/* Avança um passo. Chamado pelo laço principal e por quem precise deixar a
 * interface assentar antes de capturar. */
void relogio_avancar(void);
