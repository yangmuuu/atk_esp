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
#include "sr.h"
#include "esp_websocket_client.h"
#include "freertos/semphr.h"
#include "freertos/event_groups.h"

static const char *TAG = "APP_MAIN";

static EventGroupHandle_t ws_event_group;
#define WS_CONNECTED_BIT BIT0

/* =====================================================================
 * 全局配置与状态变量
 * ===================================================================== */
static const char *API_KEY = "sk-53fd3370c99d4cfdb6443935bbcd6677";
static const char *API_URL = "https://dashscope.aliyuncs.com/compatible-mode/v1/chat/completions";

static volatile bool is_ai_busy = false;
static volatile uint16_t xl9555_button_level = 0xFFFF;

// 500KB PSRAM 大缓存 (用于所有 HTTP 流式业务)
#define STREAM_BUFFER_SIZE (500 * 1024)
static char *stream_rx_buffer = NULL;
static int stream_rx_len = 0;

/* =====================================================================
 * 基础工具函数
 * ===================================================================== */
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

static void screen_touch_cb(lv_event_t * e) { ESP_LOGI(TAG, "屏幕点击回调"); }

static void app_ui_init(void)
{
    if (lvgl_port_lock(0)) {
        lv_obj_set_style_bg_color(lv_scr_act(), lv_color_hex(0x000000), LV_PART_MAIN);
        lv_obj_t * label = lv_label_create(lv_scr_act());
        lv_label_set_text(label, "ABCDEFG\n1234567");
        lv_obj_set_style_text_color(label, lv_color_hex(0xFFFFFF), 0);
        lv_obj_center(label);
        lv_obj_add_event_cb(lv_scr_act(), screen_touch_cb, LV_EVENT_CLICKED, NULL);
        lvgl_port_unlock();
    }
}

/* =====================================================================
 * 按键 2 - 阿里云 Omni 多模态
 * ===================================================================== */

 #if 1
// 模型参数配置
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
    if (!uri_str) { cJSON_Delete(root); return NULL; }
    sprintf(uri_str, "data:image/jpeg;base64,%s", base64_img);
    cJSON_AddStringToObject(img_url_obj, "url", uri_str);
    cJSON_AddItemToObject(img_obj, "image_url", img_url_obj);
    cJSON_AddItemToArray(content_arr, img_obj);
    free(uri_str);

    cJSON *text_obj = cJSON_CreateObject();
    cJSON_AddStringToObject(text_obj, "type", "text");
    cJSON_AddStringToObject(text_obj, "text", "简单一句话描述这张图片");
    cJSON_AddItemToArray(content_arr, text_obj);

    cJSON_AddItemToObject(user_msg, "content", content_arr);
    cJSON_AddItemToArray(messages, user_msg);
    cJSON_AddItemToObject(root, "messages", messages);

    char *json_str = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    return json_str;
}

// 处理阿里返回的SSE数据,并进行播放
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

// 拼接sse 数据
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
            } else { stream_rx_len = 0; }
        } else { stream_rx_len = 0; }
    }
    return ESP_OK;
}

// 拍照事件
static void photo_action_task(void *pvParameters)
{
    is_ai_busy = true;
    ESP_LOGW(TAG, "Free Internal RAM: %d bytes", (int)heap_caps_get_free_size(MALLOC_CAP_INTERNAL));
    ESP_LOGW(TAG, "Free SPIRAM: %d bytes", (int)heap_caps_get_free_size(MALLOC_CAP_SPIRAM));

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
                .buffer_size_tx = 1024,
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
            if (err != ESP_OK) { ESP_LOGE(TAG, "HTTP Fail: %s", esp_err_to_name(err)); }
            else { print_now("Task: Stream Finished"); }

            esp_http_client_cleanup(client);
            free(json_payload);
        }
    }
    
    if (stream_rx_buffer) { free(stream_rx_buffer); stream_rx_buffer = NULL; }
    camera_free_frame(frame);
    print_now("Task: Finished");
    is_ai_busy = false;
    vTaskDelete(NULL);
}

