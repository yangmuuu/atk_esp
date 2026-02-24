#include "sr.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

// ESP-SR 核心头文件
#include "esp_wn_iface.h"
#include "esp_wn_models.h"
#include "esp_afe_sr_models.h"
#include "esp_mn_iface.h"
#include "esp_mn_models.h"
#include "model_path.h"

static const char *TAG = "SR_WAKE";

// 注意这里去掉了 const，适应新版 API
static esp_afe_sr_iface_t *afe_handle = NULL;
static esp_afe_sr_data_t *afe_data = NULL;

// 唤醒回调函数
static sr_wakeup_cb_t s_wakeup_cb = NULL;

// 监听状态标志位
static volatile bool s_is_listening = true;

// 唤醒词检测任务
static void wake_detect_task(void *arg)
{
    ESP_LOGI(TAG, "Wake detection task started");
    
    while (1) {
        if (!s_is_listening) {
            vTaskDelay(pdMS_TO_TICKS(100));
            continue;
        }

        afe_fetch_result_t* res = afe_handle->fetch(afe_data); 
        if (!res || res->ret_value == ESP_FAIL) {
            continue;
        }
        
        if (res->wakeup_state == WAKENET_DETECTED) {
            ESP_LOGI(TAG, "Wake Word Detected");
            
            // 检测到唤醒词后自动暂停，防止后续音频误触发
            s_is_listening = false;
            ESP_LOGI(TAG, "Listening suspended automatically");

            if (s_wakeup_cb) {
                s_wakeup_cb();
            }
        }
    }
}

// 初始化语音唤醒功能
esp_err_t sr_wake_init(sr_wakeup_cb_t cb)
{
    s_wakeup_cb = cb;
    s_is_listening = true;
    
    // 1. 加载唤醒模型
    srmodel_list_t *models = esp_srmodel_init("model");
    if (!models) {
        ESP_LOGE(TAG, "Failed to load models");
        return ESP_FAIL;
    }

    // 参数: 麦克风数量("M"=单麦克风), 模型列表, AFE类型(SR=语音识别), 工作模式(低成本)
    afe_config_t *afe_config = afe_config_init("M", models, AFE_TYPE_SR, AFE_MODE_LOW_COST);
    if (!afe_config) {
        ESP_LOGE(TAG, "Failed to init AFE config");
        return ESP_FAIL;
    }
    
    // 强制使用 PSRAM 分配内存
    afe_config->memory_alloc_mode = AFE_MEMORY_ALLOC_MORE_PSRAM;
    
    afe_handle = esp_afe_handle_from_config(afe_config);
    afe_data = afe_handle->create_from_config(afe_config);
    
    // 配置用完后必须释放
    afe_config_free(afe_config);

    if (!afe_data || !afe_handle) {
        ESP_LOGE(TAG, "Failed to create AFE data");
        return ESP_FAIL;
    }

    // 创建检测任务
    xTaskCreatePinnedToCore(wake_detect_task, "wake_detect", 1024 * 4, NULL, 5, NULL, 1);
    
    ESP_LOGI(TAG, "SR Init Success");
    return ESP_OK;
}

// 获取每次需要输入的音频数据大小
int sr_get_feed_chunk_size(void)
{
    if (!afe_handle || !afe_data) {
        return 0;
    }
    return afe_handle->get_fetch_chunksize(afe_data);
}

// 输入音频数据
void sr_wakeup_feed(int16_t *audio_chunk)
{
    if (afe_handle && afe_data && s_is_listening) {
        afe_handle->feed(afe_data, audio_chunk);
    }
}

// 手动暂停检测
void sr_wake_suspend(void)
{
    s_is_listening = false;
    ESP_LOGI(TAG, "Listening suspended");
}

// 手动恢复检测
void sr_wake_resume(void)
{
    s_is_listening = true;
    ESP_LOGI(TAG, "Listening resumed");
}