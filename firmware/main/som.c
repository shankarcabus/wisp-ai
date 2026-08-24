#include "som.h"
#include "sons.h"

#include "bsp/esp-bsp.h"
#include "driver/i2s_std.h"
#include "esp_check.h"
#include "esp_codec_dev.h"
#include "esp_codec_dev_defaults.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "som";

/* Casa com o PCM gerado por tools/sons_para_c.py. Mudar aqui sem mudar lá dá
 * som acelerado ou lento, que é o sintoma mais confuso possível: toca, e está
 * errado. */
#define TAXA        22050

#define PIN_MCLK    19
#define PIN_BCLK    20
#define PIN_DIN     21   /* do ES7210 (mics) — declarado para o modo STD, não usado */
#define PIN_WS      22
#define PIN_DOUT    23

/* Os três degraus na escala 0-100 do esp_codec_dev. O 65 é o valor com que o
 * som foi ouvido nesta placa pela primeira vez; os outros dois são um passo
 * para cada lado, e são estes três números que se mexe se a mesa pedir. */
static const int VOLUMES[WISP_VOL_QTD] = {40, 65, 85};

static esp_codec_dev_handle_t s_dev;
static i2s_chan_handle_t      s_tx;
static volatile bool          s_tocando;
static uint8_t                s_degrau = 0xFF;   /* nenhum aplicado ainda */
static wisp_state_t           s_fila;

esp_err_t som_init(i2c_master_bus_handle_t bus)
{
    if (!bus) return ESP_ERR_INVALID_ARG;

    /* Sem o rail, todo o resto toca no silêncio — e "toca no silêncio" é
     * indistinguível de driver quebrado, então é a primeira coisa. */
    ESP_RETURN_ON_ERROR(bsp_amp_power(true), TAG, "rail do amplificador");

    const i2s_chan_config_t ch = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
    ESP_RETURN_ON_ERROR(i2s_new_channel(&ch, &s_tx, NULL), TAG, "i2s canal");

    i2s_std_config_t std = {
        .clk_cfg  = I2S_STD_CLK_DEFAULT_CONFIG(TAXA),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT,
                                                        I2S_SLOT_MODE_MONO),
        .gpio_cfg = {
            .mclk = PIN_MCLK, .bclk = PIN_BCLK, .ws = PIN_WS,
            .dout = PIN_DOUT, .din = PIN_DIN,
            .invert_flags = {false, false, false},
        },
    };
    std.clk_cfg.mclk_multiple = I2S_MCLK_MULTIPLE_256;   /* casa com mclk_div */
    ESP_RETURN_ON_ERROR(i2s_channel_init_std_mode(s_tx, &std), TAG, "i2s std");
    ESP_RETURN_ON_ERROR(i2s_channel_enable(s_tx), TAG, "i2s enable");

    /* ES8311_CODEC_DEFAULT_ADDR é 0x30, que é o nosso 0x18 em oito bits — o
     * componente fala endereço deslocado. Escrever 0x18 aqui daria um codec que
     * não responde, e o sintoma seria silêncio. */
    audio_codec_i2c_cfg_t i2c_cfg = {
        .port = BSP_I2C_NUM,
        .addr = ES8311_CODEC_DEFAULT_ADDR,
        .bus_handle = bus,
    };
    const audio_codec_ctrl_if_t *ctrl = audio_codec_new_i2c_ctrl(&i2c_cfg);
    ESP_RETURN_ON_FALSE(ctrl, ESP_FAIL, TAG, "controle i2c do codec");

    audio_codec_i2s_cfg_t i2s_cfg = {
        .port = I2S_NUM_0, .rx_handle = NULL, .tx_handle = s_tx,
    };
    const audio_codec_data_if_t *data = audio_codec_new_i2s_data(&i2s_cfg);
    ESP_RETURN_ON_FALSE(data, ESP_FAIL, TAG, "dados i2s do codec");

    es8311_codec_cfg_t es = {
        .ctrl_if     = ctrl,
        .gpio_if     = audio_codec_new_gpio(),
        .codec_mode  = ESP_CODEC_DEV_WORK_MODE_DAC,  /* só saída: os mics ficam fora */
        .pa_pin      = -1,        /* NÃO HÁ pino de amplificador nesta placa */
        .master_mode = false,
        .use_mclk    = true,
        .hw_gain     = {.pa_voltage = 5.0f, .codec_dac_voltage = 3.3f},
        .mclk_div    = 256,
    };
    const audio_codec_if_t *codec = es8311_codec_new(&es);
    ESP_RETURN_ON_FALSE(codec, ESP_FAIL, TAG, "es8311_codec_new");

    esp_codec_dev_cfg_t dev = {
        .dev_type = ESP_CODEC_DEV_TYPE_OUT, .codec_if = codec, .data_if = data,
    };
    s_dev = esp_codec_dev_new(&dev);
    ESP_RETURN_ON_FALSE(s_dev, ESP_FAIL, TAG, "esp_codec_dev_new");

    esp_codec_dev_sample_info_t fmt = {
        .bits_per_sample = 16, .channel = 1, .channel_mask = 0x01,
        .sample_rate = TAXA,
    };
    ESP_RETURN_ON_FALSE(esp_codec_dev_open(s_dev, &fmt) == ESP_CODEC_DEV_OK,
                        ESP_FAIL, TAG, "esp_codec_dev_open");

    ESP_LOGI(TAG, "ES8311 pronto (%d Hz mono)", TAXA);
    return ESP_OK;
}

void som_volume(uint8_t degrau)
{
    if (!s_dev || degrau >= WISP_VOL_QTD || degrau == s_degrau) return;
    if (esp_codec_dev_set_out_vol(s_dev, VOLUMES[degrau]) == ESP_CODEC_DEV_OK)
        s_degrau = degrau;
}

static void tarefa(void *arg)
{
    (void) arg;
    size_t len = 0;
    const uint8_t *pcm = som_pcm(s_fila, &len);
    esp_codec_dev_write(s_dev, (void *) pcm, len);
    s_tocando = false;
    vTaskDelete(NULL);
}

void som_tocar(wisp_state_t s)
{
    if (!s_dev || s_tocando) return;
    s_tocando = true;
    s_fila = s;
    /* Prioridade 1, abaixo do LVGL: o som pode atrasar, a tela não. 3072 bytes
     * de pilha porque a task só chama write. */
    if (xTaskCreate(tarefa, "som", 3072, NULL, 1, NULL) != pdPASS) {
        s_tocando = false;
        ESP_LOGW(TAG, "sem RAM para a task de som");
    }
}