// 按键回调
static void short_press_btn2(int gpio)
{
    if (is_ai_busy) { ESP_LOGW(TAG, "System is busy..."); return; }
    ESP_LOGI(TAG, "Button Trigger: Start Omni Task");
    static StackType_t *ai_stack = NULL;
    static StaticTask_t ai_tcb;
    if (ai_stack == NULL) {
        ai_stack = (StackType_t *)heap_caps_malloc(1024 * 16, MALLOC_CAP_SPIRAM);
    }
    if (ai_stack != NULL) {
        xTaskCreateStatic(photo_action_task, "ai_task", 1024 * 16, NULL, 5, ai_stack, &ai_tcb);
    } else { ESP_LOGE(TAG, "致命错误：PSRAM 分配栈内存失败！"); }
}

#endif

/* ------------------ 【业务一结束】 ------------------ */

/* =====================================================================
 * 按键 3 - 服务器接口测试 (HTTP SSE)
 * ===================================================================== */

#if 1

// 拼接sse数据
esp_err_t _test_http_event_handler(esp_http_client_event_t *evt)
{
    switch (evt->event_id) {
        case HTTP_EVENT_ON_DATA:
            if (evt->data_len > 0) {
                // 1. 将新数据追加到缓冲区
                if (stream_rx_len + evt->data_len < STREAM_BUFFER_SIZE) {
                    memcpy(stream_rx_buffer + stream_rx_len, evt->data, evt->data_len);
                    stream_rx_len += evt->data_len;
                    stream_rx_buffer[stream_rx_len] = '\0';
                } else {
                    ESP_LOGE(TAG, "缓冲区溢出！强制清空");
                    stream_rx_len = 0;
                }

                // 2. 循环处理缓冲区内所有完整的行
                char *line_start = stream_rx_buffer;
                char *line_end;

                // SSE 标准以 \n 分隔，寻找换行符
                while ((line_end = strstr(line_start, "\n")) != NULL) {
                    *line_end = '\0'; // 替换为结束符
                    
                    // 3. 定位 data: 标签
                    char *data_ptr = strstr(line_start, "data:");
                    if (data_ptr) {
                        data_ptr += 5; // 跳过 "data:"
                        while (*data_ptr == ' ' || *data_ptr == '\r') data_ptr++; // 清理空格和回车

                        size_t b64_str_len = strlen(data_ptr);
                        // 去掉行尾残留的 \r
                        while (b64_str_len > 0 && data_ptr[b64_str_len - 1] == '\r') {
                            data_ptr[--b64_str_len] = '\0';
                        }

                        if (b64_str_len > 0) {
                            // 4. 解码当前行的音频碎片
                            size_t out_len = 0;
                            unsigned char *pcm_buf = heap_caps_malloc(b64_str_len, MALLOC_CAP_SPIRAM);
                            if (pcm_buf) {
                                int ret = mbedtls_base64_decode(pcm_buf, b64_str_len, &out_len, 
                                                              (const unsigned char *)data_ptr, b64_str_len);
                                if (ret == 0 && out_len > 0) {
                                    audio_write(pcm_buf, out_len); // 阻塞写入播放
                                } else if (ret != 0) {
                                    ESP_LOGE(TAG, "Base64 解码失败: ret=-0x%x", -ret);
                                }
                                free(pcm_buf);
                            }
                        }
                    }
                    line_start = line_end + 1; // 移动到下一行
                }

                // 5. 保留不完整的一行等待拼接
                int processed_len = line_start - stream_rx_buffer;
                int remaining = stream_rx_len - processed_len;
                if (remaining > 0) {
                    memmove(stream_rx_buffer, line_start, remaining);
                    stream_rx_len = remaining;
                } else { stream_rx_len = 0; }
            }
            break;

        case HTTP_EVENT_ON_FINISH:
            ESP_LOGI(TAG, "HTTP 请求完成");
            stream_rx_len = 0;
            break;

        case HTTP_EVENT_DISCONNECTED:
            ESP_LOGW(TAG, "HTTP 连接断开");
            stream_rx_len = 0;
            break;
        default: break;
    }
    return ESP_OK;
}

