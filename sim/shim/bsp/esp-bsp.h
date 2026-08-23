/* Dos nove símbolos do BSP, ui.c usa dois. A assinatura é copiada de
 * firmware/components/bsp_c6_amoled_216/include/bsp/esp-bsp.h:61-62 para que a
 * chamada bsp_display_lock(-1) compile igual nos dois lados. */
#pragma once

#include <stdbool.h>
#include <stdint.h>

bool bsp_display_lock(uint32_t timeout_ms);
void bsp_display_unlock(void);
