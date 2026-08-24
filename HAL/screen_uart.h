/*
 * uart.h
 *
 *  Created on: 2025��1��16��
 *      Author: Administrator
 */

#ifndef BOARDDRV_UARTDRV_UART_H_
#define BOARDDRV_UARTDRV_UART_H_

/***************************** Include Files *********************************/
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
//#include "ft_gpio.h"
/************************** Constant Definitions *****************************/
//#define         BASE_ADDR							0x41220000
//#define         BASE_ADDR							0x41200000
  
extern volatile void* pl_uart_screen_base_addr;    // For PL_AXI_UART_BASE (0x41220000)
 
#define         AXI_UART_CLK	                   (29491200)
#define         AXI_UART_RBR(x) 				   ( (volatile uint32_t )(pl_uart_screen_base_addr ) + x*0x2000 + 0x00001000)
#define	        AXI_UART_THR(x) 				   ( (volatile uint32_t )(pl_uart_screen_base_addr ) + x*0x2000 + 0x00001000)
#define         AXI_UART_IER(x) 				   ( (volatile uint32_t )(pl_uart_screen_base_addr ) + x*0x2000 + 0x00001004)
#define         AXI_UART_IIR(x) 				   ( (volatile uint32_t )(pl_uart_screen_base_addr) + x*0x2000 + 0x00001008)//
#define	        AXI_UART_FCR(x) 				   ( (volatile uint32_t )(pl_uart_screen_base_addr ) + x*0x2000 + 0x00001008)
#define	        AXI_UART_LCR(x) 				   ( (volatile uint32_t )(pl_uart_screen_base_addr ) + x*0x2000 + 0x0000100C)
#define	        AXI_UART_MCR(x) 				   ( (volatile uint32_t )(pl_uart_screen_base_addr ) + x*0x2000 + 0x00001010)
#define         AXI_UART_LSR(x) 				   ( (volatile uint32_t )(pl_uart_screen_base_addr ) + x*0x2000 + 0x00001014)
#define         AXI_UART_MSR(x) 				   ( (volatile uint32_t )(pl_uart_screen_base_addr ) + x*0x2000 + 0x00001018)//
#define         AXI_UART_SCR(x) 				   ( (volatile uint32_t )(pl_uart_screen_base_addr ) + x*0x2000 + 0x0000101c)//
#define         AXI_UART_DLL(x) 				   ( (volatile uint32_t )(pl_uart_screen_base_addr ) + x*0x2000 + 0x00001000)
#define         AXI_UART_DLM(x) 				   ( (volatile uint32_t )(pl_uart_screen_base_addr ) + x*0x2000 + 0x00001004)
#define         LSR_TX_READY     			       0x01  // Data Ready
#define         LSR_TX_BUFFER_EMPTY 		       0x20  // Transmit  reg empty/

#define			GPIO_WRITE32(addr, data)		(*(volatile unsigned int *)(addr) = data)
#define			GPIO_READ32(addr)				(*(volatile unsigned int *)(addr))



/************************** Function Prototypes ******************************/
int32_t sysUartInit(uint8_t channel,uint32_t baudrate);
int32_t sysUartSend(uint8_t channel,uint8_t *data,uint32_t size);
int32_t sysUartRecv(uint8_t channel,uint8_t *data,uint32_t *size);



int32_t  sysRs422Init(uint8_t channel,uint32_t baudrate);
int32_t sysRs422Send(uint8_t channel,uint8_t *data,uint32_t size);
int32_t sysRs422Recv(uint8_t channel,uint8_t *data,uint32_t *size);

int32_t sysA429Init(uint8_t channel,uint32_t baudrate);
int32_t sysA429Recv(uint8_t channel,uint32_t *data,uint32_t *size);
int32_t sysA429Send(uint8_t channel,uint32_t *data,uint32_t size);
#endif /* BOARDDRV_UARTDRV_UART_H_ */
