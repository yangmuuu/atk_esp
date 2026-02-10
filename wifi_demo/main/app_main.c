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
#include "esp_crt_bundle.h" 

#include "camera.h"
#include "bsp_button.h"
#include "xl9555.h"
#include "lvgl.h"
#include "esp_lvgl_port.h"
#include "audio.h"      
#include "esp_wifi.h"

static const char *TAG = "APP_MAIN";

// 阿里云Qwen配置
static const char *API_KEY = "sk-f37915195613453ea25cfce5c4d8d3fb";
static const char *API_URL = "https://dashscope.aliyuncs.com/compatible-mode/v1/chat/completions";

static volatile bool is_ai_busy = false;
static volatile uint16_t xl9555_button_level = 0xFFFF;

// 【当时的状态】100KB 大缓存
#define STREAM_BUFFER_SIZE (100 * 1024)
static char *stream_rx_buffer = NULL;
static int stream_rx_len = 0;

void app_xl9555_input_cb(uint16_t io_num, int level) {
    if (level) xl9555_button_level |= io_num;
    else xl9555_button_level &= ~io_num;
}

static int get_button_level(int gpio) {
    return (xl9555_button_level & gpio) ? 1 : 0;
}

static void print_now(const char *msg) {
    printf("[TIME] %lld ms -> %s\n", esp_timer_get_time() / 1000, msg);
}

static char *build_omni_json(const char *base64_img)
{
    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "model", "qwen3-omni-flash");
    cJSON_AddBoolToObject(root, "stream", true);

    cJSON *modalities = cJSON_CreateArray();
    cJSON_AddItemToArray(modalities, cJSON_CreateString("text"));
    cJSON_AddItemToArray(modalities, cJSON_CreateString("audio"));
    cJSON_AddItemToObject(root, "modalities", modalities);

    cJSON *audio_cfg = cJSON_CreateObject();
    cJSON_AddStringToObject(audio_cfg, "voice", "Cherry");
    cJSON_AddStringToObject(audio_cfg, "format", "wav");
    cJSON_AddItemToObject(root, "audio", audio_cfg);

    cJSON *messages = cJSON_CreateArray();
    cJSON *user_msg = cJSON_CreateObject();
    cJSON_AddStringToObject(user_msg, "role", "user");

    cJSON *content_arr = cJSON_CreateArray();

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

    cJSON *text_obj = cJSON_CreateObject();
    cJSON_AddStringToObject(text_obj, "type", "text");
    cJSON_AddStringToObject(text_obj, "text", "Describe this image in one short sentence.");
    cJSON_AddItemToArray(content_arr, text_obj);

    cJSON_AddItemToObject(user_msg, "content", content_arr);
    cJSON_AddItemToArray(messages, user_msg);
    cJSON_AddItemToObject(root, "messages", messages);

    char *json_str = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    return json_str;
}

static void process_sse_line(char *line)
{
    if (strlen(line) < 10) return;
    char *json_start = strstr(line, "data: ");
    if (!json_start) return;
    json_start += 6; 

    if (strstr(json_start, "[DONE]")) return;

    cJSON *root = cJSON_Parse(json_start);
    if (!root) return;

    cJSON *choices = cJSON_GetObjectItem(root, "choices");
    if (choices && cJSON_GetArraySize(choices) > 0) {
        cJSON *choice = cJSON_GetArrayItem(choices, 0);
        cJSON *delta = cJSON_GetObjectItem(choice, "delta");
        if (delta) {
            cJSON *audio = cJSON_GetObjectItem(delta, "audio");
            if (audio) {
                cJSON *data = cJSON_GetObjectItem(audio, "data");
                if (data && data->valuestring) {
                    size_t b64_len = strlen(data->valuestring);
                    size_t out_len = 0;
                    unsigned char *audio_buf = malloc(b64_len * 3 / 4 + 16);
                    if (audio_buf) {
                        mbedtls_base64_decode(audio_buf, b64_len, &out_len, (unsigned char *)data->valuestring, b64_len);
                        if (out_len > 0) {
                            ESP_LOGI(TAG, "Audio Decoded: %d bytes -> Writing to I2S...", (int)out_len);
                            audio_write(audio_buf, out_len);
                        }
                        free(audio_buf);
                    }
                }
            }
        }
    }
    cJSON_Delete(root);
}

