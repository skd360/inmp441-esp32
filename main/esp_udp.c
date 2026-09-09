#include <stdio.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "inmp441.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "nvs_flash.h"

#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include "preprocessing.h"
#define WIFI_SSID "ESP32_MIC"
#define WIFI_PASSWORD "12345678"
#define MAX_CONNECTIONS 1
static audio_processor_t audio_processor;
#define LAPTOP_IP "192.168.4.2"
#define UDP_PORT 4210
int16_t pcm_buffer[256];
static const char *TAG = "WIFI_AP";
size_t max_samples = sizeof(pcm_buffer) / sizeof(pcm_buffer[0]);
static void wifi_init_softap(void)
{
    ESP_ERROR_CHECK(esp_netif_init());

    ESP_ERROR_CHECK(esp_event_loop_create_default());

    esp_netif_create_default_wifi_ap();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();

    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    wifi_config_t wifi_config = {
        .ap = {
            .ssid = WIFI_SSID,
            .ssid_len = strlen(WIFI_SSID),
            .channel = 1,
            .password = WIFI_PASSWORD,
            .max_connection = MAX_CONNECTIONS,
            .authmode = WIFI_AUTH_WPA2_PSK,
        },
    };

    ESP_ERROR_CHECK(
        esp_wifi_set_mode(WIFI_MODE_AP));

    ESP_ERROR_CHECK(
        esp_wifi_set_config(WIFI_IF_AP, &wifi_config));

    ESP_ERROR_CHECK(
        esp_wifi_start());

    ESP_LOGI(TAG, "Wi-Fi AP started");
    ESP_LOGI(TAG, "SSID: %s", WIFI_SSID);
    ESP_LOGI(TAG, "ESP32 IP: 192.168.4.1");
}

static void udp_task(void *arg)
{
    size_t item_size;

    // Create UDP socket
    int sock = socket(
        AF_INET,
        SOCK_DGRAM,
        IPPROTO_IP);

    if (sock < 0)
    {
        ESP_LOGE(TAG, "Failed to create UDP socket");
        vTaskDelete(NULL);
        return;
    }

    ESP_LOGI(TAG, "UDP socket created");

    // Destination address
    struct sockaddr_in dest_addr = {
        .sin_family = AF_INET,
        .sin_port = htons(UDP_PORT),
        .sin_addr.s_addr = inet_addr(LAPTOP_IP),
    };
    audio_processor_init(&audio_processor);

    while (1)
    {
        int32_t *item = (int32_t *)xRingbufferReceiveUpTo(buf_handle, &item_size, pdMS_TO_TICKS(1000), 1024);
        if (item != NULL)
        {
            // Print item
            size_t num_samples_received = item_size / sizeof(int32_t);

            if (num_samples_received > max_samples)
                num_samples_received = max_samples;
            for (int i = 0; i < num_samples_received; i++)
            {
                pcm_buffer[i] = item[i];
            }
            vRingbufferReturnItem(buf_handle, item);

            // const char *message = "Hello from ESP32!";
            audio_process(&audio_processor,pcm_buffer,num_samples_received);

            int err = sendto(
                sock,
                pcm_buffer,
                sizeof(pcm_buffer),
                0,
                (struct sockaddr *)&dest_addr,
                sizeof(dest_addr));

            if (err < 0)
            {
                ESP_LOGE(TAG, "UDP send failed");
                vTaskDelay(
                    pdMS_TO_TICKS(1000));
            }
            // else
            // {
            //     ESP_LOGI(
            //         TAG,
            //         "UDP packet sent: %s",
            //         message);
            // }

            // vTaskDelay(
            //     pdMS_TO_TICKS(1000));
        }
    }
}

void app_main(void)
{
    inmp_init();
    ESP_ERROR_CHECK(
        nvs_flash_init());

    // Start Wi-Fi AP
    wifi_init_softap();

    xTaskCreate(
        show_output,
        "op_task",
        4096,
        NULL,
        5,
        NULL);
    // Start UDP task
    xTaskCreate(
        udp_task,
        "udp_task",
        4096,
        NULL,
        5,
        NULL);
}