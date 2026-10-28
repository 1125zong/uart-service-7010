#include <stdio.h>
#include <stdint.h>
#include <stddef.h>
#include <fcntl.h>
#include <time.h>


//#include "common.h"
#include "hal_axi16550.h"
#include "hal_com.h"


//串口内存映射
/* 定义Linux内存映射相关变量 */
static int mem_fd = -1;
volatile void* pl_led_base_addr = MAP_FAILED;        // For PL_AXI_BASE (0x40000000)
volatile void* pl_uart_base_addr = MAP_FAILED;   // For PL_AXI_UART_BASE (0x41200000)
volatile void* pl_uart_screen_base_addr = MAP_FAILED;   // For pl_uart_screen_base_addr (0x41220000)

static const size_t PL_AXI_SIZE = 0x10000;     /* 假设PL AXI空间大小 */
static const size_t PL_AXI_UART_SIZE = 0x200000; /* PL AXI UART空间大小, 16个UART通道 * 0x2000 each */
static const size_t PL_AXI_SCREEN_UART_SIZE = 0x10000; /* 0x10000  */


/* FPGA寄存器访问函数 - Linux实现 */
int init_pl_memory_access(void)
{
    // Check if both addresses are already mapped
    if (pl_led_base_addr != MAP_FAILED && pl_uart_base_addr != MAP_FAILED) {
        return 0;  // Both already initialized
    }
    
    printf("init_pl_memory_access cross\n");
    mem_fd = open("/dev/mem", O_RDWR | O_SYNC);
    if (mem_fd == -1) {
        printf("open failed /dev/mem: %s\n", strerror(errno));
        return -1;
    }


    
    // Map PL_AXI_BASE (0x40000000) for general PL registers
    if (pl_led_base_addr == MAP_FAILED) 
    {
        pl_led_base_addr = mmap(NULL, PL_AXI_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, mem_fd, PL_AXI_BASE);
        if (pl_led_base_addr == MAP_FAILED) 
        {
            printf("PL_AXI_BASE (0x%08x) mmap failed: %s\n", PL_AXI_BASE, strerror(errno));
            close(mem_fd);
            mem_fd = -1;
            return -1;
        }
        printf("PL_AXI_BASE ok: 0x%08x - 0x%08x\n", PL_AXI_BASE, PL_AXI_BASE + PL_AXI_SIZE - 1);
    }
    
    // Map PL_AXI_UART_BASE (0x41200000) for UART registers
    if (pl_uart_base_addr == MAP_FAILED) 
    {
        pl_uart_base_addr = mmap(NULL, PL_AXI_UART_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, mem_fd, PL_AXI_UART_BASE);
        if (pl_uart_base_addr == MAP_FAILED) 
        {
            printf("PL_AXI_UART_BASE (0x%08x) mmap failed: %s\n", PL_AXI_UART_BASE, strerror(errno));
            // Unmap the first mapping if second fails
            if (pl_led_base_addr != MAP_FAILED) 
            {
                munmap((void*)pl_led_base_addr, PL_AXI_SIZE);
                pl_led_base_addr = MAP_FAILED;
            }
            close(mem_fd);
            mem_fd = -1;
            return -1;
        }
        printf("PL_AXI_UART_BASE ok: 0x%08x - 0x%08x\n",  PL_AXI_UART_BASE, PL_AXI_UART_BASE + PL_AXI_UART_SIZE - 1);
    }

    // Map PL_AXI_UART_SCREEN_BASE (0x41220000) for UART screen registers
    if (pl_uart_screen_base_addr == MAP_FAILED)
    {
        pl_uart_screen_base_addr = mmap(NULL, PL_AXI_SCREEN_UART_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, mem_fd, PL_AXI_UART_SCREEN_BASE);
        if (pl_uart_screen_base_addr == MAP_FAILED)
        {
            printf("PL_AXI_UART_SCREEN_BASE (0x%08x) mmap failed: %s\n", PL_AXI_UART_SCREEN_BASE, strerror(errno));
            // Unmap the first two mappings if third fails
            if (pl_led_base_addr != MAP_FAILED)
            {
                munmap((void*)pl_led_base_addr, PL_AXI_SIZE);
                pl_led_base_addr = MAP_FAILED;
            }
            if (pl_uart_base_addr != MAP_FAILED)
            {
                munmap((void*)pl_uart_base_addr, PL_AXI_UART_SIZE);
                pl_uart_base_addr = MAP_FAILED;
            }
            close(mem_fd);
            mem_fd = -1;
            return -1;

        }
        else 
        {
            printf("PL_AXI_UART_SCREEN_BASE ok: 0x%08x\n", pl_uart_screen_base_addr);
        }
    }
    
    return 0;
}


