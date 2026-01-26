#include "audio.h"
#include "driver/i2s_std.h"
#include "driver/i2s_pdm.h"

static const char *TAG = "AUDIO";

static i2s_chan_handle_t tx_handle = NULL;
static i2s_chan_handle_t rx_handle = NULL;

void init_speaker(gpio_num_t bclk, gpio_num_t ws, gpio_num_t dout, uint32_t sample_rate)
{
    // 主模式工作模式, i2s1
    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_1, I2S_ROLE_MASTER);
    chan_cfg.auto_clear_after_cb = true;
	// 初始化频道,返回发送句柄 和 接受句柄,, 但是这里只需要发送句柄
    ESP_ERROR_CHECK(i2s_new_channel(&chan_cfg, &tx_handle, NULL));
    i2s_std_config_t i2s_tx_cfg = { // 配置参数
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(sample_rate), // 配置时钟参数
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_MONO),// 标准模式(飞利浦模式),采样率位深,单通道/双通道
        .gpio_cfg = // 配置引脚参数
            {
                .mclk = I2S_GPIO_UNUSED, // 忽略, 用外部的设备当主时钟才需要
                .bclk = bclk, // BCK引脚
                .ws = ws, // WS引脚
                .dout = dout, // DOUT引脚
                .invert_flags = // 引脚翻转
                    {
                        .mclk_inv = false, // 忽略
                        .bclk_inv = false, // BCK引脚翻转
                        .ws_inv = false, // WS引脚翻转
                    },
            },
    };
	i2s_tx_cfg.slot_cfg.slot_mask = I2S_STD_SLOT_RIGHT; // 右声道
	ESP_ERROR_CHECK(i2s_channel_init_std_mode(tx_handle, &i2s_tx_cfg)); // 初始化
    ESP_ERROR_CHECK(i2s_channel_enable(tx_handle)); // 启用通道
}

void init_pdm_microphone(gpio_num_t dat, gpio_num_t clk, uint32_t sample_rate) 
{
	// 初始化pdm麦克风
    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
    ESP_ERROR_CHECK(i2s_new_channel(&chan_cfg, NULL, &rx_handle));

    i2s_pdm_rx_config_t pdm_rx_cfg = {
        .clk_cfg = I2S_PDM_RX_CLK_DEFAULT_CONFIG(sample_rate),
        /* The default mono slot is the left slot (whose 'select pin' of the PDM microphone is pulled down) */
        .slot_cfg = I2S_PDM_RX_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_MONO),
        .gpio_cfg = {
            .clk = clk,
            .din = dat,
            .invert_flags = {
                .clk_inv = false,
            },
        },
    };
    pdm_rx_cfg.slot_cfg.slot_mask = I2S_STD_SLOT_RIGHT;
    ESP_ERROR_CHECK(i2s_channel_init_pdm_rx_mode(rx_handle, &pdm_rx_cfg));
    ESP_ERROR_CHECK(i2s_channel_enable(rx_handle));

}

int audio_write(const int16_t *data, int samples) {}

int audio_read(int16_t *dest, int samples) {}