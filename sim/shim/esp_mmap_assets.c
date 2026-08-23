/* Lê os .bin que o build do firmware já produziu.
 *
 * POR QUE OS .bin INDIVIDUAIS, E NÃO O storage.bin EMPACOTADO
 * -----------------------------------------------------------
 * O storage.bin é o container do esp_mmap_assets, com um cabeçalho próprio
 * que só aquele componente sabe ler. Os arquivos individuais em
 * mmap_build/assets/storage/ são a saída direta do LVGLImage.py: 12 bytes de
 * lv_image_header_t seguidos dos planos de cor e alfa. Esse formato está
 * documentado no próprio ui.c, e ler dele significa que o simulador exercita
 * o MESMO parsing de cabeçalho que a placa.
 *
 * OS DOIS BYTES
 * -------------
 * Na placa, mmap_assets_get_mem() já pulou os 2 bytes de magic do empacotador
 * e mmap_assets_get_size() ainda os conta — ui.c compensa com `- 2`. Aqui não
 * existe empacotador, então devolvemos o arquivo inteiro em get_mem e
 * `tamanho + 2` em get_size. É o que faz a subtração de ui.c chegar no número
 * certo sem que ui.c saiba onde está rodando.
 *
 * ORDEM ALFABÉTICA
 * ----------------
 * A tabela IDX[] de ui.c mapeia estado -> índice contando com a ordem
 * alfabética dos arquivos na partição. scandir() com alphasort() reproduz
 * exatamente isso.
 *
 * SE NÃO HOUVER BUILD
 * -------------------
 * mmap_build/ é gerado por `idf.py build` e é gitignored. Sem ele, esta função
 * devolve ESP_FAIL, ui.c avisa "particao de mascotes nao abriu — segue no
 * vetor" e cai no mascote vetorial. É o mesmo caminho de degradação da placa
 * quando o mmap falha, e é justamente o que queremos poder ver. */
#include "esp_mmap_assets.h"

#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define ASSETS_MAX 16
#define PASTA "firmware/build/mmap_build/assets/storage"

struct mmap_assets_t {
    uint8_t *dados[ASSETS_MAX];
    long     tam[ASSETS_MAX];
    int      qtd;
};

static int e_asset(const struct dirent *d)
{
    const char *p = strrchr(d->d_name, '.');
    if (!p || strcmp(p, ".bin") != 0) return 0;
    /* O container empacotado tem o nome da partição e não é um asset. */
    return strcmp(d->d_name, "storage.bin") != 0;
}

esp_err_t mmap_assets_new(const mmap_assets_config_t *config,
                          mmap_assets_handle_t *handle)
{
    if (!config || !handle) return ESP_FAIL;

    struct dirent **lista = NULL;
    int n = scandir(PASTA, &lista, e_asset, alphasort);
    if (n <= 0) {
        printf("W (sim) %s: sem assets convertidos — rode `idf.py build` "
               "em firmware/ se quiser ver o mascote de imagem\n", PASTA);
        return ESP_FAIL;
    }

    struct mmap_assets_t *a = calloc(1, sizeof(*a));
    if (!a) { free(lista); return ESP_FAIL; }

    for (int i = 0; i < n && i < ASSETS_MAX; i++) {
        char caminho[512];
        snprintf(caminho, sizeof(caminho), "%s/%s", PASTA, lista[i]->d_name);
        FILE *f = fopen(caminho, "rb");
        if (!f) continue;
        fseek(f, 0, SEEK_END);
        long tam = ftell(f);
        fseek(f, 0, SEEK_SET);
        a->dados[a->qtd] = malloc((size_t) tam);
        if (a->dados[a->qtd] && fread(a->dados[a->qtd], 1, (size_t) tam, f) == (size_t) tam) {
            a->tam[a->qtd] = tam;
            a->qtd++;
        }
        fclose(f);
    }
    for (int i = 0; i < n; i++) free(lista[i]);
    free(lista);

    printf("I (sim) %d assets lidos de %s\n", a->qtd, PASTA);
    *handle = a;
    return a->qtd > 0 ? ESP_OK : ESP_FAIL;
}

const uint8_t *mmap_assets_get_mem(mmap_assets_handle_t handle, int index)
{
    if (!handle || index < 0 || index >= handle->qtd) return NULL;
    return handle->dados[index];
}

int mmap_assets_get_size(mmap_assets_handle_t handle, int index)
{
    if (!handle || index < 0 || index >= handle->qtd) return 0;
    /* + 2: ver a nota sobre os dois bytes no topo deste arquivo. */
    return (int) handle->tam[index] + 2;
}
