#include "http.h"
#include <string.h>
#include <stdlib.h>
#include "esp_log.h"
#include "esp_http_client.h"
#include "esp_tls.h"
#include "esp_crt_bundle.h"

static const char *TAG = "HTTP_CLIENT";

typedef struct {
    char *buffer;
    int len;
} response_data_t;

static esp_err_t _http_event_handler(esp_http_client_event_t *evt) {
    response_data_t *resp = (response_data_t *)evt->user_data;
    if (evt->event_id == HTTP_EVENT_ON_DATA) {
        if (!esp_http_client_is_chunked_response(evt->client) && resp) {
            char *new_buf = realloc(resp->buffer, resp->len + evt->data_len + 1);
            if (new_buf) {
                resp->buffer = new_buf;
                memcpy(resp->buffer + resp->len, evt->data, evt->data_len);
                resp->len += evt->data_len;
                resp->buffer[resp->len] = 0;
            }
        }
    }
    return ESP_OK;
}

char* http_send_request(const char *url, int method, const char *token, const char *data) {
    response_data_t response = { .buffer = NULL, .len = 0 };
    esp_http_client_config_t config = {
        .url = url,
        .event_handler = _http_event_handler,
        .user_data = &response,
        .timeout_ms = 30000,          // 30秒超时
        .buffer_size = 4096,         // 接收缓冲
        .buffer_size_tx = 4096,      // 发送缓冲：必须设置，否则大Body易失败
        .crt_bundle_attach = esp_crt_bundle_attach,
    };

    esp_http_client_handle_t client = esp_http_client_init(&config);
    esp_http_client_set_method(client, method);

    if (token) {
        char auth_header[128];
        snprintf(auth_header, sizeof(auth_header), "Bearer %s", token);
        esp_http_client_set_header(client, "Authorization", auth_header);
    }

    if (method == HTTP_METHOD_POST && data) {
        esp_http_client_set_header(client, "Content-Type", "application/json");
        esp_http_client_set_post_field(client, data, strlen(data));
    }

    ESP_LOGI(TAG, "正在建立连接并发送数据...");
    esp_err_t err = esp_http_client_perform(client);
    char *final_result = NULL;

    if (err == ESP_OK) {
        int status = esp_http_client_get_status_code(client);
        if (status >= 200 && status < 300) final_result = response.buffer;
        else if (response.buffer) free(response.buffer);
    } else {
        ESP_LOGE(TAG, "HTTP请求失败: %s", esp_err_to_name(err));
        if (response.buffer) free(response.buffer);
    }

    esp_http_client_cleanup(client);
    return final_result;
}