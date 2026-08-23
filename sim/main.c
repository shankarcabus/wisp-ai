/* Simulador da tela do Wisp.
 *
 * Roda firmware/main/ui.c sem modificação: os headers de ESP-IDF que ele
 * inclui resolvem para sim/shim/, que vem antes na ordem de include.
 *
 * NÃO PROVA CUSTO DE RENDER. O Mac tem CPU e RAM de sobra e nenhuma das
 * restrições da placa existe aqui. Isto serve para ver layout, expressão e
 * composição; número de FPS e RAM se medem na placa. */
#include <poll.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <SDL2/SDL.h>

#include "cena.h"
#include "mascote.h"
#include "relogio.h"
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

/* Display de mentira para o modo headless: o LVGL desenha, ninguém mostra.
 *
 * A captura por lv_snapshot não lê deste buffer — ela redesenha a árvore num
 * buffer próprio. Este existe só porque o LVGL exige um display para ter tela
 * ativa, timers de refresh e contexto de desenho. */
static void flush_nada(lv_display_t *disp, const lv_area_t *area, uint8_t *px)
{
    (void) area; (void) px;
    lv_display_flush_ready(disp);
}

static lv_display_t *display_headless(void)
{
    /* PARTIAL com um buffer de dez linhas: é o mesmo modo de render da placa
     * (ui.c documenta que rodamos PARTIAL), então o caminho de desenho
     * exercitado aqui é o mesmo. */
    static uint8_t buf[480 * 10 * 2];
    lv_display_t *d = lv_display_create(480, 480);
    lv_display_set_buffers(d, buf, NULL, sizeof(buf), LV_DISPLAY_RENDER_MODE_PARTIAL);
    lv_display_set_flush_cb(d, flush_nada);
    return d;
}

int main(int argc, char **argv)
{
    bool headless = false;
    for (int i = 1; i < argc; i++)
        if (!strcmp(argv[i], "--headless")) headless = true;

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
    lv_tick_set_cb(relogio_agora);
    lv_delay_set_cb(SDL_Delay);

    /* Headless é o modo da FOLHA DE CONTATO, e existe por determinismo.
     *
     * Com janela SDL, duas execuções da mesma sequência de comandos não davam
     * os mesmos bytes: medido, a primeira execução após um build diferia das
     * seguintes, que eram idênticas entre si. A causa está nos eventos que o
     * sistema entrega ao lançar uma janela, não no relógio — o tick virtual no
     * instante da captura era igual nos dois casos. Sem janela, sem eventos,
     * sem indeterminação.
     *
     * A janela continua sendo o modo padrão: é para olhar. */
    lv_display_t *disp = NULL;
    if (headless) {
        disp = display_headless();
        printf("I (sim) headless: sem janela, captura determinística\n");
    } else {
        disp = lv_sdl_window_create(480, 480);
        lv_sdl_window_set_title(disp, "Wisp — simulador da placa");
    }
    (void) disp;
    /* SEM indev de mouse, de propósito.
     *
     * lv_sdl_mouse_create() alimenta o LVGL com a posição do mouse REAL da
     * máquina. O tileview é rolável, então o ponteiro passando por cima da
     * janela mexe na posição de scroll — e aí duas execuções da mesma sequência
     * de comandos dão capturas diferentes. Foi exatamente o que aconteceu:
     * 25063 pixels de diferença no painel de limites, com o mascote e o painel
     * idênticos, o que denuncia deslocamento de scroll e não animação.
     *
     * O simulador é dirigido por stdin; o comando `tile` cobre o que o dedo
     * faria. Reabilitar o mouse aqui reintroduz a indeterminação e invalida a
     * folha de contato. */

    /* O personagem é escolhido NO BOOT, como na placa — lá vem da NVS, aqui de
     * WISP_MASCOT. Não há comando para trocar em tempo de execução, e isso é
     * deliberado: reconstruir a tela deixaria os objetos compartilhados de um
     * personagem (as interrogações do Terminal, criadas uma vez) apontando para
     * memória liberada. Trocar de personagem é relançar:
     *
     *     WISP_MASCOT=pixel ./sim/build/wisp-sim
     */
    const personagem_t *personagem = mascote_por_nome(getenv("WISP_MASCOT"));
    mascote_escolher(personagem);
    printf("I (sim) personagem: %s\n", personagem->nome);

    /* ui.h: ui_create() precisa ser chamada com o mutex do LVGL na mão. */
    bsp_display_lock(0);
    ui_create();
    bsp_display_unlock();

    if (!headless) printf("I (sim) janela aberta; ctrl-c para sair\n");
    cena_init();
    cena_ajuda();
    /* Sem o lock na mão: ui_update() pega o mutex por conta própria (ui.h), e
     * com um mutex não recursivo isto seria travamento na primeira linha. */
    ui_update(cena_atual());

    for (;;) {
        bsp_display_lock(0);
        relogio_avancar();
        lv_timer_handler();
        bsp_display_unlock();

        /* stdin sem bloquear: se houver linha, aplica. */
        struct pollfd p = {.fd = 0, .events = POLLIN};
        if (poll(&p, 1, 0) > 0 && (p.revents & POLLIN)) {
            char linha[128];
            if (fgets(linha, sizeof(linha), stdin)) cena_comando(linha);
        }

        /* Ritmo de parede fixo, e não o que lv_timer_handler sugere: o tempo
         * que o LVGL vê é o relógio virtual, então quem manda no passo é ele.
         * Um passo por iteração mantém a animação na velocidade certa. */
        SDL_Delay(RELOGIO_PASSO_MS);
    }
    return 0;
}
