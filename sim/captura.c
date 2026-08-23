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
/* Serve ao BMP e ao TIFF: os dois são little-endian, e havia um par
 * idêntico destas duas funções sessenta linhas abaixo. */
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

/* ————————————————————————————————————————————————
 *  TIFF de 32 bits, para os sprites do app do Mac
 * ———————————————————————————————————————————————— */
/* Uma entrada de IFD: tag, tipo, quantidade, valor. Valor de 1 SHORT mora nos
 * dois primeiros bytes do campo de quatro; o resto vai zerado. */
static void tif_entrada(FILE *f, uint16_t tag, uint16_t tipo,
                        uint32_t qtd, uint32_t valor)
{
    escrever_le16(f, tag);
    escrever_le16(f, tipo);
    escrever_le32(f, qtd);
    if (tipo == 3 && qtd == 1) { escrever_le16(f, (uint16_t) valor); escrever_le16(f, 0); }
    else                        escrever_le32(f, valor);
}

#define TIF_ENTRADAS 11
#define TIF_IFD      8
#define TIF_BPS      (TIF_IFD + 2 + TIF_ENTRADAS * 12 + 4)   /* array de 4 SHORT */
#define TIF_PIXELS   (TIF_BPS + 8)

bool captura_tiff(const char *caminho, int x0, int y0, int w, int h)
{
    lv_draw_buf_t *buf = lv_snapshot_take(lv_screen_active(), LV_COLOR_FORMAT_ARGB8888);
    if (!buf) { printf("E (sim) lv_snapshot_take falhou\n"); return false; }

    const int tw = (int) buf->header.w, th = (int) buf->header.h;
    if (x0 < 0 || y0 < 0 || x0 + w > tw || y0 + h > th) {
        printf("E (sim) recorte %d,%d %dx%d nao cabe em %dx%d\n", x0, y0, w, h, tw, th);
        lv_draw_buf_destroy(buf);
        return false;
    }

    FILE *f = fopen(caminho, "wb");
    if (!f) { lv_draw_buf_destroy(buf); return false; }

    fputc('I', f); fputc('I', f);          /* little-endian */
    escrever_le16(f, 42);
    escrever_le32(f, TIF_IFD);

    escrever_le16(f, TIF_ENTRADAS);
    /* As tags têm de sair em ordem crescente — é exigência do formato. */
    tif_entrada(f, 256, 4, 1, (uint32_t) w);            /* ImageWidth       */
    tif_entrada(f, 257, 4, 1, (uint32_t) h);            /* ImageLength      */
    tif_entrada(f, 258, 3, 4, TIF_BPS);                 /* BitsPerSample    */
    tif_entrada(f, 259, 3, 1, 1);                       /* sem compressão   */
    tif_entrada(f, 262, 3, 1, 2);                       /* RGB              */
    tif_entrada(f, 273, 4, 1, TIF_PIXELS);              /* StripOffsets     */
    tif_entrada(f, 277, 3, 1, 4);                       /* SamplesPerPixel  */
    tif_entrada(f, 278, 4, 1, (uint32_t) h);            /* RowsPerStrip     */
    tif_entrada(f, 279, 4, 1, (uint32_t) (w * h * 4));  /* StripByteCounts  */
    tif_entrada(f, 284, 3, 1, 1);                       /* chunky           */
    tif_entrada(f, 338, 3, 1, 2);                       /* alfa não-associado */
    escrever_le32(f, 0);                                        /* fim da cadeia    */

    for (int i = 0; i < 4; i++) escrever_le16(f, 8);            /* 8 bits por amostra */

    /* ARGB8888 do LVGL chega em memória como B,G,R,A; o TIFF quer R,G,B,A. */
    for (int y = 0; y < h; y++) {
        const uint8_t *src = (const uint8_t *) buf->data
                           + (size_t) (y0 + y) * buf->header.stride + (size_t) x0 * 4;
        for (int x = 0; x < w; x++) {
            fputc(src[x * 4 + 2], f);
            fputc(src[x * 4 + 1], f);
            fputc(src[x * 4 + 0], f);
            fputc(src[x * 4 + 3], f);
        }
    }
    bool ok = fclose(f) == 0;
    lv_draw_buf_destroy(buf);
    if (ok) printf("I (sim) %s  (%dx%d com alfa)\n", caminho, w, h);
    return ok;
}
