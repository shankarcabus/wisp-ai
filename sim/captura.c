#include "captura.h"

#include <stdint.h>
#include <stdio.h>

#include "lvgl.h"
#include "relogio.h"

/* Escreve BMP de 24 bits à mão.
 *
 * POR QUE NÃO SDL_SaveBMP
 * -----------------------
 * Para a captura ser determinística ela roda em modo headless, sem janela e
 * sem vídeo do SDL inicializado. Manter o SDL no caminho da gravação
 * reintroduziria a dependência que o headless existe para cortar — e um BMP
 * são catorze bytes de cabeçalho de arquivo, quarenta de cabeçalho de imagem e
 * as linhas de baixo para cima. Não vale uma dependência. */
static void escrever_le32(FILE *f, uint32_t v)
{
    fputc(v & 0xFF, f); fputc((v >> 8) & 0xFF, f);
    fputc((v >> 16) & 0xFF, f); fputc((v >> 24) & 0xFF, f);
}

static void escrever_le16(FILE *f, uint16_t v)
{
    fputc(v & 0xFF, f); fputc((v >> 8) & 0xFF, f);
}

static bool bmp24(const char *caminho, const uint8_t *argb,
                  uint32_t w, uint32_t h, uint32_t stride)
{
    FILE *f = fopen(caminho, "wb");
    if (!f) return false;

    const uint32_t linha  = (w * 3 + 3) & ~3u;    /* linhas alinhadas em 4 */
    const uint32_t dados  = linha * h;
    const uint32_t inicio = 14 + 40;

    fputc('B', f); fputc('M', f);
    escrever_le32(f, inicio + dados);
    escrever_le32(f, 0);
    escrever_le32(f, inicio);

    escrever_le32(f, 40);
    escrever_le32(f, w);
    escrever_le32(f, h);
    escrever_le16(f, 1);
    escrever_le16(f, 24);
    for (int i = 0; i < 6; i++) escrever_le32(f, 0);

    /* BMP é de baixo para cima; o buffer do LVGL é de cima para baixo. */
    for (uint32_t y = 0; y < h; y++) {
        const uint8_t *src = argb + (size_t)(h - 1 - y) * stride;
        uint32_t escritos = 0;
        for (uint32_t x = 0; x < w; x++) {
            /* ARGB8888 do LVGL chega em memória como B,G,R,A. */
            fputc(src[x * 4 + 0], f);
            fputc(src[x * 4 + 1], f);
            fputc(src[x * 4 + 2], f);
            escritos += 3;
        }
        while (escritos++ < linha) fputc(0, f);
    }
    return fclose(f) == 0;
}

bool captura_bmp(const char *caminho)
{
    lv_draw_buf_t *buf = lv_snapshot_take(lv_screen_active(), LV_COLOR_FORMAT_ARGB8888);
    if (!buf) {
        printf("E (sim) lv_snapshot_take falhou\n");
        return false;
    }

    bool ok = bmp24(caminho, buf->data, buf->header.w, buf->header.h,
                    buf->header.stride);
    lv_draw_buf_destroy(buf);

    if (ok) printf("I (sim) %s  t=%ums\n", caminho, relogio_agora());
    else    printf("E (sim) falhou gravar %s\n", caminho);
    return ok;
}
