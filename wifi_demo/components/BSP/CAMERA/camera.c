#include "camera.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "usb_stream.h"
#include <string.h>

static const char *TAG = "CAMERA";

// 分辨率设置 (QVGA最稳)
// 320
#define CAM_WIDTH       1280
// 240
#define CAM_HEIGHT      720
#define CAM_FPS         10
// 25
#define CAM_BUF_SIZE    (64 * 1024)
// 30
#define CAM_FRAME_MAX   (150 * 1024)

static uint8_t *xfer_buf_a = NULL;
static uint8_t *xfer_buf_b = NULL;
static uint8_t *frame_buf = NULL;

static bool is_mem_alloc = false;

static SemaphoreHandle_t s_capture_sem = NULL;
static volatile bool s_capture_req = false;
static camera_frame_t *s_captured_frame = NULL;

// 回调函数
static void camera_frame_cb(uvc_frame_t *frame, void *ptr)
{
    if (!s_capture_req) return;

    // ESP_LOGI(TAG, "Got frame: %d bytes", (int)frame->data_bytes);

    camera_frame_t *out_frame = heap_caps_malloc(sizeof(camera_frame_t), MALLOC_CAP_SPIRAM);
    if (out_frame) {
        out_frame->data = heap_caps_malloc(frame->data_bytes, MALLOC_CAP_SPIRAM);
        if (out_frame->data) {
            memcpy(out_frame->data, frame->data, frame->data_bytes);
            out_frame->len = frame->data_bytes;
            out_frame->width = frame->width;
            out_frame->height = frame->height;
            s_captured_frame = out_frame;
        } else {
            free(out_frame);
            s_captured_frame = NULL;
        }
    }
    
    s_capture_req = false;
    xSemaphoreGive(s_capture_sem);
}

// 初始化只负责申请内存
esp_err_t camera_init(void)
{
    if (is_mem_alloc) return ESP_OK;

    s_capture_sem = xSemaphoreCreateBinary();

    xfer_buf_a = (uint8_t *)heap_caps_malloc(CAM_BUF_SIZE, MALLOC_CAP_SPIRAM);
    xfer_buf_b = (uint8_t *)heap_caps_malloc(CAM_BUF_SIZE, MALLOC_CAP_SPIRAM);
    frame_buf  = (uint8_t *)heap_caps_malloc(CAM_FRAME_MAX, MALLOC_CAP_SPIRAM);

    if (!xfer_buf_a || !xfer_buf_b || !frame_buf) {
        ESP_LOGE(TAG, "Malloc failed");
        return ESP_ERR_NO_MEM;
    }
    
    is_mem_alloc = true;
    ESP_LOGI(TAG, "Camera Memory Ready");
    return ESP_OK;
}

// 配置 -> 启动 -> 拍照 -> 销毁
camera_frame_t* camera_capture(void)
{
    if (!is_mem_alloc) return NULL;

    ESP_LOGI(TAG, "Powering ON Camera...");

    // 1. 每次都重新配置 (修复 uvc not configured 报错)
    uvc_config_t config = {
        .frame_width = CAM_WIDTH,
        .frame_height = CAM_HEIGHT,
        .frame_interval = FPS2INTERVAL(CAM_FPS),
        .xfer_buffer_size = CAM_BUF_SIZE,
        .xfer_buffer_a = xfer_buf_a,
        .xfer_buffer_b = xfer_buf_b,
        .frame_buffer_size = CAM_FRAME_MAX,
        .frame_buffer = frame_buf,
        .frame_cb = &camera_frame_cb,
        .frame_cb_arg = NULL,
    };
    
    // 2. 启动流
    if (uvc_streaming_config(&config) != ESP_OK) {
        ESP_LOGE(TAG, "Config failed");
        return NULL;
    }
    if (usb_streaming_start() != ESP_OK) {
        ESP_LOGE(TAG, "Start failed");
        return NULL;
    }

    // 3. 等待连接稳定
    if (usb_streaming_connect_wait(pdMS_TO_TICKS(2000)) != ESP_OK) {
        ESP_LOGW(TAG, "Connect timeout");
        usb_streaming_stop();
        return NULL;
    }
    
    // 丢弃前几帧不稳定画面
    vTaskDelay(pdMS_TO_TICKS(500)); 

    // 4. 发起抓拍
    xSemaphoreTake(s_capture_sem, 0);
    s_captured_frame = NULL;
    s_capture_req = true;
    
    ESP_LOGI(TAG, "Say Cheese!");

    // 5. 等待照片
    if (xSemaphoreTake(s_capture_sem, pdMS_TO_TICKS(2000)) != pdTRUE) {
        ESP_LOGE(TAG, "Capture timeout");
        s_capture_req = false;
    }

    // 6. 彻底停止并销毁，释放带宽给 Wi-Fi
    ESP_LOGI(TAG, "Powering OFF Camera...");
    usb_streaming_stop();
    // 稍微延时确保 USB 任务完全退出
    vTaskDelay(pdMS_TO_TICKS(100));

    return s_captured_frame;
}

void camera_free_frame(camera_frame_t *frame)
{
    if (frame) {
        if (frame->data) free(frame->data);
        free(frame);
    }
}

// 占位函数，保持兼容
// void camera_pause_stream(void) {}
// void camera_resume_stream(void) {}