#include "cena.h"

#include <SDL2/SDL.h>

#include "captura.h"
#include "relogio.h"
#include "lvgl.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Teto do assentamento: 60 passos de 16ms = ~1s de tempo virtual, folgado
 * para os ~300ms da animação do tileview. */
#define ASSENTAR_MAX 60

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
           "          tile <0|1> | bat <pct|-1> | lim | nolim\n"
           "          shot <arquivo.bmp> | quit | ?\n"
           "estados : idle working tool asking waiting done error offline\n");
}

const wisp_data_t *cena_atual(void) { return &d; }

void cena_comando(const char *linha)
{
    /* `arg` do tamanho de um CAMINHO, não de uma palavra.
     *
     * Com 32 bytes o `%31s` truncava silenciosamente o caminho passado a
     * `shot`, o fopen falhava e a folha de contato saía vazia sem que nada
     * óbvio explicasse. Caminho absoluto de projeto passa fácil de 60 bytes. */
    char cmd[32] = "", arg[512] = "";
    int  i = 0;
    if (sscanf(linha, "%31s %511s %d", cmd, arg, &i) < 1) return;

    if (!strcmp(cmd, "n")) {
        /* Atenção ao que este número faz: a placa desenha UM mascote sempre
         * (ui.c:1319 força n = 1, com a justificativa logo acima). O que muda
         * com 2, 3 ou 4 é a LISTA de sessões sob o rótulo e o "+N". As vagas
         * de 178px e 140px em vaga_de() são inalcançáveis hoje. */
        int q = atoi(arg);
        d.session_count = q < 1 ? 1 : (q > WISP_MAX_SESSIONS ? WISP_MAX_SESSIONS : q);
    } else if (!strcmp(cmd, "s")) {
        if (i < 0 || i >= WISP_MAX_SESSIONS) i = 0;
        d.sessions[i].state = ui_state_from_text(arg);
    } else if (!strcmp(cmd, "todos")) {
        wisp_state_t e = ui_state_from_text(arg);
        for (int k = 0; k < WISP_MAX_SESSIONS; k++) d.sessions[k].state = e;
    } else if (!strcmp(cmd, "rest")) {
        /* Repouso exige LISTA VAZIA além do silêncio: ui.c:1313-1315 monta a
         * condição como `sem_sessao && clock && rest_s > 0 && ocioso_bastante`.
         * Só adiantar age_s não entra em repouso — foi o que eu tentei
         * primeiro, e a tela não mudava. */
        d.session_count = 0;
        d.age_s = d.rest_s + 60;
    } else if (!strcmp(cmd, "wake")) {
        d.session_count = 1;
        d.age_s = 3;
    } else if (!strcmp(cmd, "tile")) {
        ui_swipe(atoi(arg) == 1 ? +1 : -1);
        return;                       /* ui_swipe já desenha */
    } else if (!strcmp(cmd, "quit")) {
        /* Existe para a captura em lote ser SÍNCRONA. Sem isto o simulador
         * roda para sempre — o que é certo no modo interativo — e um roteiro
         * como `printf ... | wisp-sim` obriga quem chama a adivinhar quando os
         * arquivos ficaram prontos, matar o processo e torcer. Com `quit` no
         * fim do roteiro, o pipe termina quando o trabalho terminou. */
        printf("I (sim) fim do roteiro\n");
        exit(0);
    } else if (!strcmp(cmd, "shot")) {
        /* Deixa a interface assentar antes de ler os pixels. Dois ciclos não
         * bastam: o tileview desliza com animação, e uma captura logo depois de
         * `tile 1` pega a tela no MEIO do deslizamento — foi o que aconteceu na
         * primeira tentativa, e a imagem parece um bug de layout sem ser.
         *
         * O número de passos é FIXO, e não "até as animações pararem". Sair
         * cedo faz o tempo virtual no momento da captura depender do estado, e
         * como a respiração do mascote é função do tick absoluto, isso é o
         * suficiente para dois arquivos diferirem. Passo fixo torna o instante
         * da captura uma função só da sequência de comandos. */
        for (int k = 0; k < ASSENTAR_MAX; k++) {
            relogio_avancar();
            lv_timer_handler();
        }
        captura_bmp(arg);
        return;
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