#if 1
/* Cleanup function to unmap both addresses */
void cleanup_pl_memory_access(void)
{
    if (pl_led_base_addr != MAP_FAILED) {
        munmap((void*)pl_led_base_addr, PL_AXI_SIZE);
        pl_led_base_addr = MAP_FAILED;
    }
    
    if (pl_uart_base_addr != MAP_FAILED) {
        munmap((void*)pl_uart_base_addr, PL_AXI_UART_SIZE);
        pl_uart_base_addr = MAP_FAILED;
    }
    if (pl_uart_screen_base_addr != MAP_FAILED) {
        munmap((void*)pl_uart_screen_base_addr, PL_AXI_SCREEN_UART_SIZE);
        pl_uart_screen_base_addr = MAP_FAILED;
    }
    
    if (mem_fd != -1) {
        close(mem_fd);
        mem_fd = -1;
    }
}
#endif

void userAxiCfgWrite(unsigned int channel, unsigned int offset, unsigned int data)
{
    sysAxiWriteLong(AXI_UART_BASE(channel) + offset, data);
}

unsigned int userAxiCfgRead(unsigned int channel, unsigned int offset)
{
    unsigned int data = 0;
    data = sysAxiReadLong(AXI_UART_BASE(channel) + offset);
    return data;
}

int axi16550_TxReady(unsigned int channel)
{
    if ((userAxiCfgRead(channel, AXI_16550_LSR) & LSR_THRE) == 0)
        return 0;  /* Not ready (THR or FIFO not empty) */
    else
        return 1;  /* Ready (THR or FIFO is empty) */
}


int axi16550_Tx_IDLE(unsigned int channel)
{
    if ((userAxiCfgRead(channel, AXI_16550_LSR) & LSR_TEMT) == 0)
        return 0;  /* Not ready (THR or FIFO not empty) */
    else
        return 1;  /* Ready (THR or FIFO is empty) */
}

int axi16550SendNoWait(unsigned int channel, uint8_t *buffer, uint32_t len)
{
    uint32_t i = 0;
    if (buffer == NULL || len == 0)
    {
        return -1;
    }
    for (i = 0; i < len; i++)
    {
        userAxiCfgWrite(channel, AXI_16550_THR, buffer[i]);
    }
    return 0;
}

int axi16550Send(unsigned int channel, uint8_t *buffer, uint32_t len)
{
    uint32_t i = 0;
    if (buffer == NULL || len == 0)
    {
        return -1;
    }
    while (!axi16550_TxReady(channel));
    for (i = 0; i < len; i++)
    {
        userAxiCfgWrite(channel, AXI_16550_THR, buffer[i]);
    }

    uint32_t timeout = 0;
    timeout = 100000;
    while(1)
    {
        uint8_t lsr = userAxiCfgRead(channel, AXI_16550_LSR);
        if( (lsr & (1<<6)) && timeout-- ) // TEMT 位
        {
            break;
        }
    }

    // 超时判断
    if(timeout == 0)
    {
        return -2; // 发送超时
    }
        
    return 0;
}

int axi16550Recv(unsigned int channel, uint8_t *buffer, uint32_t *len,
                 uint32_t *frame_err, uint32_t *parity_err,
                 uint32_t *overrun_err, uint32_t *break_cnt)
{
    *len = 0;
    uint32_t local_frame  = 0;
    uint32_t local_parity = 0;
    uint32_t local_over   = 0;
    uint32_t local_break  = 0;

    /* Read data until there is no more data available */
    while (1)
    {
        uint8_t lsr = userAxiCfgRead(channel, AXI_16550_LSR);
        if (!(lsr & LSR_DR))
            break;

        /* 捕获错误位 (每个字符的LSR错误位, 读取RBR后更新) */
        if (lsr & LSR_FE) local_frame++;
        if (lsr & LSR_PE) local_parity++;
        if (lsr & LSR_OE) local_over++;
        if (lsr & LSR_BI) local_break++;

        /* Read the data from the UART */
        buffer[(*len)++] = userAxiCfgRead(channel, AXI_16550_RBR);
    }

    /* 返回错误计数 */
    if (frame_err)   *frame_err  = local_frame;
    if (parity_err)  *parity_err = local_parity;
    if (overrun_err) *overrun_err = local_over;
    if (break_cnt)   *break_cnt  = local_break;

    if (buffer == NULL || *len == 0)
        return -1;
    return 0;
}

