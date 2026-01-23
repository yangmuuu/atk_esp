#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_event.h"
#include "cJSON.h"
#include "esp_http_client.h"
#include "wifi.h"
#include "http.h"

static const char *TAG = "MAIN";

// 阿里云百炼 API Key
static const char *API_KEY = "sk-f37915195613453ea25cfce5c4d8d3fb";

// 聊天补全接口完整地址
static const char *API_URL = "https://dashscope.aliyuncs.com/compatible-mode/v1/chat/completions";

// 构建请求体 JSON 字符串
char* build_request_json(const char *user_input)
{
    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "model", "qwen-plus");

    cJSON *messages = cJSON_CreateArray();
    cJSON_AddItemToObject(root, "messages", messages);

    cJSON *sys_msg = cJSON_CreateObject();
    cJSON_AddStringToObject(sys_msg, "role", "system");
    cJSON_AddStringToObject(sys_msg, "content", "You are a helpful assistant.");
    cJSON_AddItemToArray(messages, sys_msg);

    cJSON *user_msg = cJSON_CreateObject();
    cJSON_AddStringToObject(user_msg, "role", "user");
    cJSON_AddStringToObject(user_msg, "content", user_input);
    cJSON_AddItemToArray(messages, user_msg);

    char *json_str = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    return json_str;
}

// 解析响应 JSON 并提取回复内容
void parse_response_json(const char *json_response)
{
    cJSON *root = cJSON_Parse(json_response);
    if (root == NULL) {
        ESP_LOGE(TAG, "JSON parse failed");
        return;
    }

    cJSON *choices = cJSON_GetObjectItem(root, "choices");
    if (choices != NULL && cJSON_GetArraySize(choices) > 0) {
        cJSON *first_choice = cJSON_GetArrayItem(choices, 0);
        cJSON *content = cJSON_GetObjectItem(cJSON_GetObjectItem(first_choice, "message"), "content");

        if (cJSON_IsString(content)) {
            ESP_LOGI(TAG, "AI Response: %s", content->valuestring);
        }
    }

    cJSON_Delete(root);
}

// 执行 HTTP 请求的任务
void chat_task(void *pvParameters)
{
    ESP_LOGI(TAG, "Network Ready! Sending request to Qwen...");

    char *post_data = build_request_json("你是谁？");
    
    if (post_data) {
        char *response = http_send_request(API_URL, HTTP_METHOD_POST, API_KEY, post_data);
        
        if (response) {
            parse_response_json(response);
            free(response);
        } else {
            ESP_LOGE(TAG, "Request failed");
        }
        free(post_data);
    }

    // 任务结束，删除自身
    vTaskDelete(NULL);
}

void app_main(void)
{
    // 初始化事件循环
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    // 启动 WiFi 组件 (如果以前配过网，这里会在后台开始连接)
    wifi_init_portal();
	// 如果没连上网，程序会一直停在这里
    wifi_wait_for_ip();

    // 创建任务去请求 AI
    // 任务里有 wait_for_ip，所以这里直接创建没事
    xTaskCreate(chat_task, "chat_task", 8192, NULL, 5, NULL);
}