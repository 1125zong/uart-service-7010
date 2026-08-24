
#include "hal_com.h"

//#ifndef __linux__
// VxWorks implementation - direct memory access
void sysAxiWriteLong(int32_t address, int32_t data)
{
    *(volatile int32_t *)address = data;
    //printf("Write to address: 0x%x\n", address);
}

int32_t sysAxiReadLong(int32_t address)
{
    return *(volatile int32_t *)address;
}
//#endif
