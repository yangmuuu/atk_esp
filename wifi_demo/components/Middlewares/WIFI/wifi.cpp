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

// 定义事件组和连接标志位
static EventGroupHandle_t s_wifi_event_group;
static const int WIFI_CONNECTED_BIT = BIT0;

// 系统 IP 事件回调：只有真正拿到 IP 才算联网成功
static void ip_event_handler(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data)
{
    if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ESP_LOGI(TAG, "获取到 IP 地址，网络就绪");
        xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
    }
}

void wifi_init_portal(void)
{
    // 创建事件标志组
    s_wifi_event_group = xEventGroupCreate();

    // 初始化 NVS
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    // 注册 IP 获取事件监听
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT,
                                                        IP_EVENT_STA_GOT_IP,
                                                        ip_event_handler,
                                                        NULL,
                                                        NULL));

    // 初始化 WiFi Manager
    auto& wifi_manager = WifiManager::GetInstance();
    WifiManagerConfig config;
    config.ssid_prefix = "PROV_S3_WEB";
    config.language = "zh-CN";
    wifi_manager.Initialize(config);

    // 设置组件回调
    wifi_manager.SetEventCallback([](WifiEvent event) {
        switch (event) {
            case WifiEvent::Scanning:
                ESP_LOGI(TAG, "正在扫描 WiFi...");
                break;
            case WifiEvent::Connecting:
                ESP_LOGI(TAG, "正在连接 WiFi...");
                break;
            case WifiEvent::Connected:
                ESP_LOGI(TAG, "WiFi 链路已连接 (等待分配 IP...)");
                break;
            case WifiEvent::Disconnected:
                ESP_LOGW(TAG, "WiFi 断开");
                // 断开时清除标志位，防止后续任务误判
                xEventGroupClearBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
                break;
            case WifiEvent::ConfigModeEnter:
                ESP_LOGI(TAG, "进入配网模式: 请连接热点 PROV_S3_WEB，访问 192.168.4.1");
                break;
            default:
                break;
        }
    });

    // 启动逻辑
    auto& ssid_list = SsidManager::GetInstance().GetSsidList();
    if (ssid_list.empty()) {
        wifi_manager.StartConfigAp();
    } else {
        wifi_manager.StartStation();
    }
}

// 阻塞等待直到网络连接成功
void wifi_wait_for_ip(void)
{
    ESP_LOGI(TAG, "等待获取 IP 地址...");
    // 无限等待，直到 IP 事件触发设置了标志位
    xEventGroupWaitBits(s_wifi_event_group, WIFI_CONNECTED_BIT, pdFALSE, pdTRUE, portMAX_DELAY);
}