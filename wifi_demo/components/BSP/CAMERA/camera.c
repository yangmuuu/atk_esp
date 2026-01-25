#include "camera.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h" // 引入信号量
#include "usb_stream.h"

static const char *TAG = "CAMERA";

// 配置宏
#define CAM_WIDTH       640
#define CAM_HEIGHT      480
#define CAM_FPS         15
#define CAM_BUF_SIZE    (55 * 1024)
#define CAM_FRAME_MAX   (60 * 1024)

static uint8_t *xfer_buf_a = NULL;
static uint8_t *xfer_buf_b = NULL;
static uint8_t *frame_buf = NULL;
static bool is_init = false;

// 同步控制
static SemaphoreHandle_t s_capture_sem = NULL;
static volatile bool s_capture_req = false;     // 抓拍请求标志
static camera_frame_t *s_captured_frame = NULL; // 用于暂存抓拍结果

// ================== 回调函数 (在USB任务中运行) ==================
static void camera_frame_cb(uvc_frame_t *frame, void *ptr)
{
    // 如果没有抓拍请求，直接丢弃，不处理
    if (!s_capture_req) {
        return;
    }

    ESP_LOGI(TAG, "Callback got frame: %d bytes", (int)frame->data_bytes);

    // 1. 申请用户结构体
    camera_frame_t *out_frame = heap_caps_malloc(sizeof(camera_frame_t), MALLOC_CAP_SPIRAM);
    if (out_frame) {
        // 2. 深拷贝数据 (必须拷贝，因为frame->data回调结束后会失效)
        out_frame->data = heap_caps_malloc(frame->data_bytes, MALLOC_CAP_SPIRAM);
        if (out_frame->data) {
            memcpy(out_frame->data, frame->data, frame->data_bytes);
            out_frame->len = frame->data_bytes;
            out_frame->width = frame->width;
            out_frame->height = frame->height;
            
            // 保存结果
            s_captured_frame = out_frame;
        } else {
            free(out_frame);
            s_captured_frame = NULL; // 内存不足
        }
    } else {
        s_captured_frame = NULL;
    }

    // 3. 清除标志位并通知主任务
    s_capture_req = false;
    xSemaphoreGive(s_capture_sem);
}

// ================== 初始化 ==================
esp_err_t camera_init(void)
{
    if (is_init) return ESP_OK;

    // 创建信号量
    s_capture_sem = xSemaphoreCreateBinary();

    // 1. 申请内存
    xfer_buf_a = (uint8_t *)heap_caps_malloc(CAM_BUF_SIZE, MALLOC_CAP_SPIRAM);
    xfer_buf_b = (uint8_t *)heap_caps_malloc(CAM_BUF_SIZE, MALLOC_CAP_SPIRAM);
    frame_buf  = (uint8_t *)heap_caps_malloc(CAM_FRAME_MAX, MALLOC_CAP_SPIRAM);

    if (!xfer_buf_a || !xfer_buf_b || !frame_buf) {
        ESP_LOGE(TAG, "Malloc failed");
        return ESP_ERR_NO_MEM;
    }

    // 2. 配置 UVC (注册回调函数)
    uvc_config_t config = {
        .frame_width = CAM_WIDTH,
        .frame_height = CAM_HEIGHT,
        .frame_interval = FPS2INTERVAL(CAM_FPS),
        .xfer_buffer_size = CAM_BUF_SIZE,
        .xfer_buffer_a = xfer_buf_a,
        .xfer_buffer_b = xfer_buf_b,
        .frame_buffer_size = CAM_FRAME_MAX,
        .frame_buffer = frame_buf,
        .frame_cb = &camera_frame_cb, // 重点：注册回调
        .frame_cb_arg = NULL,
    };

    // 3. 启动
    esp_err_t ret = uvc_streaming_config(&config);
    if (ret != ESP_OK) return ret;

    ret = usb_streaming_start();
    if (ret != ESP_OK) return ret;

    // 连接等待
    usb_streaming_connect_wait(pdMS_TO_TICKS(3000));
    
    // 注意：旧版本没有 suspend/resume，我们就让它一直流传输即可，
    // 通过回调里的 s_capture_req 标志来决定是否处理数据，这样兼容性最好。
    
    is_init = true;
    ESP_LOGI(TAG, "Camera Init Done");
    return ESP_OK;
}

// ================== 抓拍接口 ==================
camera_frame_t* camera_capture(void)
{
    if (!is_init) return NULL;

    // 1. 清理之前的信号量 (防止误触发)
    xSemaphoreTake(s_capture_sem, 0);
    s_captured_frame = NULL;

    // 2. 开启抓拍标志
    s_capture_req = true;
    ESP_LOGI(TAG, "Waiting for frame...");

    // 3. 等待回调函数给出信号 (超时 1秒)
    if (xSemaphoreTake(s_capture_sem, pdMS_TO_TICKS(1000)) == pdTRUE) {
        if (s_captured_frame) {
            ESP_LOGI(TAG, "Capture success");
            return s_captured_frame;
        } else {
            ESP_LOGE(TAG, "Capture failed (Memory?)");
            return NULL;
        }
    } else {
        ESP_LOGW(TAG, "Capture timeout");
        s_capture_req = false; // 超时了要把标志复位
        return NULL;
    }
}

void camera_free_frame(camera_frame_t *frame)
{
    if (frame) {
        if (frame->data) free(frame->data);
        free(frame);
    }
}