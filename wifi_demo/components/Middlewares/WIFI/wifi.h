#pragma once

#ifdef __cplusplus
extern "C" {
#endif

// 初始化 WiFi 管理器
void wifi_init_portal(void);

// 阻塞等待直到获取到 IP 地址 (用于正常启动后的等待)
void wifi_wait_for_ip(void);

#ifdef __cplusplus
}
#endif