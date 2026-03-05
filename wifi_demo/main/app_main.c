#include "app_main.h"
#include "cJSON.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
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

static const char *TAG = "APP_MAIN";

/* =====================================================================
 * 全局配置与状态变量
 * ===================================================================== */
// 服务器基础地址，便于一键修改
#define SERVER_BASE_URL "http://8.137.121.189:8080"

static volatile bool is_ai_busy = false;
static volatile uint16_t xl9555_button_level = 0xFFFF;

// SSE 接收全局缓存 (500KB PSRAM)
#define STREAM_BUFFER_SIZE (500 * 1024)
static char *stream_rx_buffer = NULL;
static int stream_rx_len = 0;

/* =====================================================================
 * 基础工具函数
 * ===================================================================== */

// XL9555 IO 扩展芯片输入回调，用于更新按键状态
void app_xl9555_input_cb(uint16_t io_num, int level) {
    if (level) xl9555_button_level |= io_num;
    else xl9555_button_level &= ~io_num;
}

// 获取指定按键当前的电平状态
static int get_button_level(int gpio) {
    return (xl9555_button_level & gpio) ? 1 : 0;
}

// 打印带系统运行时间戳的日志，方便分析耗时
static void print_now(const char *msg) {
    printf("[TIME] %lld ms -> %s\n", esp_timer_get_time() / 1000, msg);
}

// 屏幕触摸测试回调
static void screen_touch_cb(lv_event_t * e) { 
    ESP_LOGI(TAG, "屏幕被点击"); 
}

// 初始化 LVGL 基本 UI
static void app_ui_init(void) {
    if (lvgl_port_lock(0)) {
        lv_obj_set_style_bg_color(lv_scr_act(), lv_color_hex(0x000000), LV_PART_MAIN);
        lv_obj_t * label = lv_label_create(lv_scr_act());
        lv_label_set_text(label, "Omni Device\nReady...");
        lv_obj_set_style_text_color(label, lv_color_hex(0xFFFFFF), 0);
        lv_obj_center(label);
        lv_obj_add_event_cb(lv_scr_act(), screen_touch_cb, LV_EVENT_CLICKED, NULL);
        lvgl_port_unlock();
    }
}

/* =====================================================================
 * 核心共享工具：SSE 音频流解析器
 * 负责接收 Java 后端发来的 Server-Sent Events 流，提取并解码 Base64 播放
 * ===================================================================== */
esp_err_t shared_sse_http_event_handler(esp_http_client_event_t *evt)
{
    switch (evt->event_id) {
        case HTTP_EVENT_ON_DATA:
            if (evt->data_len > 0) {
                // 1. 将新收到的数据追加到缓冲区尾部
                if (stream_rx_len + evt->data_len < STREAM_BUFFER_SIZE) {
                    memcpy(stream_rx_buffer + stream_rx_len, evt->data, evt->data_len);
                    stream_rx_len += evt->data_len;
                    stream_rx_buffer[stream_rx_len] = '\0';
                } else {
                    ESP_LOGE(TAG, "SSE 缓冲区溢出！强制清空");
                    stream_rx_len = 0;
                }

                // 2. 按行分割数据，寻找标准的 SSE 换行符
                char *line_start = stream_rx_buffer;
                char *line_end;
                while ((line_end = strstr(line_start, "\n")) != NULL) {
                    *line_end = '\0'; // 截断形成独立字符串
                    
                    // 3. 定位 "data:" 标签
                    char *data_ptr = strstr(line_start, "data:");
                    if (data_ptr) {
                        data_ptr += 5; // 跳过标签文本
                        while (*data_ptr == ' ' || *data_ptr == '\r') data_ptr++; // 清理前导空格和回车

                        size_t b64_str_len = strlen(data_ptr);
                        while (b64_str_len > 0 && data_ptr[b64_str_len - 1] == '\r') {
                            data_ptr[--b64_str_len] = '\0';
                        }

                        if (b64_str_len > 0) {
                            // 4. Base64 解码并送入硬件 I2S 播放
                            size_t out_len = 0;
                            unsigned char *pcm_buf = heap_caps_malloc(b64_str_len, MALLOC_CAP_SPIRAM);
                            if (pcm_buf) {
                                int ret = mbedtls_base64_decode(pcm_buf, b64_str_len, &out_len, 
                                                      (const unsigned char *)data_ptr, b64_str_len);
                                if (ret == 0 && out_len > 0) {
                                    audio_write(pcm_buf, out_len); // 阻塞式写入音频硬件
                                } else if (ret != 0) {
                                    ESP_LOGE(TAG, "Base64 解码失败: ret=-0x%x", -ret);
                                }
                                free(pcm_buf);
                            }
                        }
                    }
                    line_start = line_end + 1; // 移动指针到下一行
                }

                // 5. 将不完整的数据移动到缓冲区头部等待下一次拼接
                int processed_len = line_start - stream_rx_buffer;
                int remaining = stream_rx_len - processed_len;
                if (remaining > 0) {
                    memmove(stream_rx_buffer, line_start, remaining);
                    stream_rx_len = remaining;
                } else { stream_rx_len = 0; }
            }
            break;

        case HTTP_EVENT_ON_FINISH:
            ESP_LOGI(TAG, "HTTP 传输结束");
            stream_rx_len = 0;
            break;

        case HTTP_EVENT_DISCONNECTED:
            ESP_LOGW(TAG, "HTTP 连接已断开");
            stream_rx_len = 0;
            break;
        default: break;
    }
    return ESP_OK;
}


