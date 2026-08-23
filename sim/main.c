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