// 发送固定文本任务
static void text_to_audio_test_task(void *pvParameters)
{
    is_ai_busy = true;
    print_now("Test Task: Starting request...");

    if (!stream_rx_buffer) {
        stream_rx_buffer = (char *)heap_caps_malloc(STREAM_BUFFER_SIZE, MALLOC_CAP_SPIRAM);
    }
    stream_rx_len = 0;

    const char *test_url = "http://8.137.121.189:8080/ai/analyText-stream";
    const char *post_data = 
        "--Esp32Boundary1234\r\n"
        "Content-Disposition: form-data; name=\"question\"\r\n\r\n"
        "你是谁\r\n"
        "--Esp32Boundary1234--\r\n";

    esp_http_client_config_t config = {
        .url = test_url,
        .event_handler = _test_http_event_handler,
        .buffer_size = 4096,
        .buffer_size_tx = 1024,
        .timeout_ms = 60000,
        .method = HTTP_METHOD_POST,
        .disable_auto_redirect = true,
    };
    esp_http_client_handle_t client = esp_http_client_init(&config);
    esp_http_client_set_header(client, "Content-Type", "multipart/form-data; boundary=Esp32Boundary1234");
    esp_http_client_set_post_field(client, post_data, strlen(post_data));
    
    esp_err_t err = esp_http_client_perform(client);
    if (err != ESP_OK) { ESP_LOGE(TAG, "Test HTTP Fail: %s", esp_err_to_name(err)); }
    else { ESP_LOGI(TAG, "HTTP Status Code: %d", esp_http_client_get_status_code(client)); }

    esp_http_client_cleanup(client);
    if (stream_rx_buffer) { free(stream_rx_buffer); stream_rx_buffer = NULL; }
    
    is_ai_busy = false;
    vTaskDelete(NULL);
}

// 按键回调
static void short_press_btn3(int gpio)
{
    if (is_ai_busy) { ESP_LOGW(TAG, "System is busy..."); return; }
    ESP_LOGI(TAG, "Button 3 Trigger: Start Test Task");
    static StackType_t *test_stack = NULL;
    static StaticTask_t test_tcb;
    if (test_stack == NULL) {
        test_stack = (StackType_t *)heap_caps_malloc(1024 * 16, MALLOC_CAP_SPIRAM);
    }
    if (test_stack != NULL) {
        xTaskCreateStatic(text_to_audio_test_task, "test_task", 1024 * 16, NULL, 5, test_stack, &test_tcb);
    }
}

#endif

/* ------------------ 【业务二结束】 ------------------ */


/* =====================================================================
 * 按键 1 - 阿里云 WebSocket 实时 TTS 
 * ===================================================================== */
#if 1
static SemaphoreHandle_t tts_done_sem = NULL;
static char *ws_rx_buffer = NULL; 

static void ws_send_json(esp_websocket_client_handle_t client, cJSON *root) {
    char *json_str = cJSON_PrintUnformatted(root);
    if (json_str) {
        esp_websocket_client_send_text(client, json_str, strlen(json_str), portMAX_DELAY);
        free(json_str);
    }
    cJSON_Delete(root);
}

static void ali_tts_ws_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data) {
    esp_websocket_event_data_t *data = (esp_websocket_event_data_t *)event_data;
    if (event_id == WEBSOCKET_EVENT_DATA && (data->op_code == 0x01 || data->op_code == 0x00)) {
        if (data->payload_offset == 0) {
            if (ws_rx_buffer) free(ws_rx_buffer);
            ws_rx_buffer = heap_caps_malloc(data->payload_len + 1, MALLOC_CAP_SPIRAM);
        }
        if (ws_rx_buffer) memcpy(ws_rx_buffer + data->payload_offset, data->data_ptr, data->data_len);
        if (ws_rx_buffer && (data->payload_offset + data->data_len >= data->payload_len)) {
            ws_rx_buffer[data->payload_len] = '\0'; 
            cJSON *root = cJSON_Parse(ws_rx_buffer);
            if (root) {
                cJSON *type = cJSON_GetObjectItem(root, "type");
                if (type && type->valuestring) {
                    if (strcmp(type->valuestring, "response.audio.delta") == 0) {
                        cJSON *delta = cJSON_GetObjectItem(root, "delta");
                        if (delta && delta->valuestring) {
                            size_t b64_len = strlen(delta->valuestring);
                            size_t out_len = 0;
                            unsigned char *pcm_buf = heap_caps_malloc(b64_len * 3 / 4 + 16, MALLOC_CAP_SPIRAM);
                            if (pcm_buf) {
                                mbedtls_base64_decode(pcm_buf, b64_len, &out_len, (const unsigned char *)delta->valuestring, b64_len);
                                if (out_len > 0) { audio_write(pcm_buf, out_len); }
                                free(pcm_buf);
                            }
                        }
                    } else if (strcmp(type->valuestring, "response.done") == 0) {
                        ESP_LOGI(TAG, "阿里 TTS: 播放结束");
                        if (tts_done_sem) xSemaphoreGive(tts_done_sem);
                    }
                }
                cJSON_Delete(root);
            }
            free(ws_rx_buffer); ws_rx_buffer = NULL;
        }
    }
}