esp_err_t _http_event_handler(esp_http_client_event_t *evt)
{
    if (evt->event_id == HTTP_EVENT_ON_DATA) {
        if (stream_rx_len + evt->data_len < STREAM_BUFFER_SIZE) {
            memcpy(stream_rx_buffer + stream_rx_len, evt->data, evt->data_len);
            stream_rx_len += evt->data_len;
            stream_rx_buffer[stream_rx_len] = 0;

            char *curr = stream_rx_buffer;
            char *newline = NULL;
            while ((newline = strstr(curr, "\n"))) {
                *newline = 0;
                process_sse_line(curr);
                curr = newline + 1;
            }
            int remaining = stream_rx_len - (curr - stream_rx_buffer);
            if (remaining > 0) {
                memmove(stream_rx_buffer, curr, remaining);
                stream_rx_len = remaining;
            } else {
                stream_rx_len = 0;
            }
        } else {
            stream_rx_len = 0; 
        }
    }
    return ESP_OK;
}

static void photo_action_task(void *pvParameters)
{
    is_ai_busy = true;
    print_now("Task: Opening Camera & Capturing...");
    
    camera_frame_t *frame = camera_capture();
    if (frame == NULL) {
        ESP_LOGE(TAG, "Camera capture failed");
        is_ai_busy = false;
        vTaskDelete(NULL);
        return;
    }

    if (!stream_rx_buffer) {
        stream_rx_buffer = (char *)heap_caps_malloc(STREAM_BUFFER_SIZE, MALLOC_CAP_SPIRAM);
    }
    stream_rx_len = 0;

    print_now("Task: Encoding Base64...");
    size_t b64_max_len = ((frame->len + 2) / 3) * 4 + 1;
    char *b64_buf = (char *)heap_caps_malloc(b64_max_len, MALLOC_CAP_SPIRAM);

    if (b64_buf) {
        size_t output_len = 0;
        mbedtls_base64_encode((unsigned char *)b64_buf, b64_max_len, &output_len, frame->data, frame->len);
        b64_buf[output_len] = '\0';
        
        print_now("Task: Building Omni JSON...");
        char *json_payload = build_omni_json(b64_buf);
        free(b64_buf); 

        if (json_payload) {
            print_now("Task: Sending Stream Request...");

            esp_http_client_config_t config = {
                .url = API_URL,
                .event_handler = _http_event_handler,
                .buffer_size = 4096,
                .buffer_size_tx = 1024, // 当时是 1024
                .timeout_ms = 60000,
                .method = HTTP_METHOD_POST,
                .disable_auto_redirect = true,
                .crt_bundle_attach = esp_crt_bundle_attach,
            };
            esp_http_client_handle_t client = esp_http_client_init(&config);

            esp_http_client_set_header(client, "Content-Type", "application/json");
            char auth_header[128];
            snprintf(auth_header, sizeof(auth_header), "Bearer %s", API_KEY);
            esp_http_client_set_header(client, "Authorization", auth_header);

            esp_http_client_set_post_field(client, json_payload, strlen(json_payload));
            
            esp_err_t err = esp_http_client_perform(client);
            
            if (err != ESP_OK) {
                ESP_LOGE(TAG, "HTTP Fail: %s", esp_err_to_name(err));
            } else {
                print_now("Task: Stream Finished");
            }

            esp_http_client_cleanup(client);
            free(json_payload);
        }
    }
    
    if (stream_rx_buffer) {
        free(stream_rx_buffer);
        stream_rx_buffer = NULL;
    }
    camera_free_frame(frame);
    
    print_now("Task: Finished");
    is_ai_busy = false;
    vTaskDelete(NULL);
}

static void short_press(int gpio)
{
    if (is_ai_busy) {
        ESP_LOGW(TAG, "System is busy...");
        return;
    }
    ESP_LOGI(TAG, "Button Trigger: Start Omni Task");
    
    // 【当时的状态】20KB 栈
    xTaskCreate(photo_action_task, "ai_task", 1024 * 20, NULL, 5, NULL);
}

static void long_press(int gpio) { ESP_LOGI(TAG, "Long Press"); }
static void screen_touch_cb(lv_event_t * e) { ESP_LOGI(TAG, "Clicked"); }

static void app_ui_init(void)
{
    if (lvgl_port_lock(0)) {
        lv_obj_set_style_bg_color(lv_scr_act(), lv_color_hex(0x000000), LV_PART_MAIN);
        lv_obj_t * label = lv_label_create(lv_scr_act());
        lv_label_set_text(label, "Omni Mode Ready\nPress Btn to Chat");
        lv_obj_set_style_text_color(label, lv_color_hex(0xFFFFFF), 0);
        lv_obj_center(label);
        lv_obj_add_event_cb(lv_scr_act(), screen_touch_cb, LV_EVENT_CLICKED, NULL);
        lvgl_port_unlock();
    }
}

void app_entry(void)
{
    ESP_LOGI(TAG, "Starting Omni Logic...");
    ESP_ERROR_CHECK(audio_init()); 
    esp_wifi_set_ps(WIFI_PS_NONE);
    ESP_LOGI(TAG, "Wi-Fi Power Save Disabled");

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

    app_ui_init();

    ESP_LOGI(TAG, "Entering Main Loop");
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}