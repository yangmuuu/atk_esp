#include "audio.h"
#include "esp_log.h"
#include "driver/i2s_std.h"
#include "xl9555.h" 

static const char *TAG = "AUDIO";

// ================== 引脚定义 ==================
#define I2S_BCLK       GPIO_NUM_46
#define I2S_WS         GPIO_NUM_9
#define I2S_SDOUT      GPIO_NUM_8
#define SPK_EN_IO      IO0_0  

#define AUDIO_RATE     24000
#define AUDIO_BITS     I2S_DATA_BIT_WIDTH_16BIT

static i2s_chan_handle_t tx_handle = NULL;

esp_err_t audio_init(void)
{
    if (tx_handle) return ESP_OK;

    ESP_LOGI(TAG, "Initializing Native I2S (Default DMA)...");

    // 1. 创建 I2S 通道 (使用默认配置，缓存很小)
    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_AUTO, I2S_ROLE_MASTER);
    chan_cfg.auto_clear_after_cb = true; 
    ESP_ERROR_CHECK(i2s_new_channel(&chan_cfg, &tx_handle, NULL));

    // 2. 配置 I2S 标准模式
    i2s_std_config_t std_cfg = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(AUDIO_RATE),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(AUDIO_BITS, I2S_SLOT_MODE_MONO),
        .gpio_cfg = {
            .mclk = I2S_GPIO_UNUSED,
            .bclk = I2S_BCLK,
            .ws = I2S_WS,
            .dout = I2S_SDOUT,
            .din = I2S_GPIO_UNUSED,
            .invert_flags = {
                .mclk_inv = false,
                .bclk_inv = false,
                .ws_inv = false,
            },
        },
    };
    std_cfg.slot_cfg.slot_mask = I2S_STD_SLOT_RIGHT;

    ESP_ERROR_CHECK(i2s_channel_init_std_mode(tx_handle, &std_cfg));
    ESP_ERROR_CHECK(i2s_channel_enable(tx_handle));

    // 3. 打开功放
    ESP_LOGI(TAG, "Enabling PA...");
    xl9555_pin_write(SPK_EN_IO, 1);

    return ESP_OK;
}

int audio_write(const void *data, int len)
{
    if (!tx_handle) return -1;
    size_t bytes_written = 0;
    // 写入 I2S
    esp_err_t ret = i2s_channel_write(tx_handle, data, len, &bytes_written, 1000);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "I2S Write Failed: %s", esp_err_to_name(ret));
    }
    return bytes_written;
}