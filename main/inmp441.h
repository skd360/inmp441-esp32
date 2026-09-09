#ifndef INMP441_H

#define INMP441_H 
#include "freertos/ringbuf.h"
void inmp_init();
void show_output(void *pvParams);
extern RingbufHandle_t buf_handle;
#endif