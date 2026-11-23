#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <stdint.h>
#include <stddef.h>
#include <fcntl.h>
#include "hal_axi16550.h"
#include "app_com.h"
#include "app_uart.h"

// const int bauderate_table[] = {134, 150, 300, 600, 1200, 1800, 2400, 4800, 7200, 9600, 19200, 38400, 57600, 115200, 230400, 460800, 921600, 1843200, 3686400, 7372800};
const int bauderate_table[] = {300, 600, 1200, 2400, 4800, 7200, 9600, 19200, 38400, 57600, 115200, 230400, 460800, 921600, 150, 134, 110, 75, 50};
const unsigned int data_bit_table[] = {5, 6, 7, 8};

int socket_send_to_middle(int sock_fd, char *buf, int buf_len)
{
	int sent = 0;
	while (sent < buf_len)
	{
		ssize_t n = send(sock_fd, buf + sent, buf_len - sent, MSG_NOSIGNAL);
		if (n < 0 && errno == EINTR)
			continue;
		if (n <= 0)
		{
			LOG_ERROR("Failed to send Real COM command response on fd=%d\n", sock_fd);
			return ERROR;
		}
		sent += n;
	}
	return OK;
}

/**
 * @brief 计算指定波特率的网络发送参数
 */
int calculate_send_parameters(ChannelState *channel)
{
	int baudrate = channel->baudrate;

	// 根据波特率设置基本发送间隔
	if (baudrate <= 9600)
	{
		channel->net_send_cfg.send_interval_ms = 10; // 低速率用较长间隔
	}
	else if (baudrate <= 115200)
	{
		channel->net_send_cfg.send_interval_ms = 1; // 中速率用中等间隔
	}
	else
	{
		channel->net_send_cfg.send_interval_ms = 1; // 高速率用较短间隔
	}

	// 计算在这个间隔内会产生多少数据
	channel->net_send_cfg.packet_size = ((baudrate * channel->net_send_cfg.send_interval_ms) / (BITS_PER_CHAR * 1000)) * 40 / 10;

	// 确保包大小在合理范围内
	if (channel->net_send_cfg.packet_size < MIN_PACKET_SIZE)
	{
		channel->net_send_cfg.packet_size = MIN_PACKET_SIZE;
	}
	else if (channel->net_send_cfg.packet_size > MAX_PACKET_SIZE)
	{
		channel->net_send_cfg.packet_size = MAX_PACKET_SIZE;
	}

	LOG_WARN("Channel send params - baudrate: %d, interval: %dms, packet size: %d",
			 baudrate,
			 channel->net_send_cfg.send_interval_ms,
			 channel->net_send_cfg.packet_size);

	return 0;
}

static int uart_tx_busy_locked(ChannelState *ch, int channel)
{
	return ch->uart_state == UART_STATE_OPENED &&
	       (!ring_buffer_is_empty(&ch->buffer_net) || !axi16550_Tx_IDLE(channel) ||
	        ch->rs485_tx_active);
}

/* 调用者持有 g_config_mutex。 */
static int uart_set_interface_type_locked(unsigned int channel, uint8_t interface_type)
{
	UART_TYPE hw_type;
	ChannelState *ch = &g_system_config.channels[channel];

	switch (interface_type)
	{
	case INTERFACE_TYPE_RS232:
		hw_type = UART_MODE_RS232;
		break;
	case INTERFACE_TYPE_RS422:
		hw_type = UART_MODE_RS422;
		break;
	case INTERFACE_TYPE_RS485_2WIRE:
	case INTERFACE_TYPE_RS485_4WIRE:
		hw_type = UART_MODE_RS485_RX;
		break;
	default:
		return ERROR;
	}

	/* RS-485 重复写接收模式也会截断正在发送的数据。 */
	if (uart_tx_busy_locked(ch, channel) &&
	    (interface_type != ch->interface_type || interface_type == INTERFACE_TYPE_RS485_4WIRE ||
	     interface_type == INTERFACE_TYPE_RS485_2WIRE))
		return ERROR;

	if (!axi16550TypeSet(hw_type, channel))
		return ERROR;

	ch->rs485_tx_active = 0;
	ch->interface_type = interface_type;
	return OK;
}

