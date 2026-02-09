#include "app_main.h"
#include "cJSON.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "mbedtls/base64.h"
#include <string.h>
#include <stdio.h>
#include "esp_http_client.h"

#include "camera.h"
#include "http.h"
#include "button.h"
#include "xl9555.h"
#include "lvgl.h"
#include "esp_lvgl_port.h"

// 日志标签
static const char *TAG = "APP_MAIN";

// 阿里云Qwen配置
static const char *API_KEY = "sk-f37915195613453ea25cfce5c4d8d3fb";
static const char *API_URL = "https://dashscope.aliyuncs.com/compatible-mode/v1/chat/completions";

// 状态标志位：防止重复触发导致内存溢出
static volatile bool is_ai_busy = false;

// XL9555按键状态缓存
static volatile uint16_t xl9555_button_level = 0xFFFF;

// XL9555输入回调实现
void app_xl9555_input_cb(uint16_t io_num, int level)
{
    if (level)
        xl9555_button_level |= io_num;
    else
        xl9555_button_level &= ~io_num;
}

// 获取按键电平辅助函数
static int get_button_level(int gpio)
{
    return (xl9555_button_level & gpio) ? 1 : 0;
}

// 打印带时间戳的调试信息
static void print_now(const char *msg)
{
    printf("[TIME] %lld ms -> %s\n", esp_timer_get_time() / 1000, msg);
}

// 构建视觉大模型请求JSON
static char *build_vision_json(const char *base64_img)
{
    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "model", "qwen3-vl-plus");
    cJSON_AddBoolToObject(root, "enable_thinking", false);

    cJSON *messages = cJSON_CreateArray();
    cJSON *user_msg = cJSON_CreateObject();
    cJSON_AddStringToObject(user_msg, "role", "user");

    cJSON *content_arr = cJSON_CreateArray();

    // 图片部分
    cJSON *img_obj = cJSON_CreateObject();
    cJSON_AddStringToObject(img_obj, "type", "image_url");
    cJSON *img_url_obj = cJSON_CreateObject();

    int uri_len = strlen(base64_img) + 64;
    char *uri_str = heap_caps_malloc(uri_len, MALLOC_CAP_SPIRAM);
    if (!uri_str) {
        cJSON_Delete(root);
        return NULL;
    }
    sprintf(uri_str, "data:image/jpeg;base64,%s", base64_img);
    cJSON_AddStringToObject(img_url_obj, "url", uri_str);
    cJSON_AddItemToObject(img_obj, "image_url", img_url_obj);
    cJSON_AddItemToArray(content_arr, img_obj);
    free(uri_str);

    // 文本部分 (使用英文以适配默认字库)
    cJSON *text_obj = cJSON_CreateObject();
    cJSON_AddStringToObject(text_obj, "type", "text");
    cJSON_AddStringToObject(text_obj, "text", "Describe this image in short English");
    cJSON_AddItemToArray(content_arr, text_obj);

    cJSON_AddItemToObject(user_msg, "content", content_arr);
    cJSON_AddItemToArray(messages, user_msg);
    cJSON_AddItemToObject(root, "messages", messages);

    char *json_str = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    return json_str;
}

