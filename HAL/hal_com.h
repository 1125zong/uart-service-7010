#ifndef __HAL_COMMON_H_
#define __HAL_COMMON_H_

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <stdint.h>
#include <stddef.h>

// Define VxWorks-specific types for compatibility

typedef int32_t STATUS;

// Define common constants
#ifndef OK
#define OK 0
#endif

#ifndef ERROR
#define ERROR -1
#endif

#define UART_HW_FIFO_SIZE               (256)

// Function declarations
void sysAxiWriteLong(int32_t address, int32_t data);
int32_t sysAxiReadLong(int32_t address);

#endif /* __HAL_COMMON_H_ */