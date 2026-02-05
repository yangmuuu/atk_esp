#include "cJSON.h"
#include "esp_event.h"
#include "esp_heap_caps.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "mbedtls/base64.h"
#include <stdio.h>
#include <string.h>

#include "button.h"
#include "camera.h"
#include "http.h"
#include "wifi.h"
#include "xl9555.h"

/* ===================== XL9555 原本配置 ===================== */
#define XL9555_SDA GPIO_NUM_10
#define XL9555_SCL GPIO_NUM_11

static const char *TAG = "MAIN";
static const char *API_KEY = "sk-f37915195613453ea25cfce5c4d8d3fb";
static const char *API_URL = "https://dashscope.aliyuncs.com/compatible-mode/v1/chat/completions";

// 构建视觉模型请求 JSON (已关闭深度思考)
char *build_vision_json(const char *base64_img)
{
    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "model", "qwen3-vl-plus");
    // 关闭思考过程
    cJSON_AddBoolToObject(root, "enable_thinking", false);

    cJSON *messages = cJSON_CreateArray();
    cJSON *user_msg = cJSON_CreateObject();
    cJSON_AddStringToObject(user_msg, "role", "user");

    cJSON *content_arr = cJSON_CreateArray();

    // 1. 图片对象
    cJSON *img_obj = cJSON_CreateObject();
    cJSON_AddStringToObject(img_obj, "type", "image_url");
    cJSON *img_url_obj = cJSON_CreateObject();

    int uri_len = strlen(base64_img) + 64;
    char *uri_str = heap_caps_malloc(uri_len, MALLOC_CAP_SPIRAM);
    if (!uri_str)
    {
        cJSON_Delete(root);
        return NULL;
    }
    sprintf(uri_str, "data:image/jpeg;base64,%s", base64_img);

    cJSON_AddStringToObject(img_url_obj, "url", uri_str);
    cJSON_AddItemToObject(img_obj, "image_url", img_url_obj);
    cJSON_AddItemToArray(content_arr, img_obj);
    free(uri_str);

    // 2. 文本对象
    cJSON *text_obj = cJSON_CreateObject();
    cJSON_AddStringToObject(text_obj, "type", "text");
    cJSON_AddStringToObject(text_obj, "text", "用一句话描述这张图");
    cJSON_AddItemToArray(content_arr, text_obj);

    // 组装消息
    cJSON_AddItemToObject(user_msg, "content", content_arr);
    cJSON_AddItemToArray(messages, user_msg);
    cJSON_AddItemToObject(root, "messages", messages);

    char *json_str = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    return json_str;
}

// 辅助函数：打印当前毫秒时间戳
void print_now(const char *msg)
{
    // 转换为毫秒(ms)打印，方便阅读
    printf("[TIME] %lld ms -> %s\n", esp_timer_get_time() / 1000, msg);
}

void print_full_response(const char *json_response)
{
    cJSON *root = cJSON_Parse(json_response);
    if (root == NULL)
        return;
    char *formatted_json = cJSON_Print(root);
    if (formatted_json)
    {
        printf("\n%s\n", formatted_json);
        free(formatted_json);
    }
    cJSON_Delete(root);
}

void photo_action_task(void *pvParameters)
{
    esp_log_level_set("USB_STREAM", ESP_LOG_ERROR);

    print_now("触发短按");

    camera_frame_t *frame = camera_capture();
    if (frame == NULL)
    {
        vTaskDelete(NULL);
        return;
    }

    print_now("转Base64");

    size_t b64_max_len = ((frame->len + 2) / 3) * 4 + 1;
    char *b64_buf = (char *)heap_caps_malloc(b64_max_len, MALLOC_CAP_SPIRAM);

    if (b64_buf)
    {
        size_t output_len = 0;
        if (mbedtls_base64_encode((unsigned char *)b64_buf, b64_max_len, &output_len, frame->data, frame->len) == 0)
        {
            b64_buf[output_len] = '\0';

            print_now("构建JSON");

            char *json_payload = build_vision_json(b64_buf);
            free(b64_buf);

            if (json_payload)
            {
                // 发送请求
                char *response = http_send_request(API_URL, HTTP_METHOD_POST, API_KEY, json_payload);

                print_now("收到响应");

                if (response)
                {
                    print_full_response(response);
                    free(response);
                }
                free(json_payload);
            }
        }
        else
        {
            free(b64_buf);
        }
    }
    camera_free_frame(frame);
    print_now("结束任务");
    vTaskDelete(NULL);
}

/* ===================== XL9555 按键===================== */
static volatile uint16_t xl9555_button_level = 0xFFFF;

int get_button_level(int gpio) { return (xl9555_button_level & gpio) ? 1 : 0; }

void xl9555_input_callback(uint16_t io_num, int level)
{
    if (level)
        xl9555_button_level |= io_num;
    else
        xl9555_button_level &= ~io_num;
}

void long_press(int gpio) { ESP_LOGI(TAG, "Button long press"); }

void short_press(int gpio) { xTaskCreate(photo_action_task, "photo_task", 16384, NULL, 5, NULL); }

void button_init(void)
{
    button_config_t cfg1 = {
        .gpio_num = IO0_1,
        .active_level = 0,
        .getlevel_cb = get_button_level,
        .long_press_time = 3000,
        .long_cb = long_press,
    };
    button_event_set(&cfg1);

    button_config_t cfg2 = {
        .gpio_num = IO0_2,
        .active_level = 0,
        .getlevel_cb = get_button_level,
        .short_cb = short_press,
    };
    button_event_set(&cfg2);
}

void app_main(void)
{
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    // 恢复你原本的初始化顺序和参数
    xl9555_init(XL9555_SDA, XL9555_SCL, GPIO_NUM_17, xl9555_input_callback);
    xl9555_ioconfig(0xFFFF);
    button_init();

    wifi_init_portal();
    wifi_wait_for_ip();

    ESP_LOGI(TAG, "网络已连接，正在启动摄像头...");
    if (camera_init() != ESP_OK)
    {
        ESP_LOGE(TAG, "摄像头初始化失败");
        // 可以根据需要决定是否重启
    }
    else
    {
        ESP_LOGI(TAG, "摄像头初始化成功");
    }

    ESP_LOGI(TAG, "初始化完成");
}