int uart_set_interface_type(unsigned int channel, uint8_t interface_type)
{
	if (channel >= NUM_PORTS || semTake(g_config_mutex, WAIT_FOREVER) != OK)
		return ERROR;

	int result = uart_set_interface_type_locked(channel, interface_type);
	semGive(g_config_mutex);
	return result;
}

static void uart_set_modem_lines_locked(ChannelState *ch, int channel,
                                        unsigned char dtr, unsigned char rts)
{
	unsigned int mcr = userAxiCfgRead(channel, AXI_16550_MCR);
	mcr &= ~(MCR_DTR | MCR_RTS);
	if (dtr)
		mcr |= MCR_DTR;
	if (rts)
		mcr |= MCR_RTS;

	userAxiCfgWrite(channel, AXI_16550_MCR, mcr);
	ch->usart_mcr_dtr = dtr;
	ch->usart_mcr_rts = rts;
}

static int uart_baudrate_is_valid(uint32_t baudrate)
{
	if (baudrate == 0)
		return 0;

	uint32_t divisor = AXI_16550_CLK / 16 / baudrate;
	return divisor > 0 && divisor <= UINT16_MAX;
}

int init_usart(ChannelState *ch, int client_socket, char *buf, int buf_len, int channel)
{
	if (buf_len < 5)
		return ERROR;

	const uint8_t *cmd = (const uint8_t *)buf;
	unsigned int baud_index = cmd[2];
	if (baud_index != 0xff && baud_index >= sizeof(bauderate_table) / sizeof(bauderate_table[0]))
		return ERROR;

	usart_info_t uart_info = {0};
	uart_info.data_bit = data_bit_table[cmd[3] & 0x03];
	uart_info.stop_bit = (cmd[3] & 0x04) ? USART_STOP_BIT_2 : USART_STOP_BIT_1;
	switch (cmd[3] & 0x38)
	{
	case 0x00:
		uart_info.parity = USART_PARITY_NONE;
		break;
	case 0x08:
		uart_info.parity = USART_PARITY_EVEN;
		break;
	case 0x10:
		uart_info.parity = USART_PARITY_ODD;
		break;
	case 0x18:
		uart_info.mark = USART_IOCTL_MARK;
		break;
	case 0x20:
		uart_info.space = USART_IOCTL_SPACE;
		break;
	default:
		return ERROR;
	}

	if (semTake(g_config_mutex, WAIT_FOREVER) != OK)
		return ERROR;

	/* 0xff 表示显式波特率由后续 SETBAUD 给出。 */
	uart_info.baud_rate = baud_index == 0xff ? ch->baudrate : bauderate_table[baud_index];
	if (!uart_baudrate_is_valid(uart_info.baud_rate))
	{
		semGive(g_config_mutex);
		return ERROR;
	}

	int was_open = ch->uart_state == UART_STATE_OPENED;
	int config_changed = ch->baudrate != uart_info.baud_rate ||
	                     ch->data_bits != uart_info.data_bit ||
	                     ch->stop_bits != uart_info.stop_bit ||
	                     ch->parity != uart_info.parity ||
	                     ch->mark != uart_info.mark || ch->space != uart_info.space;
	if (config_changed && uart_tx_busy_locked(ch, channel))
	{
		semGive(g_config_mutex);
		return ASPP_COMMAND_DEFERRED;
	}

	/* 同参数的恢复命令不能复位正在发送的 FIFO。 */
	if (!was_open || config_changed)
	{
		if (uart_set_interface_type_locked(channel, ch->interface_type) != OK)
		{
			semGive(g_config_mutex);
			return ERROR;
		}
		axi165502CInit(&uart_info, channel);
		ch->baudrate = uart_info.baud_rate;
		ch->data_bits = uart_info.data_bit;
		ch->stop_bits = uart_info.stop_bit;
		ch->parity = uart_info.parity;
		ch->mark = uart_info.mark;
		ch->space = uart_info.space;
		calculate_send_parameters(ch);
	}
	uart_set_modem_lines_locked(ch, channel, cmd[4], cmd[5]);
	ch->usart_crtscts = cmd[6];

	if (!was_open)
	{
		semTake(g_stats_mutex, WAIT_FOREVER);
		ch->tx_count = 0;
		ch->rx_count = 0;
		semGive(g_stats_mutex);
	}
	ch->uart_state = UART_STATE_OPENED;
	ch->uart_close_pending = 0;
	semGive(g_config_mutex);

	char response[5] = {buf[0], 0x03, 0, 0, 0};
	return socket_send_to_middle(client_socket, response, sizeof(response));
}

