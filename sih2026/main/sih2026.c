#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "driver/i2s_std.h"
#include <inmp441.h>
// #include "driver/i2s_common.h"
#include "driver/gpio.h"
#include <inttypes.h>
i2s_chan_handle_t rx_handle;

i2s_chan_config_t chan_conf = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_AUTO, I2S_ROLE_MASTER);
int32_t buff[1024];
size_t bytes_to_read = sizeof(buff);
size_t bytes_read;

void app_main(void)
{
    inmp_init();
    // i2s_channel_read(rx_handle, &buff, bytes_to_read, &bytes_read, portMAX_DELAY);
    xTaskCreate(show_output, "inmp_op", 4096, NULL, 4, NULL);
    printf("gandu");

}