void axi16550BaudInit(unsigned int channel, unsigned int baud)
{
    unsigned int div;
    unsigned short dlm, dll;
    unsigned char reg;
    div = AXI_16550_CLK / 16 / baud;

    /*------- 临时添加 --------*/
    // printf("axi16550BaudInit UART channel=%u, clk=%u, baud=%u, div=%u, DLM=0x%02X, DLL=0x%02X\n",
    //    channel,
    //    AXI_16550_CLK,
    //    baud,
    //    div,
    //    (div >> 8) & 0xFF,
    //    div & 0xFF);
    /*******************/
    
    dlm = (div >> 8) & 0xFF;
    dll = div & 0xFF;
    reg = userAxiCfgRead(channel, AXI_16550_LCR);
    userAxiCfgWrite(channel, AXI_16550_LCR, reg | 0x80);
    userAxiCfgWrite(channel, AXI_16550_DLM, dlm);
    userAxiCfgWrite(channel, AXI_16550_DLL, dll);
    userAxiCfgWrite(channel, AXI_16550_LCR, reg);
}

void axi16550SendStartBreak(unsigned int channel)
{
    unsigned char lcr = userAxiCfgRead(channel, AXI_16550_LCR);
    lcr |= LCR_SBRK;
    userAxiCfgWrite(channel, AXI_16550_LCR, lcr);

    //taskDelay(10); 
}

void axi16550SendStopBreak(unsigned int channel)
{
    unsigned char lcr = userAxiCfgRead(channel, AXI_16550_LCR);
    lcr &= ~LCR_SBRK;
    userAxiCfgWrite(channel, AXI_16550_LCR, lcr);
}

void send_xon_xoff_char(uint8_t channel, uint8_t is_xon)
{
    uint8_t control_char = is_xon ? XON_CHAR : XOFF_CHAR;
    while (!(userAxiCfgRead(channel, AXI_16550_LSR) & LSR_THRE_MASK));
    userAxiCfgWrite(channel, AXI_16550_THR, control_char);
}


uint8_t axi16550TypeSet(UART_TYPE uart_type, unsigned int channel)
{
    uint32_t value = 0;
    switch (uart_type)
    {
        case UART_MODE_RS232:
        case UART_MODE_RS485:
        case UART_MODE_RS422:
            value = 0x02;
            break;
        case UART_MODE_RS485_TX:
            value = 0x01;
            break;
        case UART_MODE_RS485_RX:
            value = 0x00;
            break;
        default:
            return 0;
            break;
    }
    sysAxiWriteLong(PL_AXI_BASE + UART_TYPE_BASE + channel * 4 , value);
    // sysAxiWriteLong(PL_AXI_BASE + UART_TYPE_BASE, value);
    return 1;
}

// 写FPGA 
int port_mirror_apply(unsigned char port, unsigned char direction)
{
    int32_t reg_addr;
    unsigned char fpga_port;

    if (port < 1 || port > 16 || direction > 1)
    {
        return ERROR;
    }
    
    if (pl_led_base_addr == MAP_FAILED)
    {
        return ERROR;
    }

    fpga_port = 16 - port;
    if (direction == 0x00)
    {
        reg_addr = (int32_t)pl_led_base_addr + PORT_MIRROR_RX_OFFSET;
    }
    else
    {
        reg_addr = (int32_t)pl_led_base_addr + PORT_MIRROR_TX_OFFSET;
    }

    sysAxiWriteLong(reg_addr, fpga_port);
    return OK;
}