int usart_set_baudrate(ChannelState *ch, int client_socket, char *buf, int buf_len, int channel)
{
	if (buf_len < 4)
		return ERROR;

	const uint8_t *bytes = (const uint8_t *)buf + ASPP_HEADER_LEN;
	uint32_t baudrate = (uint32_t)bytes[0] << 24 | (uint32_t)bytes[1] << 16 |
	                    (uint32_t)bytes[2] << 8 | bytes[3];
	if (!uart_baudrate_is_valid(baudrate))
	{
		/* WQ Linux 驱动发送本机字节序，兼容可用的显式波特率。 */
		baudrate = (uint32_t)bytes[3] << 24 | (uint32_t)bytes[2] << 16 |
		           (uint32_t)bytes[1] << 8 | bytes[0];
		if (!uart_baudrate_is_valid(baudrate))
			return ERROR;
	}

	if (semTake(g_config_mutex, WAIT_FOREVER) != OK)
		return ERROR;
	if (ch->baudrate != (int)baudrate)
	{
		if (uart_tx_busy_locked(ch, channel))
		{
			semGive(g_config_mutex);
			return ASPP_COMMAND_DEFERRED;
		}
		axi16550BaudInit(channel, baudrate);
		ch->baudrate = baudrate;
		calculate_send_parameters(ch);
	}
	semGive(g_config_mutex);

	char response[3] = {buf[0], 'O', 'K'};
	return socket_send_to_middle(client_socket, response, sizeof(response));
}

int usart_set_xon_xoff(int client_socket, int channel, char *buf, int buf_len)
{
	if (channel < 0 || channel >= NUM_PORTS || buf_len < 2 ||
	    semTake(g_config_mutex, WAIT_FOREVER) != OK)
		return ERROR;

	/* WQ 驱动下发两个原始控制字符。 */
	ChannelState *ch = &g_system_config.channels[channel];
	ch->IX_on = (unsigned char)buf[2];
	ch->IX_off = (unsigned char)buf[3];
	semGive(g_config_mutex);

	char response[3] = {buf[0], 'O', 'K'};
	return socket_send_to_middle(client_socket, response, sizeof(response));
}

int usart_set_tx_fifo(int client_socket, int channel, char *buf, int buf_len)
{
	if (channel < 0 || channel >= NUM_PORTS || buf_len < 1)
		return ERROR;

	/* 沿用设备的 FPGA FIFO 配置，保留驱动需要的应答。 */
	char response[3] = {buf[0], 'O', 'K'};
	return socket_send_to_middle(client_socket, response, sizeof(response));
}

int usart_set_line_control(int client_socket, int channel, char *buf, int buf_len)
{
	if (channel < 0 || channel >= NUM_PORTS || buf_len < 2 ||
	    semTake(g_config_mutex, WAIT_FOREVER) != OK)
		return ERROR;

	ChannelState *ch = &g_system_config.channels[channel];
	unsigned char dtr = (unsigned char)buf[2];
	unsigned char rts = (unsigned char)buf[3];
	if (ch->uart_state == UART_STATE_OPENED)
		uart_set_modem_lines_locked(ch, channel, dtr, rts);
	else
	{
		ch->usart_mcr_dtr = dtr;
		ch->usart_mcr_rts = rts;
	}
	semGive(g_config_mutex);

	char response[3] = {buf[0], 'O', 'K'};
	return socket_send_to_middle(client_socket, response, sizeof(response));
}