static void ali_realtime_tts_task(void *pvParameters) {
    is_ai_busy = true;
    if (tts_done_sem == NULL) tts_done_sem = xSemaphoreCreateBinary();
    esp_websocket_client_config_t ws_cfg = {
        .uri = "wss://dashscope.aliyuncs.com/api-ws/v1/realtime?model=qwen3-tts-flash-realtime",
        .buffer_size = 16384, .task_stack = 8192,
        .crt_bundle_attach = esp_crt_bundle_attach,
    };
    esp_websocket_client_handle_t client = esp_websocket_client_init(&ws_cfg);
    esp_websocket_register_events(client, WEBSOCKET_EVENT_ANY, ali_tts_ws_event_handler, (void *)client);
    char auth_header[128];
    snprintf(auth_header, sizeof(auth_header), "Bearer %s", API_KEY);
    esp_websocket_client_append_header(client, "Authorization", auth_header);
    esp_websocket_client_start(client);
    vTaskDelay(pdMS_TO_TICKS(1000)); 

    cJSON *update_req = cJSON_CreateObject();
    cJSON_AddStringToObject(update_req, "type", "session.update");
    cJSON *session = cJSON_CreateObject();
    cJSON_AddStringToObject(session, "voice", "Cherry");
    cJSON_AddStringToObject(session, "mode", "server_commit");
    cJSON_AddStringToObject(session, "response_format", "pcm");
    cJSON_AddNumberToObject(session, "sample_rate", 24000); 
    cJSON_AddItemToObject(update_req, "session", session);
    ws_send_json(client, update_req);

    cJSON *append_req = cJSON_CreateObject();
    cJSON_AddStringToObject(append_req, "type", "input_text_buffer.append");
    cJSON_AddStringToObject(append_req, "text", "你好呀！我是阿里云的实时语音合成。阿里云的大模型服务平台百炼是一站式的大模型开发及应用构建平台。不论是开发者还是业务人员，都能深入参与大模型应用的设计和构建。");
    ws_send_json(client, append_req);

    cJSON *finish_req = cJSON_CreateObject();
    cJSON_AddStringToObject(finish_req, "type", "session.finish");
    ws_send_json(client, finish_req);

    xSemaphoreTake(tts_done_sem, portMAX_DELAY);
    esp_websocket_client_stop(client);
    esp_websocket_client_destroy(client);
    is_ai_busy = false;
    vTaskDelete(NULL);
}

static void short_press_btn1(int gpio)
{
    if (is_ai_busy) { ESP_LOGW(TAG, "System is busy..."); return; }
    ESP_LOGI(TAG, "Button 1 Trigger: Start Ali TTS Task");
    static StackType_t *tts_stack = NULL;
    static StaticTask_t tts_tcb;
    if (tts_stack == NULL) { tts_stack = (StackType_t *)heap_caps_malloc(1024 * 16, MALLOC_CAP_SPIRAM); }
    if (tts_stack != NULL) { xTaskCreateStatic(ali_realtime_tts_task, "ali_tts_task", 1024 * 16, NULL, 5, tts_stack, &tts_tcb); }
}
#endif
/* ------------------ 【业务三结束】 ------------------ */


/* =====================================================================
 * 唤醒词与音频采集逻辑
 * ===================================================================== */

#define MAX_REC_SIZE (2 * 1024 * 1024) 
static int16_t *rec_buffer = NULL;
static int rec_len = 0;
static SemaphoreHandle_t voice_done_sem = NULL;

static volatile int voice_chat_stage = 0; 
static esp_websocket_client_handle_t voice_ws_client = NULL;

// 检查连接
bool is_ws_connected(esp_websocket_client_handle_t client) {
    return (client != NULL) && esp_websocket_client_is_connected(client);
}