/* =====================================================================
 * 【业务一】 按键 2 触发：图像采集与服务器 AI 分析
 * 流程：摄像头获取 JPEG -> 构建 Multipart 表单 -> 分块上传 -> 接收 SSE 音频播放
 * ===================================================================== */

// 图像上传与分析业务主体
static void image_analysis_task(void *pvParameters)
{
    is_ai_busy = true;
    print_now("图像业务: 开启摄像头并拍照...");
    
    camera_frame_t *frame = camera_capture();
    if (frame == NULL) {
        ESP_LOGE(TAG, "摄像头图像获取失败");
        is_ai_busy = false;
        vTaskDelete(NULL);
        return;
    }

    print_now("图像业务: 获取成功，连接 Java 服务器...");

    if (!stream_rx_buffer) {
        stream_rx_buffer = (char *)heap_caps_malloc(STREAM_BUFFER_SIZE, MALLOC_CAP_SPIRAM);
    }
    stream_rx_len = 0;

    // 构建表单头 (对应 Java: @RequestParam("imageFile"))
    const char *boundary = "Esp32BoundaryImg123456789";
    char header[256];
    int header_len = snprintf(header, sizeof(header),
        "--%s\r\n"
        "Content-Disposition: form-data; name=\"imageFile\"; filename=\"capture.jpg\"\r\n"
        "Content-Type: image/jpeg\r\n\r\n", boundary);

    char footer[64];
    int footer_len = snprintf(footer, sizeof(footer), "\r\n--%s--\r\n", boundary);
    int total_len = header_len + frame->len + footer_len;

    esp_http_client_config_t config = {
        .url = SERVER_BASE_URL "/ai/analyze-stream",
        .method = HTTP_METHOD_POST,
        .timeout_ms = 60000, 
        .buffer_size = 4096,
        .buffer_size_tx = 4096,
    };

    esp_http_client_handle_t client = esp_http_client_init(&config);
    char content_type[128];
    snprintf(content_type, sizeof(content_type), "multipart/form-data; boundary=%s", boundary);
    esp_http_client_set_header(client, "Content-Type", content_type);

    esp_err_t err = esp_http_client_open(client, total_len);
    if (err == ESP_OK) {
        esp_http_client_write(client, header, header_len);
        
        // 极速分块传输二进制图片文件
        int remaining = frame->len;
        uint8_t *ptr = (uint8_t *)frame->data;
        while (remaining > 0) {
            int to_write = (remaining > 4096) ? 4096 : remaining;
            esp_http_client_write(client, (char *)ptr, to_write);
            ptr += to_write;
            remaining -= to_write;
            vTaskDelay(pdMS_TO_TICKS(1)); // 喂狗与系统调度
        }
        
        esp_http_client_write(client, footer, footer_len);
        esp_http_client_fetch_headers(client);
        print_now("图像业务: 图片上传完成，等待解析并接收语音流...");

        // 手动拉取 HTTP 响应并注入到共享 SSE 解析器中
        char chunk[4096];
        while (1) {
            int read_len = esp_http_client_read(client, chunk, sizeof(chunk));
            if (read_len > 0) {
                esp_http_client_event_t evt = {
                    .event_id = HTTP_EVENT_ON_DATA,
                    .data = chunk,
                    .data_len = read_len
                };
                shared_sse_http_event_handler(&evt);
            } else if (read_len <= 0) {
                break; 
            }
        }
        print_now("图像业务: 语音流播放完毕");
    } else {
        ESP_LOGE(TAG, "图像业务: 服务器连接失败");
    }

    esp_http_client_cleanup(client);
    if (stream_rx_buffer) { free(stream_rx_buffer); stream_rx_buffer = NULL; }
    camera_free_frame(frame);
    
    print_now("图像业务: 资源彻底释放");
    is_ai_busy = false;
    vTaskDelete(NULL);
}

