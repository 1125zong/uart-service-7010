#include <string.h>
#include "hal_ringbuffer.h"



/**
 * Returns whether a ring buffer is empty.
 * @param buffer The buffer for which it should be returned whether it is empty.
 * @return 1 if empty; 0 otherwise.
 */
uint8_t ring_buffer_is_empty(ring_buffer_t *buffer)
{
    return (buffer->head_index == buffer->tail_index);
}

/**
 * Returns whether a ring buffer is full.
 * @param buffer The buffer for which it should be returned whether it is full.
 * @return 1 if full; 0 otherwise.
 */
 uint8_t ring_buffer_is_full(ring_buffer_t *buffer)
{
    return ((buffer->head_index - buffer->tail_index) & RING_BUFFER_MASK(buffer)) == RING_BUFFER_MASK(buffer);
}

/**
 * Returns the number of items in a ring buffer.
 * @param buffer The buffer for which the number of items should be returned.
 * @return The number of items in the ring buffer.
 */
 ring_buffer_size_t ring_buffer_num_items(ring_buffer_t *buffer)
{
    return ((buffer->head_index - buffer->tail_index) & RING_BUFFER_MASK(buffer));
}



/**
 * @file
 * Implementation of ring buffer functions.
 */

void ring_buffer_init(ring_buffer_t *buffer, unsigned char *buf, size_t buf_size)
{
    RING_BUFFER_ASSERT(RING_BUFFER_IS_POWER_OF_TWO(buf_size) == 1);
    buffer->buffer = buf;
    buffer->buffer_mask = buf_size - 1;
    buffer->tail_index = 0;
    buffer->head_index = 0;
}

void ring_buffer_reset(ring_buffer_t *buffer)
{
    buffer->tail_index = 0;
    buffer->head_index = 0;
}

void ring_buffer_queue(ring_buffer_t *buffer, char data)
{
    /* Is buffer full? */
    if (ring_buffer_is_full(buffer))
    {
        /* Is going to overwrite the oldest byte */
        /* Increase tail index */
        buffer->tail_index = ((buffer->tail_index + 1) & RING_BUFFER_MASK(buffer));
    }

    /* Place data in buffer */
    buffer->buffer[buffer->head_index] = data;
    buffer->head_index = ((buffer->head_index + 1) & RING_BUFFER_MASK(buffer));
}

#if 0
void ring_buffer_queue_arr(ring_buffer_t *buffer, const char *data, ring_buffer_size_t size)
{
    /* Add bytes; one by one */
    ring_buffer_size_t i;
    for (i = 0; i < size; i++)
    {
        ring_buffer_queue(buffer, data[i]);
    }
}
#else	
void ring_buffer_queue_arr(ring_buffer_t *buffer, const char *data, ring_buffer_size_t size)
{
    ring_buffer_size_t head = buffer->head_index;
    ring_buffer_size_t mask = buffer->buffer_mask;
    ring_buffer_size_t available = mask - ((head - buffer->tail_index) & mask);
    
    if (size > available) {
        // 处理溢出
        buffer->tail_index = (buffer->tail_index + (size - available)) & mask;
    }
    
    // 批量拷贝处理非跨越情况
    if ((head + size) <= mask + 1) {
        memcpy(&buffer->buffer[head], data, size);
    } else {
        // 处理跨越情况
        ring_buffer_size_t first_part = mask + 1 - head;
        memcpy(&buffer->buffer[head], data, first_part);
        memcpy(buffer->buffer, data + first_part, size - first_part);
    }
    
    buffer->head_index = (head + size) & mask;
}
#endif


uint8_t ring_buffer_dequeue(ring_buffer_t *buffer, char *data)
{
    if (ring_buffer_is_empty(buffer))
    {
        /* No items */
        return 0;
    }

    *data = buffer->buffer[buffer->tail_index];
    buffer->tail_index = ((buffer->tail_index + 1) & RING_BUFFER_MASK(buffer));
    return 1;
}



ring_buffer_size_t ring_buffer_dequeue_arr(ring_buffer_t *buffer, char *data, ring_buffer_size_t len)
{
    if (ring_buffer_is_empty(buffer))
    {
        /* No items */
        return 0;
    }

    char *data_ptr = data;
    ring_buffer_size_t cnt = 0;
    while ((cnt < len) && ring_buffer_dequeue(buffer, data_ptr))
    {
        cnt++;
        data_ptr++;
    }
    return cnt;
}


ring_buffer_size_t ring_buffer_peek_arr(ring_buffer_t *buffer, char *data, ring_buffer_size_t len)
{
    ring_buffer_size_t available;
    ring_buffer_size_t tail;
    ring_buffer_size_t capacity;
    ring_buffer_size_t first_part;

    available = ring_buffer_num_items(buffer);

    if (len > available)
    {
        len = available;
    }

    if (len == 0)
    {
        return 0;
    }

    tail = buffer->tail_index;
    capacity = buffer->buffer_mask + 1;

    first_part = capacity - tail;

    if (first_part > len)
    {
        first_part = len;
    }

    memcpy(data, &buffer->buffer[tail], first_part);

    if (len > first_part)
    {
        memcpy(data + first_part, buffer->buffer, len - first_part);
    }

    return len;
}

ring_buffer_size_t ring_buffer_discard( ring_buffer_t *buffer, ring_buffer_size_t len)
{
    ring_buffer_size_t available;

    available = ring_buffer_num_items(buffer);

    if (len > available)
    {
        len = available;
    }

    buffer->tail_index = (buffer->tail_index + len) & RING_BUFFER_MASK(buffer);

    return len;
}


/**/

uint8_t ring_buffer_peek(ring_buffer_t *buffer, char *data, ring_buffer_size_t index)
{
    if (index >= ring_buffer_num_items(buffer))
    {
        /* No items at index */
        return 0;
    }

    /* Add index to pointer */
    ring_buffer_size_t data_index = ((buffer->tail_index + index) & RING_BUFFER_MASK(buffer));
    *data = buffer->buffer[data_index];
    return 1;
}
