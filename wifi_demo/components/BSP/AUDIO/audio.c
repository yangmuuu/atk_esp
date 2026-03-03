#include "audio.h"
#include "esp_log.h"
#include "driver/i2s_std.h"
#include "driver/i2s_pdm.h"
#include "xl9555.h" 

static const char *TAG = "AUDIO";

// ================== 扬声器引脚定义 ==================
#define I2S_BCLK       GPIO_NUM_46
#define I2S_WS         GPIO_NUM_9
#define I2S_SDOUT      GPIO_NUM_8
#define SPK_EN_IO      IO0_0  
#define AUDIO_RATE     24000
#define AUDIO_BITS     I2S_DATA_BIT_WIDTH_16BIT

// ================== 麦克风引脚定义 ==================
#define I2S_MIC_CLK    GPIO_NUM_3  // 示例引脚，请修改
#define I2S_MIC_DAT    GPIO_NUM_42  // 示例引脚，请修改
#define MIC_RATE       16000       // 唤醒词和常规语音识别通常固定使用 16000Hz

static i2s_chan_handle_t tx_handle = NULL;
static i2s_chan_handle_t rx_handle = NULL;

// 初始化扬声器
esp_err_t audio_init(void)
{
    if (tx_handle) return ESP_OK;

    ESP_LOGI(TAG, "Initializing Speaker I2S...");

    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_1, I2S_ROLE_MASTER);
    chan_cfg.auto_clear_after_cb = true; 
    
    // 适量的 DMA 缓存，防止卡顿且不占满内存
    chan_cfg.dma_desc_num = 6;     // 6
    chan_cfg.dma_frame_num = 1024;  // 1024

    ESP_ERROR_CHECK(i2s_new_channel(&chan_cfg, &tx_handle, NULL));

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

    ESP_LOGI(TAG, "Enabling PA...");
    xl9555_pin_write(SPK_EN_IO, 1);

    return ESP_OK;
}

// 初始化麦克风 (PDM 格式)
esp_err_t mic_init(void)
{
    if (rx_handle) return ESP_OK;

    ESP_LOGI(TAG, "Initializing Microphone I2S...");

    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
	// chan_cfg.dma_desc_num = 3;
    // chan_cfg.dma_frame_num = 512;
    ESP_ERROR_CHECK(i2s_new_channel(&chan_cfg, NULL, &rx_handle));

    i2s_pdm_rx_config_t pdm_rx_cfg = {
        .clk_cfg = I2S_PDM_RX_CLK_DEFAULT_CONFIG(MIC_RATE),
        .slot_cfg = I2S_PDM_RX_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_MONO),
        .gpio_cfg = {
            .clk = I2S_MIC_CLK,
            .din = I2S_MIC_DAT,
            .invert_flags = {
                .clk_inv = false,
            },
        },
    };
    pdm_rx_cfg.slot_cfg.slot_mask = I2S_STD_SLOT_RIGHT;
    
    ESP_ERROR_CHECK(i2s_channel_init_pdm_rx_mode(rx_handle, &pdm_rx_cfg));
    ESP_ERROR_CHECK(i2s_channel_enable(rx_handle));

    return ESP_OK;
}

// 音频输出接口
int audio_write(const void *data, int len)
{
    if (!tx_handle) return -1;
    size_t bytes_written = 0;
    esp_err_t ret = i2s_channel_write(tx_handle, data, len, &bytes_written, 2000);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "I2S Write Failed: %s", esp_err_to_name(ret));
    }
    return bytes_written;
}

// 音频输入接口
int audio_read(void *data, int len)
{
    if (!rx_handle) return -1;
    size_t bytes_read = 0;
    esp_err_t ret = i2s_channel_read(rx_handle, data, len, &bytes_read, 2000);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "I2S Read Failed: %s", esp_err_to_name(ret));
    }
    return bytes_read;
}