#include "lcd.h"
#include "driver/gpio.h"
#include "driver/i2c_master.h" // [修改] 引入新版 I2C 头文件，去掉 driver/i2c.h
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_vendor.h"
#include "esp_log.h"
#include "esp_lvgl_port.h"
#include "esp_lcd_touch_ft5x06.h"
#include "xl9555.h" 

#define TAG "BSP_LCD"

/* ================== 引脚定义 ================== */
// LCD 8080 接口
#define LCD_WIDTH           320
#define LCD_HEIGHT          240
#define LCD_PIN_DC          GPIO_NUM_1
#define LCD_PIN_WR          GPIO_NUM_41
#define LCD_PIN_CS          GPIO_NUM_2
// 数据引脚
#define LCD_PIN_D0          GPIO_NUM_40 
#define LCD_PIN_D1          GPIO_NUM_38
#define LCD_PIN_D2          GPIO_NUM_39
#define LCD_PIN_D3          GPIO_NUM_48
#define LCD_PIN_D4          GPIO_NUM_45
#define LCD_PIN_D5          GPIO_NUM_21
#define LCD_PIN_D6          GPIO_NUM_47
#define LCD_PIN_D7          GPIO_NUM_14

// XL9555 控制引脚
#define XL_PIN_TP_RST       IO1_0
#define XL_PIN_TP_INT       IO1_1
#define XL_PIN_LCD_BL       IO1_2
#define XL_PIN_LCD_RST      IO1_3

// 触摸屏 I2C 定义
#define TOUCH_I2C_NUM       I2C_NUM_0 
#define TOUCH_I2C_SDA       GPIO_NUM_13
#define TOUCH_I2C_SCL       GPIO_NUM_12

/* ================== 全局变量 ================== */
static esp_lcd_panel_io_handle_t io_handle = NULL;
static esp_lcd_panel_handle_t panel_handle = NULL;
static esp_lcd_touch_handle_t tp_handle = NULL;
static lv_display_t * lvgl_disp = NULL;

/* ---------------- 内部函数：初始化显示硬件 (ST7789) ---------------- */
static void lcd_panel_hw_init(void)
{
    ESP_LOGI(TAG, "Initialize Intel 8080 bus");
    esp_lcd_i80_bus_handle_t i80_bus = NULL;
    esp_lcd_i80_bus_config_t bus_config = {
        .clk_src = LCD_CLK_SRC_DEFAULT,
        .dc_gpio_num = LCD_PIN_DC,
        .wr_gpio_num = LCD_PIN_WR,
        .data_gpio_nums = {
            LCD_PIN_D0, LCD_PIN_D1, LCD_PIN_D2, LCD_PIN_D3,
            LCD_PIN_D4, LCD_PIN_D5, LCD_PIN_D6, LCD_PIN_D7,
        },
        .bus_width = 8,
        .max_transfer_bytes = LCD_WIDTH * LCD_HEIGHT * sizeof(uint16_t),
        .dma_burst_size = 64,
    };
    ESP_ERROR_CHECK(esp_lcd_new_i80_bus(&bus_config, &i80_bus));

    esp_lcd_panel_io_i80_config_t io_config = {
        .cs_gpio_num = LCD_PIN_CS,
        .pclk_hz = 10 * 1000 * 1000, 
        .trans_queue_depth = 10,
        .dc_levels = {
            .dc_idle_level = 0, .dc_cmd_level = 0, .dc_dummy_level = 0, .dc_data_level = 1,
        },
        .lcd_cmd_bits = 8,
        .lcd_param_bits = 8,
        .flags = { .swap_color_bytes = 1 }, 
    };
    ESP_ERROR_CHECK(esp_lcd_new_panel_io_i80(i80_bus, &io_config, &io_handle));

    ESP_LOGI(TAG, "Install LCD driver of st7789");
    esp_lcd_panel_dev_config_t panel_config = {
        .reset_gpio_num = -1, 
        .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB,
        .bits_per_pixel = 16,
    };
    ESP_ERROR_CHECK(esp_lcd_new_panel_st7789(io_handle, &panel_config, &panel_handle));

    ESP_ERROR_CHECK(esp_lcd_panel_reset(panel_handle));
    ESP_ERROR_CHECK(esp_lcd_panel_init(panel_handle));
    ESP_ERROR_CHECK(esp_lcd_panel_disp_on_off(panel_handle, true));
}