int usart_set_xon(int client_socket, int channel, char *buf, int buf_len)
{
	int ret;
	/*	send_xon_xoff_char(channel, 1);*/
	char response[3] = {0};
	response[0] = buf[0];
	response[1] = 'O';
	response[2] = 'K';

	if (channel < 0)
	{
		return -1;
	}
	if (buf_len < 0)
	{
		return -1;
	}

	/*返回数据给中间件*/
	ret = socket_send_to_middle(client_socket, response, sizeof(response));

	if (ret < 0)
	{
		return -1;
	}

	return 0;
}

int usart_set_xoff(int client_socket, int channel, char *buf, int buf_len)
{
	int ret;
	/*	send_xon_xoff_char(channel, 0);*/
	char response[3] = {0};
	response[0] = buf[0];
	response[1] = 'O';
	response[2] = 'K';

	if (channel < 0)
	{
		return -1;
	}
	if (buf_len < 0)
	{
		return -1;
	}

	/*返回数据给中间件*/
	ret = socket_send_to_middle(client_socket, response, sizeof(response));

	if (ret < 0)
	{
		return -1;
	}

	return 0;
}

int usart_set_start_break(int client_socket, int channel, char *buf, int buf_len)
{
	int ret;
	char response[3] = {0};
	response[0] = buf[0];
	response[1] = 'O';
	response[2] = 'K';

	if (channel < 0)
	{
		return -1;
	}
	if (buf_len < 0)
	{
		return -1;
	}

	if (semTake(g_config_mutex, WAIT_FOREVER) != OK)
		return ERROR;
	axi16550SendStartBreak(channel);
	semGive(g_config_mutex);

	/*返回数据给中间件*/
	ret = socket_send_to_middle(client_socket, response, sizeof(response));

	if (ret < 0)
	{
		return -1;
	}
	return 0;
}

int usart_set_stop_break(int client_socket, int channel, char *buf, int buf_len)
{
	int ret;
	char response[3] = {0};
	response[0] = buf[0];
	response[1] = 'O';
	response[2] = 'K';

	if (channel < 0)
	{
		return -1;
	}
	if (buf_len < 0)
	{
		return -1;
	}

	if (semTake(g_config_mutex, WAIT_FOREVER) != OK)
		return ERROR;
	axi16550SendStopBreak(channel);
	semGive(g_config_mutex);

	/*返回数据给中间件*/
	ret = socket_send_to_middle(client_socket, response, sizeof(response));

	if (ret < 0)
	{
		return -1;
	}
	return 0;
}

int usart_report_queue(int client_socket, char *buf, int buf_len)
{
	int ret;

	char pack_buf[4];

	if (buf_len < 0)
	{
		return -1;
	}

	/*打包数据*/
	pack_buf[0] = buf[0];
	pack_buf[1] = 0x02;
	pack_buf[2] = 0x00;
	pack_buf[3] = 0x00;

	/*返回数据给中间件*/
	ret = socket_send_to_middle(client_socket, pack_buf, sizeof(pack_buf));
	if (ret < 0)
	{
		return -1;
	}
	return 0;
}

int usart_close(int client_socket, char *buf, int buf_len)
{
	int ret;

	if (buf_len < 0)
	{
		return -1;
	}

	char response[3] = {0};
	response[0] = buf[0];
	response[1] = 'O';
	response[2] = 'K';
	/*返回数据给中间件*/
	ret = socket_send_to_middle(client_socket, response, sizeof(response));

	if (ret < 0)
	{
		return -1;
	}
	return 0;
}

