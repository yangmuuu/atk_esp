#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs_flash.h"
#include <stdio.h>
#include <string.h>

// 引入配网组件头文件
#include "wifi_provisioning/manager.h"
#include "wifi_provisioning/scheme_softap.h"

static const char *TAG = "WIFI_PROV";

// 事件回调函数
static void event_handler(void *arg, esp_event_base_t event_base,
                          int32_t event_id, void *event_data) {
  if (event_base == WIFI_PROV_EVENT) {
    switch (event_id) {
    case WIFI_PROV_START:
      ESP_LOGI(TAG, "配网已启动");
      break;
    case WIFI_PROV_CRED_RECV: {
      wifi_sta_config_t *wifi_cfg = (wifi_sta_config_t *)event_data;
      ESP_LOGI(TAG, "收到凭据 -> SSID: %s", (const char *)wifi_cfg->ssid);
      break;
    }
    case WIFI_PROV_CRED_FAIL: {
      wifi_prov_sta_fail_reason_t *reason =
          (wifi_prov_sta_fail_reason_t *)event_data;
      ESP_LOGE(TAG, "配网连接失败，原因: %s",
               (*reason == WIFI_PROV_STA_AUTH_ERROR) ? "密码错误" : "找不到AP");
      break;
    }
    case WIFI_PROV_CRED_SUCCESS:
      ESP_LOGI(TAG, "配网成功！");
      break;
    case WIFI_PROV_END:
      wifi_prov_mgr_deinit();
      break;
    default:
      break;
    }
  } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
    esp_wifi_connect();
  } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
    ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
    ESP_LOGI(TAG, "获取到 IP: " IPSTR, IP2STR(&event->ip_info.ip));
  } else if (event_base == WIFI_EVENT &&
             event_id == WIFI_EVENT_STA_DISCONNECTED) {
    ESP_LOGI(TAG, "WiFi 断开，正在重连...");
    esp_wifi_connect();
  }
}

void app_main(void) {
  /* 1. NVS 初始化 (必须放在最前面) */
  esp_err_t ret = nvs_flash_init();
  if (ret == ESP_ERR_NVS_NO_FREE_PAGES ||
      ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
    ESP_ERROR_CHECK(nvs_flash_erase());
    ret = nvs_flash_init();
  }
  ESP_ERROR_CHECK(ret);

  /* 2. TCP/IP 堆栈和事件循环初始化 */
  ESP_ERROR_CHECK(esp_netif_init());
  ESP_ERROR_CHECK(esp_event_loop_create_default());

  /* 3. [关键修复] WiFi 底层驱动初始化 */
  // 必须在检查 provisioning 之前把 WiFi 驱动起来，否则 is_provisioned 会报错
  esp_netif_create_default_wifi_sta();
  wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
  ESP_ERROR_CHECK(esp_wifi_init(&cfg));

  /* 4. 注册事件处理 */
  ESP_ERROR_CHECK(esp_event_handler_register(WIFI_PROV_EVENT, ESP_EVENT_ANY_ID,
                                             &event_handler, NULL));
  ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
                                             &event_handler, NULL));
  ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP,
                                             &event_handler, NULL));

  /* 5. 配置配网管理器 */
  wifi_prov_mgr_config_t config = {.scheme = wifi_prov_scheme_softap,
                                   .scheme_event_handler =
                                       WIFI_PROV_EVENT_HANDLER_NONE};
  ESP_ERROR_CHECK(wifi_prov_mgr_init(config));

  /* 6. 检查是否已经配过网 */
  bool provisioned = false;
  // 现在这里不会报错了，因为 WiFi 驱动已经 init 了
  ESP_ERROR_CHECK(wifi_prov_mgr_is_provisioned(&provisioned));

  if (!provisioned) {
    ESP_LOGI(TAG, "没有检测到配网信息，开启 SoftAP 配网...");

    char service_name[] = "PROV_S3_DEVICE";

    // 【注意区分这两个密码】
    // 1. PoP: 手机 App 连上后，为了证明你有权配置，App 会弹窗让你输入的验证码
    const char *pop = "12345678";

    // 2. WiFi 密码: 手机在系统设置里连接热点时需要的密码 (必须 >= 8位)
    const char *wifi_password = "12345678";

    // 启动配网 (注意第4个参数)
    ESP_ERROR_CHECK(wifi_prov_mgr_start_provisioning(
        WIFI_PROV_SECURITY_1, pop, service_name, wifi_password));

    ESP_LOGI(TAG, "WiFi热点已加密。SSID: %s, WiFi密码: %s, App验证码(PoP): %s",
             service_name, wifi_password, pop);
  } else {
    ESP_LOGI(TAG, "检测到已配网，直接启动连接...");

    // 释放配网占用的资源
    wifi_prov_mgr_deinit();

    // 启动 WiFi STA 模式
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_start());
  }
}