/* ---------------- 内部函数：初始化触摸硬件 (FT6X36) ---------------- */
static void touch_hw_init(void)
{
    // 1. 初始化 I2C 总线
    i2c_master_bus_handle_t touch_i2c_bus_handle = NULL;
    i2c_master_bus_config_t bus_config = {
        .i2c_port = TOUCH_I2C_NUM,
        .sda_io_num = TOUCH_I2C_SDA,
        .scl_io_num = TOUCH_I2C_SCL,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = 1,
    };
    
    ESP_ERROR_CHECK(i2c_new_master_bus(&bus_config, &touch_i2c_bus_handle));
    ESP_LOGI(TAG, "Touch I2C initialized (New Driver)");

    // 2. 配置 Panel IO
    esp_lcd_panel_io_handle_t tp_io_handle = NULL;

    esp_lcd_panel_io_i2c_config_t tp_io_config = {
        .dev_addr = ESP_LCD_TOUCH_IO_I2C_FT5x06_ADDRESS,
        .scl_speed_hz = 400 * 1000, // <--- [关键修正] 必须指定频率 (400kHz)，否则报错崩溃
        .on_color_trans_done = NULL,
        .user_ctx = NULL,
        .control_phase_bytes = 1,
        .dc_bit_offset = 0,
        .lcd_cmd_bits = 8,
        .flags = {
            .disable_control_phase = 1,
        }
    };
    
    ESP_ERROR_CHECK(esp_lcd_new_panel_io_i2c_v2(touch_i2c_bus_handle, &tp_io_config, &tp_io_handle));

    // 3. 配置触摸驱动
    esp_lcd_touch_config_t tp_cfg = {
        .x_max = LCD_WIDTH,
        .y_max = LCD_HEIGHT,
        .rst_gpio_num = -1, 
        .int_gpio_num = -1, 
        .levels = { .reset = 0, .interrupt = 0 },
        .flags = {
            .swap_xy = 0, 
            .mirror_x = 0,
            .mirror_y = 0,
        },
    };
    ESP_ERROR_CHECK(esp_lcd_touch_new_i2c_ft5x06(tp_io_handle, &tp_cfg, &tp_handle));
}

/* ---------------- 对外接口 ---------------- */
lv_display_t * bsp_lcd_init(void)
{
    // 1. 硬件复位 (通过 XL9555)
    ESP_LOGI(TAG, "Resetting Hardware via XL9555...");
    xl9555_pin_write(XL_PIN_LCD_RST, 0);
    xl9555_pin_write(XL_PIN_TP_RST, 0);
    vTaskDelay(pdMS_TO_TICKS(100));
    xl9555_pin_write(XL_PIN_LCD_RST, 1);
    xl9555_pin_write(XL_PIN_TP_RST, 1);
    vTaskDelay(pdMS_TO_TICKS(100));

    // 打开背光
    xl9555_pin_write(XL_PIN_LCD_BL, 1);

    // 2. 初始化 LVGL Port
    const lvgl_port_cfg_t lvgl_cfg = ESP_LVGL_PORT_INIT_CONFIG();
    if (lvgl_port_init(&lvgl_cfg) != ESP_OK) {
        ESP_LOGE(TAG, "LVGL Port Init Failed");
        return NULL;
    }

    // 3. 初始化 LCD 硬件
    lcd_panel_hw_init();

    // 4. 添加显示屏到 LVGL
    const lvgl_port_display_cfg_t disp_cfg = {
        .io_handle = io_handle,
        .panel_handle = panel_handle,
        .buffer_size = LCD_WIDTH * 50 * sizeof(uint16_t),
        .double_buffer = 0,
        .hres = LCD_WIDTH,
        .vres = LCD_HEIGHT,
        .monochrome = false,
        .rotation = {
            .swap_xy = true,   
            .mirror_x = false,
            .mirror_y = true,
        },
        .flags = { .buff_dma = true, .buff_spiram = false }
    };
    lvgl_disp = lvgl_port_add_disp(&disp_cfg);

    // 5. 初始化触摸并添加到 LVGL
    touch_hw_init();
    const lvgl_port_touch_cfg_t touch_cfg = {
        .disp = lvgl_disp,
        .handle = tp_handle,
    };
    lvgl_port_add_touch(&touch_cfg);

    ESP_LOGI(TAG, "LCD & Touch Initialized");
    return lvgl_disp;
}