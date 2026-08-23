#include "relogio.h"

static uint32_t g_ms = 0;

uint32_t relogio_agora(void) { return g_ms; }

void relogio_avancar(void) { g_ms += RELOGIO_PASSO_MS; }
