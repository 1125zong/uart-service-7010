#ifndef __HAL_AXI_16550_H_
#define __HAL_AXI_16550_H_

#include <stdint.h>
#include "hal_com.h"
#include <fcntl.h>
#include <unistd.h>
#include <sys/mman.h>

//镜像串口：370收     374发

// rxuart( *addr 370+ PL_AXI_BASE ,port[15:0] ) 
// rxuart( *addr 374+PL_AXI_BASE  ,port[15:0] ) 


#define PL_AXI_BASE  				0x40000000

#define PL_AXI_UART_BASE  			0x41200000
#define PL_AXI_UART_SCREEN_BASE		0x41220000
  
extern volatile void* pl_uart_base_addr;  // For PL_AXI_UART_BASE (0x41200000)


#define UART_TYPE_BASE  			0x330 
typedef enum{
	UART_MODE_RS232,
	UART_MODE_RS485_TX,
	UART_MODE_RS485_RX,
	UART_MODE_RS485,
	UART_MODE_RS422,
	UART_MODE_MAX
}UART_TYPE;


/* LED偏移 */

//锁定状态LED偏移
#define LOCKED_LED_OFFSET  					(0x30)

//发送LED偏移
#define SEND_LED_OFFSET  					(0x230)

//接收LED偏移
#define RECEIVE_LED_OFFSET  				(0x130)

//设备锁定LED偏移
#define DEVICE_LOCKED_LED_OFFSET 			(0x270)

//看门狗控制寄存器偏移
#define WDT_CTRL_REG_OFFSET 				(0x274)

//看门狗喂狗寄存器偏移
#define WDT_FEED_REG_OFFSET 				(0x278)





/* Base addresses and register offsets */
#define AXI_UART_BASE(n)         ((int32_t)pl_uart_base_addr + 0x2000 * n) /* ����ַ */
#define AXI_16550_INT            (84)
#define AXI_16550_CLK            (29491200) //29491200
#define AXI_16550_CLK1           (32000000)
#define AXI_16550_RBR            (0x1000)
#define AXI_16550_THR            (0x1000)
#define AXI_16550_IER            (0x1004)
#define AXI_16550_IIR            (0x1008)
#define AXI_16550_FCR            (0x1008)
#define AXI_16550_LCR            (0x100C)
#define AXI_16550_MCR            (0x1010)
#define AXI_16550_LSR            (0x1014)
#define AXI_16550_MSR            (0x1018)
#define AXI_16550_SCR            (0x101C)
#define AXI_16550_DLL            (0x1000)
#define AXI_16550_DLM            (0x1004)
#define BRAM_KZ                  (0x00000004)

/* Control bit definitions */
#define LCR_SBRK                  0x40  /* BREAK �źſ���λ */
#define LSR_TX_READY              0x01  /* Data Ready */
//#define LSR_TX_BUFFER_EMPTY       (1<<6) /* Transmit reg empty */
#define LSR_THER                  (1<<5)

/* Line Status Register (LSR) bits */
#define LSR_DR                    0x01  /* Data Ready */
#define LSR_OE                    0x02  /* Overrun Error */
#define LSR_PE                    0x04  /* Parity Error */
#define LSR_FE                    0x08  /* Framing Error */
#define LSR_BI                    0x10  /* Break Interrupt */
#define LSR_THRE                  0x20  /* Transmitter Holding Register Empty (FIFO has space) */
#define LSR_TEMT                  0x40  /* Transmitter Empty (FIFO and shift register empty) */

/* XON/XOFF control characters */
#define XON_CHAR                  0x11  /* XON �ַ���DC1��*/
#define XOFF_CHAR                 0x13  /* XOFF �ַ���DC3��*/
#define LSR_THRE_MASK             0x20  /* ���ͱ��ּĴ���Ϊ�ձ�־λ */

typedef struct usart_info
{
	unsigned int  baud_rate;
	unsigned char data_bit;
	unsigned char stop_bit;
	unsigned char parity;
	unsigned char mark;
	unsigned char space;
	unsigned char usart_mcr_dtr;
	unsigned char usart_mcr_rts;
	unsigned char usart_crtscts;
	unsigned char IX_on;
	unsigned char IX_off; //XonXoff
} usart_info_t;

/* Function declarations */
void userAxiCfgWrite(unsigned int channel, unsigned int offset, unsigned int data);
unsigned int userAxiCfgRead(unsigned int channel, unsigned int offset);
int axi16550Recv(unsigned int channel, uint8_t *buffer, uint32_t *len,
                 uint32_t *frame_err, uint32_t *parity_err,
                 uint32_t *overrun_err, uint32_t *break_cnt);
int axi16550_TxReady(unsigned int channel);
int axi16550SendNoWait(unsigned int channel, uint8_t *buffer, uint32_t len);
int axi16550Send(unsigned int channel, uint8_t *buffer, uint32_t len);
void axi16550BaudInit(unsigned int channel, unsigned int baud);
void axi16550SendStartBreak(unsigned int channel);
void axi16550SendStopBreak(unsigned int channel);
void send_xon_xoff_char(uint8_t channel, uint8_t is_xon);
void axi16550Init(unsigned int channel, unsigned int baud);
void axi165502CInit(usart_info_t *uart_instance, int channel);
void txled(int i, int action);
void rxled(int i, int action);
void Portled(int i, int action);
int axi16550FIFOInit(int port);
void FPGA_Info_Read(void);
void axi16550CheckSelfInit(unsigned int channel, unsigned int baud);

uint8_t UART_TX_FIFO_Ready(uint8_t channel);
#endif /* AXI_16550_H_ */