void axi16550CheckSelfInit(unsigned int channel, unsigned int baud)
{
    unsigned int div;
    unsigned short dlm, dll;
    unsigned char reg;
    div = AXI_16550_CLK1 / 16 / baud;
    dlm = (div >> 8) & 0xFF;
    dll = div & 0xFF;
    reg = userAxiCfgRead(channel, AXI_16550_LCR);
    userAxiCfgWrite(channel, AXI_16550_LCR, reg | 0x80);
    userAxiCfgWrite(channel, AXI_16550_DLM, dlm); /* dlm */
    userAxiCfgWrite(channel, AXI_16550_DLL, dll); /* dll */
    userAxiCfgWrite(channel, AXI_16550_LCR, reg);
    userAxiCfgWrite(channel, AXI_16550_LCR, 0x03);
    // FCR[7:6] = 10b = 0x80: FIFO trigger level = 8 bytes
    // FCR[7:6] = 11b = 0xC0: FIFO trigger level = 14 bytes
    userAxiCfgWrite(channel, AXI_16550_FCR, 0x87);
    userAxiCfgWrite(channel, AXI_16550_FCR, 0xC1);  // 触发等级设为14字节
    userAxiCfgWrite(channel, AXI_16550_MCR, 0x10);  /* Normal mode:(bit 4 = 0)   loopback mode:(bit 4 = 1)*/
    userAxiCfgWrite(channel, AXI_16550_IER, 0x00);
}

void axi165502CInit(usart_info_t *uart_instance, int channel)
{
    unsigned int div;
    unsigned short dlm, dll;
    unsigned char reg, lcr = 0;

    switch (uart_instance->data_bit)
    {
        case 5:
            lcr |= 0x00;
            break;
        case 6:
            lcr |= 0x01;
            break;
        case 7:
            lcr |= 0x02;
            break;
        case 8:
            lcr |= 0x03;
            break;
    }
    
    if (uart_instance->stop_bit == 2) lcr |= 0x04;
    if (uart_instance->parity != 0)
    {
        lcr |= 0x08;
        if (uart_instance->parity == 2) lcr |= 0x10;
    }

    div = AXI_16550_CLK / 16 / uart_instance->baud_rate;

    /*------- 临时添加 --------*/
    // printf("axi165502CInit UART channel=%d, clk=%u, baud=%u, "
    //    "div=%u, DLM=0x%02X, DLL=0x%02X\n",
    //    channel,
    //    (unsigned int)AXI_16550_CLK,
    //    uart_instance->baud_rate,
    //    div,
    //    (div >> 8) & 0xFF,
    //    div & 0xFF);
    /*******************/

    dlm = (div >> 8) & 0xFF;
    dll = div & 0xFF;

    reg = userAxiCfgRead(channel, AXI_16550_LCR);
    userAxiCfgWrite(channel, AXI_16550_LCR, reg | 0x80);
    userAxiCfgWrite(channel, AXI_16550_DLM, dlm);
    userAxiCfgWrite(channel, AXI_16550_DLL, dll);
    userAxiCfgWrite(channel, AXI_16550_LCR, reg & 0x7F);
    userAxiCfgWrite(channel, AXI_16550_LCR, reg | lcr);
    userAxiCfgWrite(channel, AXI_16550_MCR, 0x00); /* MCR bit 4: 1 = loopback enabled (0x10), 0 = loopback disabled (0x00)*/
    userAxiCfgWrite(channel, AXI_16550_IER, 0x00);
    
    axi16550FIFOInit(channel);
}

void test_init(void)
{
    uint8_t i=0;
    printf("test_init\n");
    for (i = 0; i < 16; i++)
    {
        usart_info_t uart_info;
        memset(&uart_info, 0, sizeof(uart_info));
        uart_info.baud_rate = 115200;
        uart_info.data_bit = 8;
        uart_info.stop_bit = 1;
        uart_info.parity = 0x00;
        axi165502CInit(&uart_info, i);	 
     }    

}

#if 0
void test_uart(void)
{   
    printf("\nUART Test\r\n");
     /*
      *  先发送再接收
     */
    char send_buffer[1] = {0x88};
    char recv_buffer[128] = {0};
    uint32_t size; 
    while(1)
    {
        // axi16550Send(14, send_buffer, 1); /* Send "Hello world" */

        // printf("size=%d\n", size);

        printf("send_data: 0x%x\n", send_buffer[0]);
        axi16550Send(15, send_buffer, 1);
        

        // axi16550Recv(0, (uint8_t *)recv_buffer, &size,
        //              NULL, NULL, NULL, NULL);
        // printf("recv_data: 0x%x\n", recv_buffer[0]);
        // recv_buffer[0] = 0;


        usleep(1000000);
    }
}
#endif

