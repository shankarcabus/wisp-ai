#pragma once
#include <stddef.h>
#include <stdint.h>
#include "ui.h"

/* PCM do som de um estado: mono, 16-bit LE, 22050 Hz.
 *
 * Nunca devolve nulo — estado sem som próprio recebe o Ping, que é o "alguém
 * está te chamando". Gerado por tools/sons_para_c.py a partir dos sons do
 * sistema do macOS, os mesmos que o app do Mac toca. */
const uint8_t *som_pcm(wisp_state_t s, size_t *len);
