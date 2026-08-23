#include <stddef.h>          /* NULL */

#include "esp_lv_decoder.h"

esp_err_t esp_lv_decoder_init(esp_lv_decoder_handle_t *handle)
{
    if (handle) *handle = NULL;
    return ESP_OK;
}
