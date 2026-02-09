#include "lv_port.h"
#include "driver/gpio.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_vendor.h"
#include "esp_log.h"
#include "esp_lvgl_port.h"
#include "xl9555.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"                                                                                                                                                                                                                  

#define TAG "lv_port"

#define LCD_WIDTH 320 
#define LCD_HEIGHT 240

#define LCD_RST_IO IO1_3

static esp_lcd_panel_io_handle_t io_handle = NULL; // io句柄
static esp_lcd_panel_handle_t panel_handle = NULL; // 屏幕句柄
static lv_disp_t *lvgl_disp = NULL; // 屏幕对象

void lv_display_hanrd_init(void)
{
    ESP_LOGI(TAG, "Initialize Intel 8080 bus");
    esp_lcd_i80_bus_handle_t i80_bus = NULL;
    esp_lcd_i80_bus_config_t bus_config = {
        .clk_src = LCD_CLK_SRC_DEFAULT,
        .dc_gpio_num = GPIO_NUM_1,
        .wr_gpio_num = GPIO_NUM_41,
        .data_gpio_nums = // 使用的是八位的接口
            {
                GPIO_NUM_40,
                GPIO_NUM_38,
                GPIO_NUM_39,
                GPIO_NUM_48,
                GPIO_NUM_45,
                GPIO_NUM_21,
                GPIO_NUM_47,
                GPIO_NUM_14,
            },
        .bus_width = 8,                               // 数据宽度
        .max_transfer_bytes = LCD_WIDTH * LCD_HEIGHT, // 一次最大传输字节数
        .dma_burst_size = 64,                         // 突发传输字节, 最大64字节  16 32 64
    };
    ESP_ERROR_CHECK(esp_lcd_new_i80_bus(&bus_config, &i80_bus)); // 创建I80总线,返回总线句柄

    esp_lcd_panel_io_i80_config_t io_config = {
        // 配置8080传输参数
        .cs_gpio_num = GPIO_NUM_2,
        .pclk_hz =
            10 * 1000 * 1000, // 像素时钟,是像素传输的频率,  320*240*30,帧率30,这个是最低要求,要加上各种命令,一般写10M
        .trans_queue_depth = 10, // 传输深度,更大更大的吞吐量
        .dc_levels =             // DC电平逻辑
        {
            .dc_idle_level = 0,
            .dc_cmd_level = 0,
            .dc_dummy_level = 0,
            .dc_data_level = 1, // 传输数据dc为高电平
        },
        .lcd_cmd_bits = 8,   // lcd命令的位数
        .lcd_param_bits = 8, // lcd参数的位数
        .flags = {
            .swap_color_bytes = 1, // 交换颜色字节 red 5 green 6 blue 5
        }};
    ESP_ERROR_CHECK(esp_lcd_new_panel_io_i80(i80_bus, &io_config, &io_handle)); // 创建io

    ESP_LOGI(TAG, "Install LCD driver of st7789");
    esp_lcd_panel_dev_config_t panel_config = {
        // 设置屏幕的显示模式
        .reset_gpio_num = GPIO_NUM_NC,
        .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB, // 16位显示 RGB
        .bits_per_pixel = 16,
    };
    ESP_ERROR_CHECK(esp_lcd_new_panel_st7789(io_handle, &panel_config, &panel_handle)); // 创建屏幕

	esp_lcd_panel_reset(panel_handle);
	esp_lcd_panel_init(panel_handle);
	esp_lcd_panel_swap_xy(panel_handle, true);
	esp_lcd_panel_mirror(panel_handle, false , true);	
	esp_lcd_panel_disp_on_off(panel_handle, true);

}

void lv_port_init(void) // 初始化
{

    xl9555_pin_write(LCD_RST_IO,0);
    vTaskDelay(pdMS_TO_TICKS(100));
    xl9555_pin_write(LCD_RST_IO,1);

    lv_display_hanrd_init(); // 硬件初始化

    /* Initialize LVGL */
    const lvgl_port_cfg_t lvgl_cfg = {
        // 任务配置
        .task_priority = 4,       /* LVGL task priority 优先级*/
        .task_stack = 8192,       /* LVGL task stack size 栈空间*/
        .task_affinity = -1,      /* LVGL task pinned to core (-1 is no affinity) */
        .task_max_sleep_ms = 500, /* Maximum sleep in LVGL task */
        .timer_period_ms = 5      /* LVGL timer tick period in ms */
    };
    lvgl_port_init(&lvgl_cfg);

    /* Add LCD screen */
    ESP_LOGD(TAG, "Add LCD screen");
    const lvgl_port_display_cfg_t disp_cfg = {.io_handle = io_handle,
                                              .panel_handle = panel_handle,
                                              .buffer_size =
                                                  LCD_WIDTH * 80, // 绘制内存缓冲区大小,会占用内部sram,一般是屏宽*80
                                              .double_buffer = 0, // 双缓冲,提升刷屏速度,占用双倍内存
                                              .hres = LCD_WIDTH,
                                              .vres = LCD_HEIGHT,
                                              .color_format = LV_COLOR_FORMAT_RGB565, // 颜色格式 RGB565
                                              .rotation =                             // 旋转
                                              {
                                                  .swap_xy = true,
                                                  .mirror_x = true,
                                                  .mirror_y = true,
                                              },
                                              .flags = {
                                                  .buff_dma = true, // 使用DMA

                                              }};
    lvgl_disp = lvgl_port_add_disp(&disp_cfg); // 添加屏幕
}