// 分片上传二进制流 (完全匹配 C# 的 1024 字节分片与 100ms 间隔)
static void ws_asr_upload_binary(esp_websocket_client_handle_t client, int16_t *data, int len) {
    if (!client || len <= 0 || data == NULL) return;
    print_now("ASR: 开启极速推流模式...");
    char *ptr = (char *)data;
    
    // 增大分片至 4096 字节，提高单次发送效率
    const int chunk_size = 4096; 
    for (int i = 0; i < len; i += chunk_size) {
        if (!is_ws_connected(client)) break;
        int sz = (len - i < chunk_size) ? (len - i) : chunk_size;
        
        // 发送二进制数据
        esp_websocket_client_send_bin(client, ptr + i, sz, pdMS_TO_TICKS(100));
        
        // 这将使上传速度提升约 20 倍
        vTaskDelay(pdMS_TO_TICKS(5)); 
    }
    print_now("ASR: 数据已全速发送完毕");
}

// WebSocket 回调打印识别结果
static void voice_chat_ws_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data) {
    esp_websocket_event_data_t *data = (esp_websocket_event_data_t *)event_data;
    if (event_id == WEBSOCKET_EVENT_CONNECTED) {
        ESP_LOGI(TAG, "ASR WebSocket 握手成功");
        if (ws_event_group) xEventGroupSetBits(ws_event_group, WS_CONNECTED_BIT);
    } else if (event_id == WEBSOCKET_EVENT_DATA && data->op_code == 0x01) {
        // 建议在这里只解析最终结果，减少串口刷屏
        printf("\n[ASR 回传] >>> %.*s\n", data->data_len, (char *)data->data_ptr);
    }
}

// ASR 识别任务主体
static void ali_voice_chat_task(void *pvParameters) {
    print_now("ASR任务：启动");
    is_ai_busy = true;
    voice_chat_stage = 1; 
    rec_len = 0;

    if (ws_event_group == NULL) ws_event_group = xEventGroupCreate();
    xEventGroupClearBits(ws_event_group, WS_CONNECTED_BIT);
    if (voice_done_sem == NULL) voice_done_sem = xSemaphoreCreateBinary();
    xSemaphoreTake(voice_done_sem, 0); 

    rec_buffer = (int16_t *)heap_caps_malloc(MAX_REC_SIZE, MALLOC_CAP_SPIRAM);
    if (!rec_buffer) { ESP_LOGE(TAG, "内存分配失败"); is_ai_busy = false; vTaskDelete(NULL); return; }

    print_now(">>> [录制中] 请开始说话...");
    // 等待用户说完
    xSemaphoreTake(voice_done_sem, pdMS_TO_TICKS(10000)); 

    print_now("录音结束，闪电连接服务器...");
    voice_chat_stage = 2; 

    esp_websocket_client_config_t ws_cfg = {
        .uri = "wss://dashscope.aliyuncs.com/api-ws/v1/inference/",
        .buffer_size = 8192, // 增大缓冲区
        .crt_bundle_attach = esp_crt_bundle_attach,
    };

    voice_ws_client = esp_websocket_client_init(&ws_cfg);
    char auth_header[128];
    snprintf(auth_header, sizeof(auth_header), "Bearer %s", API_KEY);
    esp_websocket_client_append_header(voice_ws_client, "Authorization", auth_header);
    esp_websocket_register_events(voice_ws_client, WEBSOCKET_EVENT_ANY, voice_chat_ws_event_handler, NULL);
    esp_websocket_client_start(voice_ws_client);

    EventBits_t bits = xEventGroupWaitBits(ws_event_group, WS_CONNECTED_BIT, pdFALSE, pdTRUE, pdMS_TO_TICKS(5000));
    if (bits & WS_CONNECTED_BIT) {
        // 构建 run-task 配置帧
        cJSON *start = cJSON_CreateObject();
        cJSON *head = cJSON_CreateObject();
        cJSON_AddStringToObject(head, "action", "run-task");
        cJSON_AddStringToObject(head, "task_id", "esp32_fast_asr");
        cJSON_AddStringToObject(head, "streaming", "duplex");
        cJSON_AddItemToObject(start, "header", head);

        cJSON *payload = cJSON_CreateObject();
        cJSON_AddStringToObject(payload, "task_group", "audio");
        cJSON_AddStringToObject(payload, "task", "asr");
        cJSON_AddStringToObject(payload, "function", "recognition");
        cJSON_AddStringToObject(payload, "model", "fun-asr-realtime");
        
        cJSON *params = cJSON_CreateObject();
        cJSON_AddStringToObject(params, "format", "pcm");
        cJSON_AddNumberToObject(params, "sample_rate", 16000);
        cJSON_AddItemToObject(payload, "parameters", params);
        cJSON_AddObjectToObject(payload, "input"); 
        cJSON_AddItemToObject(start, "payload", payload);

        char *js = cJSON_PrintUnformatted(start);
        esp_websocket_client_send_text(voice_ws_client, js, strlen(js), pdMS_TO_TICKS(500));
        free(js); cJSON_Delete(start);
        
        // 减小等待开始的时间，阿里支持立即发送音频
        vTaskDelay(pdMS_TO_TICKS(50)); 

        // 全速上传音频
        ws_asr_upload_binary(voice_ws_client, rec_buffer, rec_len);
        
        // 发送结束帧
        esp_websocket_client_send_text(voice_ws_client, "{\"header\":{\"action\":\"finish-task\",\"task_id\":\"esp32_fast_asr\",\"streaming\":\"duplex\"},\"payload\":{\"input\":{}}}", 130, pdMS_TO_TICKS(500));
        
        print_now("正在获取最终识别结果...");
        vTaskDelay(pdMS_TO_TICKS(2000)); 
    }

    if (voice_ws_client) {
        esp_websocket_client_stop(voice_ws_client);
        esp_websocket_client_destroy(voice_ws_client);
        voice_ws_client = NULL;
    }
    if (rec_buffer) { free(rec_buffer); rec_buffer = NULL; }
    voice_chat_stage = 0;
    is_ai_busy = false;
    print_now("会话清理完成");
    vTaskDelete(NULL);
}

