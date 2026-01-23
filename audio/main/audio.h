#ifndef _AUDIO_H_
#define _AUDIO_H_

#include <driver/gpio.h>

/** 初始化喇叭
 * @param bclk 时钟GPIO
 * @param ws 声道线GPIO
 * @param dout 数据GPIO
 * @param sample_rate 采样率
 * @return 无
 */
void init_speaker(gpio_num_t bclk,gpio_num_t ws,gpio_num_t dout,uint32_t sample_rate);

/** 初始化PDM喇叭
 * @param dat 数据GPIO
 * @param clk 时钟GPIO
 * @param sample_rate 采样率
 * @return 无
 */
void init_pdm_microphone(gpio_num_t dat,gpio_num_t clk,uint32_t sample_rate);

/** 音频输出
 * @param data 16位pcm数据
 * @param samples 写入的数据长度，单位（字）
 * @return 实际写入的数据长度，单位（字）
 */
int audio_write(const int16_t* data, int samples);

/** 录音读取
 * @param data 16位pcm数据
 * @param samples 要求读取的数据长度，单位（字）
 * @return 实际读取的数据长度x，单位（字）
 */
int audio_read(int16_t* dest, int samples);

#endif