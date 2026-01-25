#ifndef _CAMERA_H_
#define _CAMERA_H_

#include <stdint.h>
#include <stddef.h>
#include "esp_err.h"

// 图像帧数据结构
typedef struct {
    uint8_t *data;      // JPEG 数据指针
    size_t len;         // 数据长度
    uint16_t width;     // 宽
    uint16_t height;    // 高
} camera_frame_t;

/**
 * @brief 初始化 USB 摄像头 (分配内存、配置UVC)
 * @return esp_err_t ESP_OK 成功
 */
esp_err_t camera_init(void);

/**
 * @brief 抓取一帧图像 (阻塞式)
 * @note  调用者必须负责调用 camera_free_frame 释放返回的结构体
 * @return camera_frame_t* 成功返回指针，失败返回 NULL
 */
camera_frame_t* camera_capture(void);

/**
 * @brief 释放图像帧内存
 * @param frame 需要释放的帧指针
 */
void camera_free_frame(camera_frame_t *frame);

#endif