// 唤醒回调启动 ASR 任务
static void wake_word_cb(void) {
    ESP_LOGI(TAG, "唤醒识别！");
    if (!is_ai_busy) {
        xTaskCreate(ali_voice_chat_task, "voice_chat", 8192, NULL, 5, NULL);
    }
    sr_wake_resume();
}

// 麦克风采集：录音与唤醒喂数并行
static void mic_read_task(void *pvParameters) {
    int chunk_size = sr_get_feed_chunk_size();
    int buffer_len = chunk_size * sizeof(int16_t);
    int16_t *audio_chunk = (int16_t *)malloc(buffer_len);
    int silence_counter = 0;

    while (1) {
        int bytes_read = audio_read(audio_chunk, buffer_len);
        if (bytes_read == buffer_len) {
            if (voice_chat_stage == 1 && rec_buffer != NULL) {
                if (rec_len + buffer_len < MAX_REC_SIZE) {
                    memcpy((char*)rec_buffer + rec_len, audio_chunk, buffer_len);
                    rec_len += buffer_len;
                }
                int32_t energy = 0;
                for(int i=0; i<chunk_size; i++) energy += abs(audio_chunk[i]);
                energy /= chunk_size;

                if (energy < 450) silence_counter++;
                else silence_counter = 0;

                // 你说完话后大约 400ms 就会自动停止录音并开始上传
                if (silence_counter > 25 && rec_len > 8000) {
                    xSemaphoreGive(voice_done_sem); 
                    silence_counter = 0;
                }
            }
            sr_wakeup_feed(audio_chunk); 
        }
    }
}

/* =====================================================================
 * 系统入口
 * ===================================================================== */
void app_entry(void)
{
    ESP_LOGI(TAG, "Starting Omni Logic...");
    ESP_ERROR_CHECK(audio_init()); 
    ESP_ERROR_CHECK(mic_init()); 
    ESP_ERROR_CHECK(sr_wake_init(wake_word_cb)); 
    xTaskCreatePinnedToCore(mic_read_task, "mic_task", 1024 * 4, NULL, 5, NULL, 0); 
    esp_wifi_set_ps(WIFI_PS_NONE);

    // 按键 1 配置
    bsp_button_config_t cfg1 = { .gpio_num = IO0_1, .active_level = 0, .getlevel_cb = get_button_level, .short_cb = short_press_btn1 };
    button_event_set(&cfg1);

    // 按键 2 配置
    bsp_button_config_t cfg2 = { .gpio_num = IO0_2, .active_level = 0, .getlevel_cb = get_button_level, .short_cb = short_press_btn2 };
    button_event_set(&cfg2);

    // 按键 3 配置
    bsp_button_config_t cfg3 = { .gpio_num = IO0_3, .active_level = 0, .getlevel_cb = get_button_level, .short_cb = short_press_btn3 };
    button_event_set(&cfg3);

    app_ui_init();
    ESP_LOGI(TAG, "Entering Main Loop");
    while (1) { vTaskDelay(pdMS_TO_TICKS(100)); }
}