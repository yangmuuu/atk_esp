#include "uart.h"

void uart_init(uint32_t baudrate)
{
	uart_config_t uart_config = {0};
	uart_config.baud_rate = baudrate;
	uart_config.data_bits = UART_DATA_8_BITS;
	uart_config.parity = UART_PARITY_DISABLE;
	uart_config.stop_bits = UART_STOP_BITS_1;
	uart_config.flow_ctrl = UART_HW_FLOWCTRL_DISABLE;
	uart_config.rx_flow_ctrl_thresh = 122;
	uart_config.source_clk = UART_SCLK_APB;

	// 要设置的串口编号
	uart_param_config(USART_UX, &uart_config);

	// 映射引脚 uart号, tx, rx, rts, cts
	uart_set_pin(USART_UX, USART_TX_GPIO_PIN, USART_RX_GPIO_PIN, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);

	// 安装串口驱动 uart号，rx缓冲区大小，tx缓冲区大小，事件队列大小，事件队列，优先级
	uart_driver_install(USART_UX, RX_BUF_SIZE * 2, RX_BUF_SIZE * 2, 20, NULL, 0);
}