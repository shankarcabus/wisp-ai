/* Adornos do personagem pixel.
 *
 * O .c é GERADO a partir dos mapas ASCII em firmware/props/ — ver
 * firmware/tools/props_to_c.py e firmware/props/README.md. */
#pragma once

#include "lvgl.h"

typedef enum {
    PROP_NENHUM = 0,
    PROP_BOLHA, PROP_LAPTOP, PROP_PERGUNTA, PROP_MAOS, PROP_FAISCAS, PROP_WIFI,
    PROP_QTD
} prop_t;

/* NULL quando não há adorno para o estado. */
const lv_image_dsc_t *prop_dsc(prop_t p);
