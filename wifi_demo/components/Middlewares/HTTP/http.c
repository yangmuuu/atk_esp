#include "http.h"
#include <string.h>
#include <stdlib.h>
#include "esp_log.h"
#include "esp_http_client.h"
#include "esp_tls.h"
#include "esp_crt_bundle.h"

static const char *TAG = "HTTP_CLIENT";

// 定义一个结构体来保存接收到的响应数据
typedef struct {
    char *buffer;       // 存储数据的指针
    int len;            // 当前数据长度
} response_data_t;

// HTTP 事件处理回调：负责把分片到达的数据拼接到一起
static esp_err_t _http_event_handler(esp_http_client_event_t *evt)
{
    response_data_t *resp = (response_data_t *)evt->user_data;

    switch(evt->event_id) {
        case HTTP_EVENT_ON_DATA:
            if (!esp_http_client_is_chunked_response(evt->client)) {
                // 只有当有数据且我们有接收容器时才处理
                if (resp != NULL) {
                    // 重新分配内存：当前长度 + 新数据长度 + 1(结束符)
                    char *new_buf = realloc(resp->buffer, resp->len + evt->data_len + 1);
                    if (new_buf == NULL) {
                        ESP_LOGE(TAG, "内存分配失败");
                        return ESP_FAIL;
                    }
                    resp->buffer = new_buf;
                    // 拼接新数据
                    memcpy(resp->buffer + resp->len, evt->data, evt->data_len);
                    resp->len += evt->data_len;
                    // 加上字符串结束符，方便外部当字符串处理
                    resp->buffer[resp->len] = 0;
                }
            }
            break;
        default:
            break;
    }
    return ESP_OK;
}

char* http_send_request(const char *url, int method, const char *token, const char *data)
{
    // 初始化接收数据的结构体
    response_data_t response = { .buffer = NULL, .len = 0 };
    
    // 1. 配置 HTTP 客户端
    esp_http_client_config_t config = {
        .url = url,
        .event_handler = _http_event_handler,
        .user_data = &response,        // 传给回调函数用来存数据
        .timeout_ms = 5000,            // 超时时间 5秒
        .disable_auto_redirect = true, // 禁止自动重定向(可选)
		.crt_bundle_attach = esp_crt_bundle_attach,
    };

    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (client == NULL) {
        ESP_LOGE(TAG, "初始化 HTTP 客户端失败");
        return NULL;
    }

    // 2. 设置 HTTP 方法 (GET/POST)
    esp_http_client_set_method(client, method);

    // 3. 设置 Token (如果有)
    if (token != NULL) {
        // 拼接 "Bearer xxxx" 格式
        char auth_header[128];
        snprintf(auth_header, sizeof(auth_header), "Bearer %s", token);
        esp_http_client_set_header(client, "Authorization", auth_header);
    }

    // 4. 设置 POST 数据 (如果有)
    if (method == HTTP_METHOD_POST && data != NULL) {
        esp_http_client_set_header(client, "Content-Type", "application/json");
        esp_http_client_set_post_field(client, data, strlen(data));
    }

    // 5. 执行请求
    esp_err_t err = esp_http_client_perform(client);
    char *final_result = NULL;

    if (err == ESP_OK) {
        int status_code = esp_http_client_get_status_code(client);
        ESP_LOGI(TAG, "请求结束, 状态码: %d, 数据长度: %d", status_code, response.len);
        
        // 只有状态码为 200 或 201 才算业务成功
        if (status_code >= 200 && status_code < 300 && response.buffer != NULL) {
            // 把所有权交给 final_result
            final_result = response.buffer;
        } else {
            // 状态码不对，就算有数据也不要了
            if (response.buffer) free(response.buffer);
        }
    } else {
        ESP_LOGE(TAG, "HTTP 请求失败: %s", esp_err_to_name(err));
        if (response.buffer) free(response.buffer);
    }

    // 6. 清理客户端资源
    esp_http_client_cleanup(client);

    // 返回结果 (NULL 或者 字符串指针)
    return final_result; 
}