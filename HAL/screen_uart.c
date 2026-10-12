/*
 * uart.c
 *
 *  Created on: 2025��1��16��
 *      Author: Administrator
 */
#include "screen_uart.h"

//#include <posLog.h>


static int32_t axiUartInit(uint8_t Channel,uint32_t Baudrate);
static int32_t axiUartSend(uint8_t Channel,uint8_t *data,uint32_t len);
int32_t axiUartRecv(uint8_t Channel,uint8_t *data,uint32_t *len);
typedef struct 
{
	uint32_t  count:8;
	uint32_t  fifo_empty:1;
	uint32_t  fifo_full:1;
	uint32_t  reserve:22;
}Arinc429_Status;

static int32_t axiUartInit(uint8_t Channel,uint32_t Baudrate)
{
    unsigned int div;
    unsigned char dlm,dll;
    unsigned int reg;
    
    // printf("Channel: %d\n", Channel);
    div = AXI_UART_CLK/16/Baudrate;
    
    /* 临时添加 */
    // printf("UART channel=%u, clk=%u, baud=%u, div=%u, DLM=0x%02X, DLL=0x%02X\n",
    //    Channel,
    //    AXI_UART_CLK,
    //    Baudrate,
    //    div,
    //    (div >> 8) & 0xFF,
    //    div & 0xFF);
    /*************/

    dlm = (div>>8)&0xFF;
    dll = div&0xFF;
    reg = GPIO_READ32(AXI_UART_LCR(Channel));
    GPIO_WRITE32(AXI_UART_LCR(Channel),reg|0x80);
    GPIO_WRITE32(AXI_UART_DLM(Channel),dlm);
    GPIO_WRITE32(AXI_UART_DLL(Channel),dll);
    GPIO_WRITE32(AXI_UART_LCR(Channel),reg);
    GPIO_WRITE32(AXI_UART_LCR(Channel),0x03);
    GPIO_WRITE32(AXI_UART_FCR(Channel),0x87);
    GPIO_WRITE32(AXI_UART_FCR(Channel),0x81);
    GPIO_WRITE32(AXI_UART_MCR(Channel),0x00);
    GPIO_WRITE32(AXI_UART_IER(Channel),0x00);
    return 0;
}

static int32_t axiUartSend(uint8_t Channel,uint8_t *data,uint32_t len)
{
    uint32_t i=0;
    if(data == NULL || len == 0)
        return -1;
    for(i=0;i<len;i++)
    {
        // Wait for the transmitter holding register to be empty
        while ((GPIO_READ32(AXI_UART_LSR(Channel)) & LSR_TX_BUFFER_EMPTY) == 0);
        // Write the data to the UART
        GPIO_WRITE32(AXI_UART_THR(Channel),data[i]);
    }
    return 0;
}

int32_t axiUartRecv(uint8_t Channel,uint8_t *data,uint32_t *len)
{
    *len = 0;
    // Read data until there is no more data available
    while(GPIO_READ32(AXI_UART_LSR(Channel)) & LSR_TX_READY)
    {
        // Read the data from the UART
    	data[(*len)++] = GPIO_READ32(AXI_UART_RBR(Channel));
    }

    if(data == NULL || *len == 0)
        return -1;
    return 0;
}



int32_t sysUartInit(uint8_t channel,uint32_t baudrate)
{
	return axiUartInit(channel ,baudrate);
}

int32_t sysUartSend(uint8_t channel,uint8_t *data,uint32_t size)
{
	return axiUartSend(channel ,data,size);
}

int32_t sysUartRecv(uint8_t channel,uint8_t *data,uint32_t *size)
{
	return axiUartRecv(channel ,data,size);
}



//void uart_test(void)
//{
//    int ret_usart;
//    
//        
//    uint8_t send_buf1[] = {0xfe, 0xfd, 0x21, 0x00, 0x02, 0x0f, 0x00, 0xdd, 0xcc, 0xbb, 0xaa};
//    uint8_t send_buf2[] = {0xfe, 0xfd, 0x22, 0x00, 0x00, 0xdd, 0xcc, 0xbb, 0xaa};
//    uint8_t send_buf3[] = {0xfe, 0xfd, 0x32, 0x00, 0x0C, 0x00, 0x00, 0x00, 0x00, 0xbc, 0xaa, 0xc8, 0xf3, 0xca, 0xb5, 0xd2, 0xb5, 0xdd, 0xcc, 0xbb, 0xaa};
//    
//    printf("sysUartInit before\n");
//        	
//	sysUartInit(0,115200);
//	
//	printf("sysUartInit after\n");
//	     
//	while(1)
//	{   
//		ret_usart = sysUartSend(0 ,  send_buf1  ,  sizeof(send_buf1) / sizeof(uint8_t));
//		ret_usart = sysUartSend(0 ,  send_buf2  ,  sizeof(send_buf2) / sizeof(uint8_t));
//		ret_usart = sysUartSend(0 ,  send_buf3  ,  sizeof(send_buf3) / sizeof(uint8_t));
//				
//		if(ret_usart == 0)
//		{
//			printf("recv OK!!!\n");			
//			printf("\n");
//		}
//		ACoreOs_task_wake_after(1000);
//	}
//     
//}
