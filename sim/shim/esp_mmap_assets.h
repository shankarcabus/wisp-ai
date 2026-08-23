/* A parte do esp_mmap_assets que ui.c usa: abrir, pegar ponteiro por índice e
 * pegar tamanho por índice. */
#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

typedef struct mmap_assets_t *mmap_assets_handle_t;

typedef struct {
    const char *partition_label;
    int         max_files;
    uint32_t    checksum;
    struct { bool mmap_enable; } flags;
} mmap_assets_config_t;

esp_err_t      mmap_assets_new(const mmap_assets_config_t *config,
                               mmap_assets_handle_t *handle);
const uint8_t *mmap_assets_get_mem(mmap_assets_handle_t handle, int index);
int            mmap_assets_get_size(mmap_assets_handle_t handle, int index);