// 拍照与AI分析任务
static void photo_action_task(void *pvParameters)
{
    // 标记系统忙碌
    is_ai_busy = true;

    esp_log_level_set("USB_STREAM", ESP_LOG_ERROR);
    print_now("Task: Capturing image...");

    // 获取图像帧
    camera_frame_t *frame = camera_capture();
    if (frame == NULL) {
        ESP_LOGE(TAG, "Camera capture failed");
        is_ai_busy = false; // 任务异常退出，清除忙碌标志
        vTaskDelete(NULL);
        return;
    }

    print_now("Task: Encoding Base64...");
    size_t b64_max_len = ((frame->len + 2) / 3) * 4 + 1;
    char *b64_buf = (char *)heap_caps_malloc(b64_max_len, MALLOC_CAP_SPIRAM);

    if (b64_buf) {
        size_t output_len = 0;
        if (mbedtls_base64_encode((unsigned char *)b64_buf, b64_max_len, &output_len, frame->data, frame->len) == 0) {
            b64_buf[output_len] = '\0';
            
            // === [新增] 打印 Base64 编码完成的时间 ===
            print_now("Task: Base64 Encoded (Local Processing Done)"); 
            // ==========================================

            print_now("Task: Building JSON...");
            char *json_payload = build_vision_json(b64_buf);
            free(b64_buf); // JSON 构建完即可释放 Base64，省内存

            if (json_payload) {
                print_now("Task: Sending HTTP Request...");
                char *response = http_send_request(API_URL, HTTP_METHOD_POST, API_KEY, json_payload);
                
                if (response) {
                    print_now("Task: Response Received");
                    
                    // 解析并漂亮地打印 JSON
                    cJSON *root = cJSON_Parse(response);
                    if (root) {
                        char *formatted_json = cJSON_Print(root); 
                        if (formatted_json) {
                            printf("---------------- AI Response ----------------\n");
                            printf("%s\n", formatted_json); 
                            printf("---------------------------------------------\n");
                            free(formatted_json); 
                        }
                        cJSON_Delete(root); 
                    } else {
                        printf("Raw Response: %s\n", response);
                    }
                    free(response);
                }
                free(json_payload);
            }
        } else {
            free(b64_buf);
        }
    }
    
    camera_free_frame(frame);
    print_now("Task: Finished");
    
    // 任务结束，清除忙碌标志
    is_ai_busy = false;
    vTaskDelete(NULL);
}

// 短按触发回调
static void short_press(int gpio)
{
    // 如果正在忙，直接忽略按键，防止死机
    if (is_ai_busy) {
        ESP_LOGW(TAG, "System is busy, please wait...");
        return;
    }

    ESP_LOGI(TAG, "Button Trigger: Start AI Task");
    xTaskCreate(photo_action_task, "ai_task", 16384, NULL, 5, NULL);
}

// 长按触发回调
static void long_press(int gpio)
{
    ESP_LOGI(TAG, "Button Trigger: Long Press Action");
}

// 屏幕触摸回调
static void screen_touch_cb(lv_event_t * e)
{
    ESP_LOGI(TAG, "Touch Action: Screen Clicked");
}

// UI初始化
static void app_ui_init(void)
{
    if (lvgl_port_lock(0)) {
        // 设置背景
        lv_obj_set_style_bg_color(lv_scr_act(), lv_color_hex(0x000000), LV_PART_MAIN);
        
        // 创建标签
        lv_obj_t * label = lv_label_create(lv_scr_act());
        lv_label_set_text(label, "AI Assistant Ready\nWaiting for wake word...");
        lv_obj_set_style_text_color(label, lv_color_hex(0xFFFFFF), 0);
        lv_obj_set_style_text_font(label, &lv_font_montserrat_14, 0);
        lv_obj_center(label);

        // 注册全屏点击事件
        lv_obj_add_event_cb(lv_scr_act(), screen_touch_cb, LV_EVENT_CLICKED, NULL);
        lvgl_port_unlock();
    }
}

// 业务主入口
void app_entry(void)
{
    ESP_LOGI(TAG, "Starting Business Logic...");

    // 配置按键事件
    bsp_button_config_t cfg1 = {
        .gpio_num = IO0_1,
        .active_level = 0,
        .getlevel_cb = get_button_level,
        .long_press_time = 3000,
        .long_cb = long_press,
    };
    button_event_set(&cfg1);

    bsp_button_config_t cfg2 = {
        .gpio_num = IO0_2,
        .active_level = 0,
        .getlevel_cb = get_button_level,
        .short_cb = short_press,
    };
    button_event_set(&cfg2);

    // 启动UI
    app_ui_init();

    ESP_LOGI(TAG, "Entering Main Loop");

    // 智能助手主循环
    while (1) {
        // TODO: 在这里调用语音识别SDK的读取函数
        // int wake_state = esp_sr_read(...);
        
        // 模拟：如果有唤醒词被检测到
        // if (wake_state == WAKE_DETECTED) {
        //     ESP_LOGI(TAG, "Wake word detected! Listening for command...");
        //     start_recording();
        //     process_voice_command();
        // }

        // 暂时挂起，避免看门狗复位
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}