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

/* Grava um recorte da tela em TIFF de 32 bits COM ALFA, para virar sprite do
 * app do Mac.
 *
 * TIFF e não PNG pelo mesmo motivo que a captura normal é BMP: o LVGL não tem
 * codificador, e um TIFF sem compressão são um cabeçalho e os bytes crus. O
 * `sips`, que já vem no macOS, converte para PNG preservando o alfa. Um PNG
 * escrito à mão exigiria CRC32 e deflate por sessenta linhas, sem ganho. */
bool captura_tiff(const char *caminho, int x0, int y0, int w, int h);