int handle_command(ChannelState *uart_instance, int client_socket, char *buf, int buf_len, int channel)
{
	if (channel < 0 || channel >= NUM_PORTS || buf_len < ASPP_HEADER_LEN)
		return ERROR;

	uint8_t cmd = (uint8_t)buf[0];
	uint8_t data_len = (uint8_t)buf[1];
	if (buf_len != ASPP_HEADER_LEN + data_len)
		return ERROR;

	switch (cmd)
	{
	case ASPP_CMD_PORT_INIT:
		return init_usart(uart_instance, client_socket, buf, data_len, channel);
	case ASPP_CMD_SETBAUD:
		return usart_set_baudrate(uart_instance, client_socket, buf, data_len, channel);
	case ASPP_CMD_XONXOFF:
		return usart_set_xon_xoff(client_socket, channel, buf, data_len);
	case ASPP_CMD_TX_FIFO:
		if (data_len < 1)
			return ERROR;
		return usart_set_tx_fifo(client_socket, channel, buf, data_len);
	case ASPP_CMD_LINECTRL:
		return usart_set_line_control(client_socket, channel, buf, data_len);
	case ASPP_CMD_SETXON:
		return usart_set_xon(client_socket, channel, buf, data_len);
	case ASPP_CMD_SETXOFF:
		return usart_set_xoff(client_socket, channel, buf, data_len);
	case ASPP_CMD_START_BREAK:
		return usart_set_start_break(client_socket, channel, buf, data_len);
	case ASPP_CMD_STOP_BREAK:
		return usart_set_stop_break(client_socket, channel, buf, data_len);
	case ASPP_CMD_WAIT_OQUEUE:
		return usart_report_queue(client_socket, buf, data_len);
	case ASPP_CMD_FLUSH:
		return usart_close(client_socket, buf, data_len);
	case ASPP_CMD_NOTIFY:
	case ASPP_CMD_ALIVE:
		return OK;
	default:
		LOG_ERROR("Unknown command: %d\n", cmd);
		return OK;
	}
}

// 复位端口
void reset_port(ChannelState *uart_instance, int channel)
{
	LOG_DEBUG("reset_port\n");

	// 复位串口
	// int ret;
	// unsigned char stop_bit;
	// unsigned char data_bit;
	usart_info_t uart_info;
	memset(&uart_info, 0, sizeof(uart_info));

	/*设置串口波特率*/
	uart_info.baud_rate = uart_instance->baudrate;
	LOG_DEBUG("baud_rate: %d\n", uart_info.baud_rate);

	/*data bit*/

	uart_info.data_bit = uart_instance->data_bits;
	LOG_DEBUG("data_bit: %d\n", uart_info.data_bit);

	/*stop bit*/

	uart_info.stop_bit = uart_instance->stop_bits;
	LOG_DEBUG("stop_bit: %d\n", uart_info.stop_bit);

	/* 0b111000，用于提取第3 - 5位*/
	int last_mask = uart_instance->parity;
	switch (last_mask)
	{
	case 0x00:
		LOG_DEBUG("Parity: None\n");
		// uart_instance->parity = USART_PARITY_NONE;
		uart_info.parity = USART_PARITY_NONE;
		break;
	case 0x08:
		LOG_DEBUG("Parity: Even\n");
		// uart_instance->parity = USART_PARITY_EVEN;
		uart_info.parity = USART_PARITY_EVEN;
		break;
	case 0x10:
		LOG_DEBUG("Parity: Odd\n");
		// uart_instance->parity = USART_PARITY_ODD;
		uart_info.parity = USART_PARITY_ODD;
		break;
	case 0x18:
		LOG_DEBUG("Parity: Mark\n");
		// uart_instance->mark = USART_IOCTL_MARK;
		uart_info.mark = USART_IOCTL_MARK;
		break;
	case 0x20:
		LOG_DEBUG("Parity: Space\n");
		// uart_instance->space = USART_IOCTL_SPACE;
		uart_info.space = USART_IOCTL_SPACE;
		break;
	default:
		LOG_ERROR("Unknown parity configuration:%02x \n", last_mask);
		break;
	}
	/*调用AXI_api设置串口相关寄存器*/
	axi165502CInit(&uart_info, channel);
}
