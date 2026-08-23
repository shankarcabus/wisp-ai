/* Os três macros de log que ui.c usa, em cima de printf.
 *
 * O formato imita o do IDF (letra, tag, mensagem) porque o objetivo é poder
 * comparar a saída do simulador com a saída do monitor serial da placa lado a
 * lado — inclusive a linha de FPS. */
#pragma once

#include <stdio.h>
#include "esp_err.h"

#define ESP_LOGI(tag, fmt, ...) printf("I (%s) " fmt "\n", tag, ##__VA_ARGS__)
#define ESP_LOGW(tag, fmt, ...) printf("W (%s) " fmt "\n", tag, ##__VA_ARGS__)
#define ESP_LOGE(tag, fmt, ...) printf("E (%s) " fmt "\n", tag, ##__VA_ARGS__)