void send_test(void)
{
	//uint8_t i=0;   
    axi16550SendNoWait(0, (uint8_t *)"Hello, World!", 16);	
}

extern int32_t axiUartRecv(uint8_t Channel,uint8_t *data,uint32_t *len);

void recv_test(void)
{
	uint8_t i=0;
    uint32_t len = 0;
    int ret = -1;
	
	//uint8_t buffer[] = {0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77};
	
	//uint8_t buffer1[] = {0x45, 0x52, 0x14, 0x13, 0x12, 0x78, 0x77,};
	
	uint8_t recv_buffer[8] = {0x00};

    //内环模式
    //axi16550SendNoWait(0, buffer, sizeof(buffer));

	ret = axi16550Recv(1, recv_buffer, &len, NULL, NULL, NULL, NULL);
	
	if(ret == 0)
	{
		printf("Received %d bytes: ", len);
		for (i = 0; i < len; i++)
		{
			printf("%02X ", recv_buffer[i]);
		}
		printf("\r\n");

		//axi16550SendNoWait(1, buffer, len);
	}
    
	
}



/* FIFO initialization function */
int axi16550FIFOInit(int port)
{
    // Enable FIFO, clear both RX and TX FIFOs  
    /*
    DMA Mode 0
    Resets XMIT FIFO.
    Resets RCVR FIFO.
    Enables FIFOs.
    */
    //userAxiCfgWrite(port, AXI_16550_FCR, 0x07);
    userAxiCfgWrite(port, AXI_16550_FCR, 0xC7);
    return 0;
}

void FPGA_Info_Read(void)
{
    uint32_t reg_value;
    reg_value = sysAxiReadLong(PL_AXI_BASE + 0x304);
    printf("FPGA Info: 0x%08X\r\n", reg_value);
}


uint32_t UART_FIFO_Info_Read(void)
{
      uint32_t reg_value = 0;
    reg_value = sysAxiReadLong(PL_AXI_BASE + 0x300);
    return reg_value;
}


void UART_FIFO_Info_Print(void)
{
    volatile uint32_t reg_value = 0;
    reg_value = sysAxiReadLong(PL_AXI_BASE + 0x300);
    printf("UART Info: 0x%08X\r\n", reg_value);
}


void UART_FIFO_RX(void)
{
    volatile uint32_t reg_value = 0;
    reg_value = sysAxiReadLong(PL_AXI_BASE + 0x30c);
    printf("RX: 0x%08X\r\n", reg_value);
}

void UART_FIFO_TX(void)
{
    volatile uint32_t reg_value = 0;
    reg_value = sysAxiReadLong(PL_AXI_BASE + 0x308);
    printf("TX: 0x%08X\r\n", reg_value);
}

// 定义位掩码
#define BIT_MASK(n) (1 << (n))

// 判断第n位是否为1
int check_bit(unsigned int num, int n)
{
    if(num & BIT_MASK(n))
    {
        return 1;
    } 
    else
    {
        return 0;
    }
}

uint32_t  UART_TX_FIFO_Info(void)
{
    volatile uint32_t reg_value = 0;
    reg_value = sysAxiReadLong(PL_AXI_BASE + 0x308);
    return reg_value;
}

/*
 DMA Modes Signaling
 Mode 0
    txrdyn:
        LOW  : no characters in the THR or Transmitter FIFO
        HIGH : the first character is loaded into the THR or FIFO.
    RXRDYn
        LOW  : at least one character in the Receiver FIFO or Receiver holding register
        HIGH : no characters in FIFO or receiver holding register.

 Mode 1
    txrdyn
        LOW  : no characters in the Transmitter FIFO
        HIGH : FIFO is completely full.
    RXRDYn
        LOW  : trigger level or the timeout has been reached
        HIGH : no characters in the FIFO
*/
uint8_t UART_TX_FIFO_Ready(uint8_t channel)
{
    volatile uint32_t reg_value = 0;
    reg_value = sysAxiReadLong(PL_AXI_BASE + 0x308);
    return check_bit(reg_value, 15 - channel);
}


void UART_LSR_Print(uint8_t channel)
{
    volatile uint32_t reg_value = 0;
    reg_value = userAxiCfgRead(channel, AXI_16550_LSR);
    printf("UART[%d] LSR: 0x%02X\r\n", channel, reg_value);
}

