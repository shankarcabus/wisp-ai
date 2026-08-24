#include "cena.h"

#include "captura.h"
#include "mascote.h"
#include "relogio.h"
#include "lvgl.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Teto do assentamento: 60 passos de 16ms = ~1s de tempo virtual, folgado
 * para os ~300ms da animação do tileview. */
#define ASSENTAR_MAX 60

static wisp_data_t d;

/* Tradução própria de nome para estado, em vez de ui_state_from_text().
 *
 * Aquela função é o parser do campo `st` do bridge, e por isso NÃO conhece
 * "offline": offline é estado interno da placa, "ainda não conectou", e nunca
 * chega pela rede. Pedir `offline` ao simulador caía silenciosamente em
 * WISP_IDLE — a folha de contato capturava idle duas vezes e o oitavo estado
 * nunca era visto. */
static wisp_state_t estado_de(const char *nome)
{
    const int i = ui_state_index(nome);
    if (i >= 0) return (wisp_state_t) i;
    printf("W (sim) estado \"%s\" nao existe — usando idle\n", nome);
    return WISP_IDLE;
}

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

/* ————————————————————————————————————————————————
 *  Exportar o personagem como sprite para o app do Mac
 * ————————————————————————————————————————————————
 * O app já sabe carregar conjuntos de `~/.wisp/mascots/<nome>/`: oito PNG, um
 * por estado, fundo transparente, mesmo enquadramento nos oito. Isto produz
 * exatamente isso a partir do personagem que está desenhado aqui — ou seja, do
 * mesmo código que roda na placa. Um personagem, duas superfícies.
 *
 * ENQUADRAMENTO
 * -------------
 * Recorte FIXO e igual nos oito, e não uma caixa ajustada ao conteúdo de cada
 * estado. É o requisito que o MASCOTS.md chama de "o item que arruína este
 * trabalho mais frequentemente": se o personagem sai maior num arquivo e mais à
 * esquerda em outro, ele PULA ao trocar de estado, e o efeito lê como bug.
 *
 * Os números vêm da geometria da tela: mascote centrado em (240,240) e cara de
 * 220px, então o pé dela está em 350. Recorte de 340 com a base 34px abaixo do
 * pé dá os 10% de margem que o MASCOTS.md pede, e 340 cobre os adornos, que
 * chegam a 165px do centro. Se a proporção da cara mudar, estes números saem de
 * lugar — e o jeito de descobrir é olhar o resultado.
 */
#define SPRITE_LADO 340
#define SPRITE_X0   ((480 - SPRITE_LADO) / 2)
#define SPRITE_Y0   (384 - SPRITE_LADO)

/* Deixa transparente (ou devolve) o fundo da tela, do tileview e dos dois
 * tiles. São eles que o ui_create() pinta de preto opaco, e sem isso o sprite
 * sai com um retângulo preto em volta do personagem.
 *
 * O caminho é pela árvore do LVGL, e não por uma função nova no ui.c: o
 * firmware não deve crescer uma API para servir uma ferramenta de bancada. */
static void fundo_transparente(bool transparente)
{
    const lv_opa_t opa = transparente ? LV_OPA_TRANSP : LV_OPA_COVER;
    lv_obj_t *tela = lv_screen_active();
    lv_obj_set_style_bg_opa(tela, opa, 0);
    for (uint32_t i = 0; i < lv_obj_get_child_count(tela); i++) {
        lv_obj_t *tv = lv_obj_get_child(tela, i);
        lv_obj_set_style_bg_opa(tv, opa, 0);
        for (uint32_t k = 0; k < lv_obj_get_child_count(tv); k++)
            lv_obj_set_style_bg_opa(lv_obj_get_child(tv, k), opa, 0);
    }
}

