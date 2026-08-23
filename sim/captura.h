/* Grava a tela ativa em BMP.
 *
 * POR QUE lv_snapshot E NÃO O FRAMEBUFFER DO SDL
 * ----------------------------------------------
 * Ler de volta com SDL_RenderReadPixels depende do conteúdo do backbuffer
 * DEPOIS do present, que é indefinido em vários drivers — dá preto ou lixo,
 * de forma intermitente. O lv_snapshot redesenha a árvore de objetos num
 * buffer próprio: mesma entrada, mesma saída, sempre. Numa folha de contato
 * que existe para comparação byte a byte, determinismo é o requisito.
 *
 * BMP e não PNG porque o LVGL não tem codificador de PNG, e o `sips` que já
 * vem no macOS converte no fim. */
#pragma once

#include <stdbool.h>

bool captura_bmp(const char *caminho);
