/* Na placa, o decoder existe para registrar o formato RAW no LVGL. Os assets
 * já chegam convertidos, então no host não há nada para decodificar: o init
 * apenas sucede, e ui.c segue pelo caminho de imagem. */
#pragma once

#include "esp_err.h"

typedef struct esp_lv_decoder_t *esp_lv_decoder_handle_t;

esp_err_t esp_lv_decoder_init(esp_lv_decoder_handle_t *handle);
