#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "driver/i2s_std.h"
//#include <frames.h>
#include <math.h>
#include "esp_dsp.h"
// #include "driver/i2s_common.h"
#include "driver/gpio.h"
#include "inmp441.h"
#include "esp_log.h"
#include <inttypes.h>

RingbufHandle_t buf_handle;
i2s_chan_handle_t rx_handle;
// RingbufHandle_t buf_handle;
i2s_chan_config_t chan_conf = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_AUTO, I2S_ROLE_MASTER);
int32_t buff[256];
size_t bytes_to_read = sizeof(buff);
size_t bytes_read;
#define N 256
float window[N];
float power_spectrum[N];
size_t num_samples;
#define FRAME_BYTES (N * sizeof(int32_t))
void inmp_init()
{
    i2s_new_channel(&chan_conf, NULL, &rx_handle);
    i2s_std_config_t std_conf = {
        .clk_cfg = {
            .sample_rate_hz = 16000,
            .clk_src = I2S_CLK_SRC_DEFAULT,
            .mclk_multiple = I2S_MCLK_MULTIPLE_384,
        },
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_24BIT, I2S_SLOT_MODE_MONO), // initializes slot mask to left
        .gpio_cfg = {
            .mclk = I2S_GPIO_UNUSED,
            .bclk = GPIO_NUM_26, // 26,25,33
            .ws = GPIO_NUM_25,
            .din = GPIO_NUM_33,
            .invert_flags = {
                .mclk_inv = false,
                .bclk_inv = false,
                .ws_inv = false,
            },
        },

    };
    // std_conf.slot_cfg.slot_mask = I2S_STD_SLOT_LEFT;
    i2s_channel_init_std_mode(rx_handle, &std_conf);
    i2s_channel_enable(rx_handle);
    ESP_LOGI("INMP", "I2S initialized successfully");

    esp_err_t err = dsps_fft2r_init_fc32(NULL, CONFIG_DSP_MAX_FFT_SIZE);
    if (err == ESP_OK)
        dsps_wind_hann_f32(window, N);
    else
        ESP_LOGE("DSP_INIT", "ERR IN DSPFFT INIT");

    buf_handle = xRingbufferCreate(4096, RINGBUF_TYPE_BYTEBUF);
}
void show_output(void *pvParams)
{
    while (1)
    {
        esp_err_t ret = i2s_channel_read(rx_handle, buff, sizeof(buff), &bytes_read, portMAX_DELAY);

        if (ret == ESP_OK)
        {
            // int32_t max_sample = 0;

            num_samples = bytes_read / sizeof(buff[0]);

            for (size_t i = 0; i < num_samples; i++)
            {
                int32_t value = buff[i] >> 16;
                UBaseType_t res = xRingbufferSend(buf_handle, &value, sizeof(value), pdMS_TO_TICKS(1000));
                if (res != pdTRUE)
                {
                    printf("Failed to send item\n");
                }

                //         if (value < 0)
                //             value = -value; // takes absolute value so converts negative to positive

                //         if (value > max_sample)
                //             max_sample = value;
                //     }

                //     printf(
                //         "bytes=%zu samples=%zu max=%ld\n",
                //         bytes_read,
                //         num_samples,
                //         (long)max_sample);

                //     // for (int i = 0; i < 20; i++)
                //     // {
                //     //     printf(
                //     //         "raw signed = %" PRId32
                //     //         " | raw hex = 0x%08" PRIx32
                //     //         "\n",
                //     //         buff[i],
                //     //         (uint32_t)buff[i]);
                //     // }
                // }
                // else
                // {
                //     printf("Macha board connect madu");
                // }
            }
        }
    }
}
