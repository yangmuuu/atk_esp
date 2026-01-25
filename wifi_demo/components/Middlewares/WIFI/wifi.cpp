#include "wifi.h"
#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "esp_event.h"
#include "wifi_manager.h"
#include "ssid_manager.h"

static const char *TAG = "WIFI_MID";

static EventGroupHandle_t s_wifi_event_group;
static const int WIFI_CONNECTED_BIT = BIT0;

// IP 事件回调
static void ip_event_handler(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data)
{
    if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ESP_LOGI(TAG, "获取到IP，网络就绪");
        xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
    }
}

void wifi_init_portal(void)
{
    s_wifi_event_group = xEventGroupCreate();

    // NVS 初始化
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, ip_event_handler, NULL, NULL));

    auto& wifi_manager = WifiManager::GetInstance();
    WifiManagerConfig config;
    config.ssid_prefix = "PROV_S3_WEB";
    config.language = "zh-CN";
    wifi_manager.Initialize(config);

    // 设置回调
    wifi_manager.SetEventCallback([](WifiEvent event) {
        switch (event) {
            case WifiEvent::Connected:
                ESP_LOGI(TAG, "WiFi已连接");
                break;
            
            case WifiEvent::Disconnected:
                ESP_LOGW(TAG, "WiFi断开");
                xEventGroupClearBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
                break;

            case WifiEvent::ConfigModeEnter:
                ESP_LOGI(TAG, "进入配网: 192.168.4.1");
                break;

            // 关键修改：配网结束或退出时重启系统，确保环境纯净
            case WifiEvent::ConfigModeExit:
                ESP_LOGI(TAG, "配网结束，系统即将重启...");
                vTaskDelay(pdMS_TO_TICKS(500)); // 等待日志打印
                esp_restart();
                break;

            default:
                break;
        }
    });

    // 逻辑分支
    auto& ssid_list = SsidManager::GetInstance().GetSsidList();
    if (ssid_list.empty()) {
        // 无账号，进配网 (结束后会触发 ConfigModeExit -> 重启)
        wifi_manager.StartConfigAp();
    } else {
        // 有账号，进 Station
        wifi_manager.StartStation();
    }
}

void wifi_wait_for_ip(void)
{
    ESP_LOGI(TAG, "等待获取IP...");
    xEventGroupWaitBits(s_wifi_event_group, WIFI_CONNECTED_BIT, pdFALSE, pdTRUE, portMAX_DELAY);
}