/* Monta um wisp_data_t sintético a partir de comandos de texto.
 *
 * Os dados são inventados de propósito, pelo mesmo motivo que mac/Shots faz
 * isso: uma captura com dados reais carrega nomes de projeto e números de uso
 * de quem gerou, e não se pode publicar. */
#pragma once

#include "ui.h"

void               cena_init(void);
void               cena_comando(const char *linha);
const wisp_data_t *cena_atual(void);
void               cena_ajuda(void);
