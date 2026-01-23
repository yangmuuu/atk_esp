#pragma once

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 发送 HTTP/HTTPS 请求
 * * @param url       请求地址 (例如 "http://example.com/api/v1/data")
 * @param method    请求方法 (HTTP_METHOD_GET 或 HTTP_METHOD_POST)
 * @param token     认证Token (如果没有传 NULL，如果有会自动添加 Authorization: Bearer 头)
 * @param data      发送的数据 (如果是 GET 传 NULL，如果是 POST 传 JSON 字符串)
 * * @return char* 服务器返回的响应内容 (字符串)。
 * 注意：使用者必须在使用完后调用 free() 释放这块内存！
 * 如果请求失败，返回 NULL。
 */
char* http_send_request(const char *url, int method, const char *token, const char *data);

#ifdef __cplusplus
}
#endif