#define RS422

#ifdef RS422

/* LED control functions */
void txled(int i, int action)
{
	if (i < 0 || i > 15) return;

	uint32_t reg_address = SEND_LED_OFFSET + (i * 4);  
	uint32_t value = (action == 1) ? 1 : 0; 

	sysAxiWriteLong((int32_t)pl_led_base_addr + reg_address, value);


    sysAxiWriteLong((int32_t)pl_led_base_addr + IIC_REG_OFFSET, 1);

    usleep(3000);
    sysAxiWriteLong((int32_t)pl_led_base_addr + IIC_REG_OFFSET, 0);


}

void rxled(int i, int action)
{
	if (i < 0 || i > 15) return;

	uint32_t reg_address = RECEIVE_LED_OFFSET + (i * 4);  
	uint32_t value = (action == 1) ? 1 : 0;  

	sysAxiWriteLong((int32_t)pl_led_base_addr + reg_address, value);

    sysAxiWriteLong((int32_t)pl_led_base_addr + IIC_REG_OFFSET, 1);
    usleep(3000);
    sysAxiWriteLong((int32_t)pl_led_base_addr + IIC_REG_OFFSET, 0);
}


void test_rx_led(void)
{
    printf("\ntest_rx_led\n");
    
        rxled(1, 1);
        rxled(2, 1);

        sleep(1);
        rxled(1, 0);
        rxled(2, 0);

}

void test_tx_led(void)
{
    for(int i = 0; i < 16; i++)
        txled(i, 1);

}


void Portled(int i, int action)
{
	if (i < 0 || i > 15) return;

	uint32_t reg_address = LOCKED_LED_OFFSET + (i * 4);  
	uint32_t value = (action == 1) ? 1 : 0;  

	sysAxiWriteLong((int32_t)pl_led_base_addr + reg_address, value);

    sysAxiWriteLong((int32_t)pl_led_base_addr + IIC_REG_OFFSET, 1);
    usleep(3000);
    sysAxiWriteLong((int32_t)pl_led_base_addr + IIC_REG_OFFSET, 0);
}


/* 设备锁定LED control functions 
*  偏移为:0x270
*  0: LED off  1: LED on
*/

void device_locked_led(int action)
{
	//uint32_t reg_address = 0x270;  
	uint32_t value = (action == 1) ? 1 : 0;  
	sysAxiWriteLong((int32_t)pl_led_base_addr + DEVICE_LOCKED_LED_OFFSET, value);

    //printf("pl_led_base_addr + 0x130: %x\n", (int *)(pl_led_base_addr + 0x130));

    // sysAxiWriteLong(pl_led_base_addr + 0x130, value);
    // sysAxiWriteLong(pl_led_base_addr + IIC_REG_OFFSET, 1);
    // sysAxiWriteLong(pl_led_base_addr + IIC_REG_OFFSET, 0);
}

#else

/* LED control functions */
void txled(int i, int action)
{
	if (i < 0 || i > 15) return;

	uint32_t reg_address = SEND_LED_OFFSET + (i * 4);  
	uint32_t value = (action == 1) ? 1 : 0; 

	sysAxiWriteLong((int32_t)pl_led_base_addr + reg_address, value);


    sysAxiWriteLong((int32_t)pl_led_base_addr + reg_address, value);


}

void rxled(int i, int action)
{
	if (i < 0 || i > 15) return;

	uint32_t reg_address = RECEIVE_LED_OFFSET + (i * 4);  
	uint32_t value = (action == 1) ? 1 : 0;  

	sysAxiWriteLong((int32_t)pl_led_base_addr + reg_address, value);
}



void Portled(int i, int action)
{
	if (i < 0 || i > 15) return;

	uint32_t reg_address = LOCKED_LED_OFFSET + (i * 4);  
	uint32_t value = (action == 1) ? 1 : 0;  

	sysAxiWriteLong((int32_t)pl_led_base_addr + reg_address, value);
}


/* 设备锁定LED control functions 
*  偏移为:0x270
*  0: LED off  1: LED on
*/

void device_locked_led(int action)
{
	//uint32_t reg_address = 0x270;  
	uint32_t value = (action == 1) ? 1 : 0;  
	sysAxiWriteLong((int32_t)pl_led_base_addr + DEVICE_LOCKED_LED_OFFSET, value);
}




#endif