static void exportar_sprite(const char *pasta)
{
    if (!pasta || !*pasta) {
        printf("W (sim) uso: sprite <pasta>\n");
        return;
    }
    /* Os rótulos saem DURANTE a exportação, e voltam depois — em vez de a
     * exportação recusar personagem que os mostre.
     *
     * A primeira versão recusava, guardada por uma propriedade do personagem que
     * depois deixou de existir. Recusar era a resposta errada de todo jeito: o
     * que o sprite não pode ter é texto, e desligar o texto é uma linha. */
    const wisp_cfg_t antes = *ui_ajustes();
    wisp_cfg_t sem_texto = antes;
    sem_texto.acao = sem_texto.projetos = false;
    ui_configurar(&sem_texto);

    const int bat = d.battery_pct;
    d.battery_pct = -1;      /* ui.c trata -1 como "sem medida": rotulo vazio */
    fundo_transparente(true);

    int ok = 0;
    for (int e = 0; e < WISP_COUNT; e++) {
        for (int k = 0; k < WISP_MAX_SESSIONS; k++)
            d.sessions[k].state = (wisp_state_t) e;
        ui_update(&d);
        for (int k = 0; k < ASSENTAR_MAX; k++) { relogio_avancar(); lv_timer_handler(); }

        char caminho[600];
        snprintf(caminho, sizeof(caminho), "%s/%s.tiff", pasta, ui_state_name(e));
        if (captura_tiff(caminho, SPRITE_X0, SPRITE_Y0, SPRITE_LADO, SPRITE_LADO)) ok++;
    }

    fundo_transparente(false);
    d.battery_pct = bat;
    ui_configurar(&antes);
    ui_update(&d);
    printf("I (sim) %d/%d sprites de \"%s\" em %s\n",
           ok, WISP_COUNT, mascote_ativo()->nome, pasta);
}

void cena_ajuda(void)
{
    printf("comandos: n <1-4> | s <estado> [i] | todos <estado> | rest | wake\n"
           "          tile <0|1> | bat <pct|-1> | lim | nolim\n"
           "          shot <arquivo.bmp> | sprite <pasta> | quit | ?\n"
           "          char <terminal|bytelo> | heap\n"
           "ajustes : acao <0|1> | proj <0|1> | idioma <en|pt>"
           " | tam <small|medium|large>\n"
           "personagem: escolhido no boot — WISP_MASCOT=bytelo ./sim/build/wisp-sim\n");
    /* Os nomes saem da MESMA tabela que o parser usa. Estavam escritos por
     * extenso aqui, o que fazia a ajuda poder discordar do que o comando aceita
     * — e a ajuda é onde alguém vai olhar quando o comando não funcionar. */
    printf("estados : ");
    for (int i = 0; i < WISP_COUNT; i++) printf("%s%s", i ? " " : "", ui_state_name((wisp_state_t) i));
    printf("\n");
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
        d.sessions[i].state = estado_de(arg);
    } else if (!strcmp(cmd, "todos")) {
        wisp_state_t e = estado_de(arg);
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
    } else if (!strcmp(cmd, "char")) {
        /* Só é seguro porque `destruir` passou a existir. Na implementação
         * anterior este comando foi recusado exatamente por isso: reconstruir a
         * tela deixava os objetos compartilhados do Terminal apontando para
         * memória liberada. */
        ui_personagem(arg);
        ui_update(&d);
        return;
    } else if (!strcmp(cmd, "acao") || !strcmp(cmd, "proj")
            || !strcmp(cmd, "idioma") || !strcmp(cmd, "tam")) {
        /* Quatro comandos e não um com quatro argumentos posicionais: posicional
         * se erra na terceira vez que se usa. O estado é estático porque
         * ui_configurar() recebe o conjunto inteiro, não o delta. */
        static wisp_cfg_t c = WISP_CFG_PADRAO;
        if      (!strcmp(cmd, "acao"))   c.acao = (atoi(arg) != 0);
        else if (!strcmp(cmd, "proj"))   c.projetos = (atoi(arg) != 0);
        else if (!strcmp(cmd, "idioma")) c.pt = !strcmp(arg, "pt");
        else                             c.tamanho = ui_tamanho_from_text(arg);
        ui_configurar(&c);
        ui_update(&d);
        return;
    } else if (!strcmp(cmd, "heap")) {
        lv_mem_monitor_t mon;
        lv_mem_monitor(&mon);
        printf("I (sim) heap do LVGL: %u usados de %u, frag %u%%\n",
               (unsigned) (mon.total_size - mon.free_size),
               (unsigned) mon.total_size, (unsigned) mon.frag_pct);
        return;
    } else if (!strcmp(cmd, "sprite")) {
        exportar_sprite(arg);
        return;
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