// 按键 2 短按回调
static void short_press_btn2(int gpio)
{
    if (is_ai_busy) { ESP_LOGW(TAG, "系统正忙..."); return; }
    ESP_LOGI(TAG, "按键2触发: 图像分析业务启动");
    static StackType_t *image_stack = NULL;
    static StaticTask_t image_tcb;
    if (image_stack == NULL) { image_stack = (StackType_t *)heap_caps_malloc(1024 * 16, MALLOC_CAP_SPIRAM); }
    if (image_stack != NULL) { xTaskCreateStatic(image_analysis_task, "img_task", 1024 * 16, NULL, 5, image_stack, &image_tcb); } 
}


/* =====================================================================
 * 【业务二】 按键 3 触发：固定文本发送与服务器 AI 分析
 * 流程：发送硬编码文本 -> 接收 SSE 音频播放 (常用于联调测试)
 * ===================================================================== */

// 文本上传与分析业务主体
static void text_analysis_task(void *pvParameters)
{
    is_ai_busy = true;
    print_now("文本业务: 发送分析请求...");

    if (!stream_rx_buffer) {
        stream_rx_buffer = (char *)heap_caps_malloc(STREAM_BUFFER_SIZE, MALLOC_CAP_SPIRAM);
    }
    stream_rx_len = 0;

    const char *post_data = 
        "--Esp32Boundary1234\r\n"
        "Content-Disposition: form-data; name=\"question\"\r\n\r\n"
        "你是谁\r\n"
        "--Esp32Boundary1234--\r\n";

    esp_http_client_config_t config = {
        .url = SERVER_BASE_URL "/ai/analyText-stream",
        .event_handler = shared_sse_http_event_handler, // 自动使用共享解析器
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
    if (err != ESP_OK) { 
        ESP_LOGE(TAG, "文本业务 HTTP 失败: %s", esp_err_to_name(err)); 
    } else { 
        ESP_LOGI(TAG, "文本业务请求完成, 状态码: %d", esp_http_client_get_status_code(client)); 
    }

    esp_http_client_cleanup(client);
    if (stream_rx_buffer) { free(stream_rx_buffer); stream_rx_buffer = NULL; }
    
    print_now("文本业务: 资源彻底释放");
    is_ai_busy = false;
    vTaskDelete(NULL);
}

// 按键 3 短按回调
static void short_press_btn3(int gpio)
{
    if (is_ai_busy) { ESP_LOGW(TAG, "系统正忙..."); return; }
    ESP_LOGI(TAG, "按键3触发: 文本分析业务启动");
    static StackType_t *text_stack = NULL;
    static StaticTask_t text_tcb;
    if (text_stack == NULL) { text_stack = (StackType_t *)heap_caps_malloc(1024 * 16, MALLOC_CAP_SPIRAM); }
    if (text_stack != NULL) { xTaskCreateStatic(text_analysis_task, "text_task", 1024 * 16, NULL, 5, text_stack, &text_tcb); }
}


/* =====================================================================
 * 【业务三】 唤醒词触发：语音录制与服务器 AI 交互
 * 流程：唤醒 -> 录音至 PSRAM -> 静音检测断句 -> 分块上传 -> 接收 SSE 音频播放
 * ===================================================================== */

#define MAX_REC_SIZE (2 * 1024 * 1024) 
static int16_t *rec_buffer = NULL;
static int rec_len = 0;
static SemaphoreHandle_t voice_done_sem = NULL;
static volatile int voice_record_stage = 0; 

// 语音录音上传与分析业务主体
static void voice_analysis_task(void *pvParameters) {
    print_now("语音业务: 任务启动");
    is_ai_busy = true;
    voice_record_stage = 1; 
    rec_len = 0;

    if (voice_done_sem == NULL) voice_done_sem = xSemaphoreCreateBinary();
    xSemaphoreTake(voice_done_sem, 0); // 清空历史信号量

    rec_buffer = (int16_t *)heap_caps_malloc(MAX_REC_SIZE, MALLOC_CAP_SPIRAM);
    if (!rec_buffer) { ESP_LOGE(TAG, "内存分配失败"); is_ai_busy = false; vTaskDelete(NULL); return; }

    print_now(">>> [录音中] 请说话...");
    // 等待麦克风采集任务中的 VAD(静音检测) 发出结束信号
    xSemaphoreTake(voice_done_sem, pdMS_TO_TICKS(15000)); 

    print_now("语音业务: 录音结束，连接服务器...");
    voice_record_stage = 2; 

    if (!stream_rx_buffer) {
        stream_rx_buffer = (char *)heap_caps_malloc(STREAM_BUFFER_SIZE, MALLOC_CAP_SPIRAM);
    }
    stream_rx_len = 0;

    // 构建表单头 (对应 Java: @RequestParam("file"))
    const char *boundary = "Esp32BoundaryVoice123456";
    char header[256];
    int header_len = snprintf(header, sizeof(header),
        "--%s\r\n"
        "Content-Disposition: form-data; name=\"file\"; filename=\"voice.pcm\"\r\n"
        "Content-Type: application/octet-stream\r\n\r\n", boundary);

    char footer[64];
    int footer_len = snprintf(footer, sizeof(footer), "\r\n--%s--\r\n", boundary);
    int total_len = header_len + rec_len + footer_len;

    esp_http_client_config_t config = {
        .url = SERVER_BASE_URL "/ai/analyText-stream",
        .method = HTTP_METHOD_POST,
        .timeout_ms = 60000, 
        .buffer_size = 4096,
        .buffer_size_tx = 4096,
    };

    esp_http_client_handle_t client = esp_http_client_init(&config);
    char content_type[128];
    snprintf(content_type, sizeof(content_type), "multipart/form-data; boundary=%s", boundary);
    esp_http_client_set_header(client, "Content-Type", content_type);

    esp_err_t err = esp_http_client_open(client, total_len);
    if (err == ESP_OK) {
        esp_http_client_write(client, header, header_len);
        
        // 极速分块传输 PCM 录音缓存
        int remaining = rec_len;
        uint8_t *ptr = (uint8_t *)rec_buffer;
        while (remaining > 0) {
            int to_write = (remaining > 4096) ? 4096 : remaining;
            esp_http_client_write(client, (char *)ptr, to_write);
            ptr += to_write;
            remaining -= to_write;
            vTaskDelay(pdMS_TO_TICKS(1)); 
        }
        
        esp_http_client_write(client, footer, footer_len);
        esp_http_client_fetch_headers(client);
        print_now("语音业务: 录音上传完成，等待服务器推送语音流...");

        // 接收 SSE 音频流并注入共享解析器
        char chunk[1024];
        while (1) {
            int read_len = esp_http_client_read(client, chunk, sizeof(chunk));
            if (read_len > 0) {
                esp_http_client_event_t evt = {
                    .event_id = HTTP_EVENT_ON_DATA,
                    .data = chunk,
                    .data_len = read_len
                };
                shared_sse_http_event_handler(&evt);
            } else if (read_len <= 0) {
                break; 
            }
        }
        print_now("语音业务: 语音流接收完毕");
    } else {
        ESP_LOGE(TAG, "语音业务: 连接服务器失败");
    }

    esp_http_client_cleanup(client);
    if (rec_buffer) { free(rec_buffer); rec_buffer = NULL; }
    if (stream_rx_buffer) { free(stream_rx_buffer); stream_rx_buffer = NULL; }
    voice_record_stage = 0;
    is_ai_busy = false;
    print_now("语音业务: 彻底释放");
    vTaskDelete(NULL);
}

// 唤醒词模型命中后的回调函数
static void wake_word_cb(void) {
    ESP_LOGI(TAG, "唤醒词触发！准备启动对话");
    if (!is_ai_busy) {
        xTaskCreate(voice_analysis_task, "voice_task", 1024 * 16, NULL, 5, NULL);
    }
    sr_wake_resume(); // 恢复下一次唤醒监听
}

// 底层麦克风 I2S 采集与唤醒词识别引擎 (常驻任务)
static void mic_read_task(void *pvParameters) {
    int chunk_size = sr_get_feed_chunk_size();
    int buffer_len = chunk_size * sizeof(int16_t);
    int16_t *audio_chunk = (int16_t *)malloc(buffer_len);
    int silence_counter = 0;

    while (1) {
        int bytes_read = audio_read(audio_chunk, buffer_len);
        if (bytes_read == buffer_len) {
            
            // 如果目前处于语音录制状态，则开始保存数据和能量检测
            if (voice_record_stage == 1 && rec_buffer != NULL) {
                // 保存录音
                if (rec_len + buffer_len < MAX_REC_SIZE) {
                    memcpy((char*)rec_buffer + rec_len, audio_chunk, buffer_len);
                    rec_len += buffer_len;
                }
                
                // 简单的 VAD (静音检测) 逻辑
                int32_t energy = 0;
                for(int i=0; i < chunk_size; i++) energy += abs(audio_chunk[i]);
                energy /= chunk_size;

                if (energy < 450) silence_counter++;
                else silence_counter = 0;

                // 连续 25 帧低能量 (约400ms静音) 即判断说话完毕，释放信号量启动上传
                if (silence_counter > 25 && rec_len > 8000) {
                    xSemaphoreGive(voice_done_sem); 
                    silence_counter = 0;
                }
            }
            // 【核心】不间断地将麦克风数据喂给唤醒词引擎
            sr_wakeup_feed(audio_chunk); 
        }
    }
}


/* =====================================================================
 * 系统入口配置
 * ===================================================================== */
void app_entry(void)
{
    ESP_LOGI(TAG, "Starting Omni Device System...");
    
    // 初始化核心外设
    ESP_ERROR_CHECK(audio_init()); 
    ESP_ERROR_CHECK(mic_init()); 
    ESP_ERROR_CHECK(sr_wake_init(wake_word_cb)); 
    
    // 启动常驻底层麦克风任务，绑定至 Core 0
    xTaskCreatePinnedToCore(mic_read_task, "mic_task", 1024 * 4, NULL, 5, NULL, 0); 
    
    // 防止 WiFi 导致音频采集卡顿
    esp_wifi_set_ps(WIFI_PS_NONE);

    // 绑定按键 2 逻辑 (图像分析)
    bsp_button_config_t cfg2 = { .gpio_num = IO0_2, .active_level = 0, .getlevel_cb = get_button_level, .short_cb = short_press_btn2 };
    button_event_set(&cfg2);

    // 绑定按键 3 逻辑 (文本测试)
    bsp_button_config_t cfg3 = { .gpio_num = IO0_3, .active_level = 0, .getlevel_cb = get_button_level, .short_cb = short_press_btn3 };
    button_event_set(&cfg3);

    // 屏幕与 UI 初始化
    app_ui_init();
    
    ESP_LOGI(TAG, "Entering System Main Loop");
    while (1) { 
        vTaskDelay(pdMS_TO_TICKS(100)); 
    }
}