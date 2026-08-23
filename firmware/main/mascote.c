/* O registro de personagens.
 *
 * Arquivo próprio porque a lista não pertence a nenhum dos personagens dela —
 * ela ficou no mascote_terminal.c só enquanto havia um. Também não pertence ao
 * ui.c: qual personagem existe não é assunto de layout.
 */
#include <string.h>

#include "esp_log.h"
#include "mascote.h"

static const char *TAG = "mascote";

/* Índice 0 é o padrão de fábrica. A ordem importa só por isso. */
const personagem_t *const MASCOTES[] = {
    &MASCOTE_TERMINAL,
    &MASCOTE_PIXEL,
};
const int MASCOTES_QTD = (int) (sizeof(MASCOTES) / sizeof(MASCOTES[0]));

static const personagem_t *g_ativo = &MASCOTE_TERMINAL;

const personagem_t *mascote_por_nome(const char *nome)
{
    if (!nome || !*nome) return MASCOTES[0];
    for (int i = 0; i < MASCOTES_QTD; i++)
        if (strcmp(MASCOTES[i]->nome, nome) == 0) return MASCOTES[i];
    /* Nome errado na NVS não deixa a placa sem mascote: um personagem coerente
     * ganha de uma tela vazia, e o aviso no log diz o que aconteceu. */
    ESP_LOGW(TAG, "personagem \"%s\" nao existe — usando %s",
             nome, MASCOTES[0]->nome);
    return MASCOTES[0];
}

void mascote_escolher(const personagem_t *p) { if (p) g_ativo = p; }
const personagem_t *mascote_ativo(void) { return g_ativo; }
