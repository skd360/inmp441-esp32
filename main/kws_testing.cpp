#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/ringbuf.h"

#include "driver/i2s_std.h"
#include "driver/gpio.h"

#include "esp_log.h"
#include "esp_system.h"
#include "esp_heap_caps.h"
#include "esp_dsp.h"

#include "tensorflow/lite/micro/micro_interpreter.h"
#include "tensorflow/lite/micro/micro_mutable_op_resolver.h"
#include "tensorflow/lite/schema/schema_generated.h"

/* ---------- Embedded TFLite model ---------- */
extern "C" {
extern const uint8_t _binary_marvin_kws_int8_tflite_start[];
extern const uint8_t _binary_marvin_kws_int8_tflite_end[];
}

/* ---------- Audio / model constants ---------- */
#define SAMPLE_RATE          16000
#define AUDIO_SAMPLES        16000
#define I2S_CHUNK_SAMPLES    256

#define N_FFT                512
#define HOP_LENGTH           160
#define N_MELS               40
#define N_FRAMES             101

#define FMIN_HZ              20.0f
#define FMAX_HZ              7600.0f

#define INPUT_SCALE          0.0048678116872906685f
#define INPUT_ZERO_POINT     (-114)

#define MARVIN_CLASS_INDEX   1
#define MARVIN_THRESHOLD     0.95f

#define LED_GPIO             GPIO_NUM_2
#define TENSOR_ARENA_SIZE    (120 * 1024)

static constexpr float PI_F = 3.14159265358979323846f;

/* ---------- I2S ---------- */
static i2s_chan_handle_t rx_handle = NULL;
static i2s_chan_config_t chan_conf =
    I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_AUTO, I2S_ROLE_MASTER);

static int32_t i2s_raw[I2S_CHUNK_SAMPLES];
static int16_t pcm_chunk[I2S_CHUNK_SAMPLES];
static size_t bytes_read = 0;

static RingbufHandle_t buf_handle = NULL;

/* ---------- KWS DSP buffers ---------- */
static int16_t audio_buffer[AUDIO_SAMPLES];
static float fft_buffer[N_FFT * 2];
static float power_spectrum[(N_FFT / 2) + 1];
static float mel_spectrogram[N_MELS][N_FRAMES];
static float hann_window[N_FFT];

/* ---------- TFLite Micro ---------- */
static uint8_t tensor_arena[TENSOR_ARENA_SIZE];

static tflite::MicroInterpreter *interpreter = nullptr;
static TfLiteTensor *input_tensor = nullptr;
static TfLiteTensor *output_tensor = nullptr;

/* =========================================================
   DSP
   ========================================================= */

static void init_hann_window(void)
{
    /* librosa/scipy periodic Hann: fftbins=True */
    for (int n = 0; n < N_FFT; n++)
    {
        hann_window[n] =
            0.5f - 0.5f * cosf((2.0f * PI_F * n) / N_FFT);
    }
}

static float hz_to_mel(float hz)
{
    const float f_sp = 200.0f / 3.0f;
    float mel = hz / f_sp;

    const float min_log_hz = 1000.0f;
    const float min_log_mel = min_log_hz / f_sp;
    const float logstep = logf(6.4f) / 27.0f;

    if (hz >= min_log_hz)
        mel = min_log_mel + logf(hz / min_log_hz) / logstep;

    return mel;
}

static float mel_to_hz(float mel)
{
    const float f_sp = 200.0f / 3.0f;

    const float min_log_hz = 1000.0f;
    const float min_log_mel = min_log_hz / f_sp;
    const float logstep = logf(6.4f) / 27.0f;

    if (mel >= min_log_mel)
        return min_log_hz * expf(logstep * (mel - min_log_mel));

    return f_sp * mel;
}

static void apply_mel_filterbank(const float *power, float *mel_output)
{
    float mel_min = hz_to_mel(FMIN_HZ);
    float mel_max = hz_to_mel(FMAX_HZ);

    float mel_points[N_MELS + 2];
    float hz_points[N_MELS + 2];

    for (int i = 0; i < N_MELS + 2; i++)
    {
        mel_points[i] =
            mel_min +
            (mel_max - mel_min) * ((float)i / (N_MELS + 1));

        hz_points[i] = mel_to_hz(mel_points[i]);
    }

    for (int m = 0; m < N_MELS; m++)
    {
        float left   = hz_points[m];
        float center = hz_points[m + 1];
        float right  = hz_points[m + 2];

        float mel_energy = 0.0f;

        /* librosa default norm='slaney' */
        float enorm = 2.0f / (right - left);

        for (int k = 0; k <= N_FFT / 2; k++)
        {
            float freq = ((float)k * SAMPLE_RATE) / N_FFT;
            float weight = 0.0f;

            if (freq >= left && freq <= center)
            {
                weight = (freq - left) / (center - left);
            }
            else if (freq > center && freq <= right)
            {
                weight = (right - freq) / (right - center);
            }

            mel_energy += power[k] * weight * enorm;
        }

        mel_output[m] = mel_energy;
    }
}

static void create_mel_spectrogram(const int16_t *audio)
{
    /*
       Matches:
       n_fft=512
       hop_length=160
       center=True
       pad_mode="constant"
       power=2.0

       16000 samples -> 101 frames.
    */
    for (int frame = 0; frame < N_FRAMES; frame++)
    {
        int start = frame * HOP_LENGTH - (N_FFT / 2);

        for (int n = 0; n < N_FFT; n++)
        {
            int sample_index = start + n;
            float sample = 0.0f;

            if (sample_index >= 0 && sample_index < AUDIO_SAMPLES)
                sample = (float)audio[sample_index] / 32768.0f;

            sample *= hann_window[n];

            fft_buffer[2 * n]     = sample;
            fft_buffer[2 * n + 1] = 0.0f;
        }

        dsps_fft2r_fc32(fft_buffer, N_FFT);
        dsps_bit_rev_fc32(fft_buffer, N_FFT);

        for (int k = 0; k <= N_FFT / 2; k++)
        {
            float re = fft_buffer[2 * k];
            float im = fft_buffer[2 * k + 1];

            /* power = |STFT|^2 */
            power_spectrum[k] = re * re + im * im;
        }

        float mel_frame[N_MELS];
        apply_mel_filterbank(power_spectrum, mel_frame);

        for (int m = 0; m < N_MELS; m++)
            mel_spectrogram[m][frame] = mel_frame[m];
    }
}

static void normalize_and_quantize_log_mel(int8_t *model_input)
{
    /*
       Matches:
       librosa.power_to_db(mel, ref=np.max)
       log_mel = (log_mel + 80) / 80
       clip [0, 1]
       INT8: q = round(x / scale) + zero_point
    */

    float max_power = 1e-10f;

    for (int m = 0; m < N_MELS; m++)
    {
        for (int t = 0; t < N_FRAMES; t++)
        {
            if (mel_spectrogram[m][t] > max_power)
                max_power = mel_spectrogram[m][t];
        }
    }

    const float ref_db = 10.0f * log10f(max_power);
    int out_index = 0;

    for (int m = 0; m < N_MELS; m++)
    {
        for (int t = 0; t < N_FRAMES; t++)
        {
            float p = mel_spectrogram[m][t];

            if (p < 1e-10f)
                p = 1e-10f;

            float db = 10.0f * log10f(p) - ref_db;

            /* librosa power_to_db default top_db=80 */
            if (db < -80.0f)
                db = -80.0f;

            float normalized = (db + 80.0f) / 80.0f;

            if (normalized < 0.0f) normalized = 0.0f;
            if (normalized > 1.0f) normalized = 1.0f;

            int32_t q =
                (int32_t)roundf(normalized / INPUT_SCALE) +
                INPUT_ZERO_POINT;

            if (q < -128) q = -128;
            if (q > 127)  q = 127;

            model_input[out_index++] = (int8_t)q;
        }
    }
}

/* =========================================================
   TFLite Micro
   ========================================================= */

static bool init_tflite(void)
{
    const tflite::Model *model =
        tflite::GetModel(_binary_marvin_kws_int8_tflite_start);

    if (model->version() != TFLITE_SCHEMA_VERSION)
    {
        printf("ERROR: TFLite schema mismatch: model=%ld runtime=%d\n",
               (long)model->version(), TFLITE_SCHEMA_VERSION);
        return false;
    }

    /*
       The model contains:
       Conv2D, MaxPool2D, Mean (global average pool),
       FullyConnected.
       ReLU is fused into layers.
    */
    static tflite::MicroMutableOpResolver<4> resolver;

    if (resolver.AddConv2D() != kTfLiteOk ||
        resolver.AddMaxPool2D() != kTfLiteOk ||
        resolver.AddMean() != kTfLiteOk ||
        resolver.AddFullyConnected() != kTfLiteOk)
    {
        printf("ERROR: Failed to register TFLM operators\n");
        return false;
    }

    static tflite::MicroInterpreter static_interpreter(
        model,
        resolver,
        tensor_arena,
        TENSOR_ARENA_SIZE);

    interpreter = &static_interpreter;

    if (interpreter->AllocateTensors() != kTfLiteOk)
    {
        printf("ERROR: AllocateTensors failed. Increase tensor arena if heap permits.\n");
        return false;
    }

    input_tensor  = interpreter->input(0);
    output_tensor = interpreter->output(0);

    if (input_tensor == nullptr || output_tensor == nullptr)
    {
        printf("ERROR: Null model tensor\n");
        return false;
    }

    printf("Input dims:");
    for (int i = 0; i < input_tensor->dims->size; i++)
        printf(" %d", input_tensor->dims->data[i]);
    printf("\n");

    printf("Input type=%d scale=%.10f zero=%ld\n",
           input_tensor->type,
           (double)input_tensor->params.scale,
           (long)input_tensor->params.zero_point);

    printf("Output dims:");
    for (int i = 0; i < output_tensor->dims->size; i++)
        printf(" %d", output_tensor->dims->data[i]);
    printf("\n");

    printf("Output type=%d scale=%.10f zero=%ld\n",
           output_tensor->type,
           (double)output_tensor->params.scale,
           (long)output_tensor->params.zero_point);

    if (input_tensor->type != kTfLiteInt8 ||
        input_tensor->bytes != N_MELS * N_FRAMES * sizeof(int8_t))
    {
        printf("ERROR: Model input is not expected INT8 1x40x101x1 tensor\n");
        return false;
    }

    return true;
}

static float get_marvin_confidence(void)
{
    /*
       The provided model has no Softmax operator at its output,
       so dequantize its logits and apply softmax here.
    */
    if (output_tensor == nullptr || output_tensor->type != kTfLiteInt8)
        return 0.0f;

    int output_count = output_tensor->bytes / sizeof(int8_t);

    if (output_count <= MARVIN_CLASS_INDEX || output_count > 8)
        return 0.0f;

    float logits[8];

    for (int i = 0; i < output_count; i++)
    {
        logits[i] =
            ((int32_t)output_tensor->data.int8[i] -
             output_tensor->params.zero_point) *
            output_tensor->params.scale;
    }

    float max_logit = logits[0];

    for (int i = 1; i < output_count; i++)
    {
        if (logits[i] > max_logit)
            max_logit = logits[i];
    }

    float sum = 0.0f;

    for (int i = 0; i < output_count; i++)
    {
        logits[i] = expf(logits[i] - max_logit);
        sum += logits[i];
    }

    if (sum <= 0.0f)
        return 0.0f;

    return logits[MARVIN_CLASS_INDEX] / sum;
}

/* =========================================================
   LED
   ========================================================= */

static void init_led(void)
{
    gpio_reset_pin(LED_GPIO);
    gpio_set_direction(LED_GPIO, GPIO_MODE_OUTPUT);
    gpio_set_level(LED_GPIO, 0);
}

static void marvin_led(void)
{
    gpio_set_level(LED_GPIO, 1);
    vTaskDelay(pdMS_TO_TICKS(2000));
    gpio_set_level(LED_GPIO, 0);
}

/* =========================================================
   INMP441 / I2S
   ========================================================= */

static void inmp_init(void)
{
    ESP_ERROR_CHECK(i2s_new_channel(&chan_conf, NULL, &rx_handle));

    i2s_std_config_t std_conf = {
        .clk_cfg = {
            .sample_rate_hz = SAMPLE_RATE,
            .clk_src = I2S_CLK_SRC_DEFAULT,
            .mclk_multiple = I2S_MCLK_MULTIPLE_384,
        },
        .slot_cfg =
            I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(
                I2S_DATA_BIT_WIDTH_24BIT,
                I2S_SLOT_MODE_MONO),
        .gpio_cfg = {
            .mclk = I2S_GPIO_UNUSED,
            .bclk = GPIO_NUM_26,
            .ws   = GPIO_NUM_25,
            .dout = I2S_GPIO_UNUSED,
            .din  = GPIO_NUM_33,
            .invert_flags = {
                .mclk_inv = false,
                .bclk_inv = false,
                .ws_inv   = false,
            },
        },
    };

    /* INMP441 L/R pin must match this slot. Uncomment if required. */
    /* std_conf.slot_cfg.slot_mask = I2S_STD_SLOT_LEFT; */

    ESP_ERROR_CHECK(i2s_channel_init_std_mode(rx_handle, &std_conf));
    ESP_ERROR_CHECK(i2s_channel_enable(rx_handle));

    ESP_LOGI("INMP", "I2S initialized: 16 kHz mono");

    esp_err_t fft_err =
        dsps_fft2r_init_fc32(NULL, CONFIG_DSP_MAX_FFT_SIZE);

    if (fft_err != ESP_OK)
    {
        ESP_LOGE("DSP", "FFT init failed: %s", esp_err_to_name(fft_err));
        abort();
    }

    /*
       Store INT16 PCM in the ring buffer.
       8192 bytes = 4096 samples = 256 ms at 16 kHz.
    */
    buf_handle = xRingbufferCreate(8192, RINGBUF_TYPE_BYTEBUF);

    if (buf_handle == NULL)
    {
        ESP_LOGE("INMP", "Failed to create audio ring buffer");
        abort();
    }
}

static void show_output(void *pvParams)
{
    (void)pvParams;

    while (1)
    {
        esp_err_t ret =
            i2s_channel_read(
                rx_handle,
                i2s_raw,
                sizeof(i2s_raw),
                &bytes_read,
                portMAX_DELAY);

        if (ret != ESP_OK)
            continue;

        size_t sample_count = bytes_read / sizeof(i2s_raw[0]);

        /*
           INMP441 24-bit samples are carried in a 32-bit word.
           Keep the upper signed 16 bits for the KWS PCM stream.
        */
        for (size_t i = 0; i < sample_count; i++)
            pcm_chunk[i] = (int16_t)(i2s_raw[i] >> 16);

        /*
           IMPORTANT:
           Send one complete 256-sample block instead of calling
           xRingbufferSend() once for every individual sample.
        */
        BaseType_t ok =
            xRingbufferSend(
                buf_handle,
                pcm_chunk,
                sample_count * sizeof(int16_t),
                pdMS_TO_TICKS(100));

        if (ok != pdTRUE)
            ESP_LOGW("AUDIO", "Ring buffer full; audio block dropped");
    }
}

/* =========================================================
   KWS
   ========================================================= */

static void kws_test_task(void *arg)
{
    (void)arg;

    init_hann_window();
    init_led();

    if (!init_tflite())
    {
        printf("KWS initialization failed\n");
        vTaskDelete(NULL);
        return;
    }

    printf("MARVIN KWS ready\n");

    size_t collected = 0;

    while (1)
    {
        size_t item_size = 0;

        int16_t *item =
            (int16_t *)xRingbufferReceiveUpTo(
                buf_handle,
                &item_size,
                pdMS_TO_TICKS(1000),
                I2S_CHUNK_SAMPLES * sizeof(int16_t));

        if (item == NULL)
            continue;

        size_t received = item_size / sizeof(int16_t);
        size_t offset = 0;

        /*
           Do not silently throw away samples if a ring-buffer item
           crosses the 16000-sample boundary.
        */
        while (offset < received)
        {
            size_t room = AUDIO_SAMPLES - collected;
            size_t copy_count = received - offset;

            if (copy_count > room)
                copy_count = room;

            memcpy(
                &audio_buffer[collected],
                &item[offset],
                copy_count * sizeof(int16_t));

            collected += copy_count;
            offset += copy_count;

            if (collected == AUDIO_SAMPLES)
            {
                create_mel_spectrogram(audio_buffer);
                normalize_and_quantize_log_mel(input_tensor->data.int8);

                if (interpreter->Invoke() != kTfLiteOk)
                {
                    printf("ERROR: TFLite inference failed\n");
                    collected = 0;
                    continue;
                }

                float confidence = get_marvin_confidence();

                printf("MARVIN confidence: %.2f%%\n",
                       confidence * 100.0f);

                if (confidence >= MARVIN_THRESHOLD)
                {
                    printf("*** MARVIN DETECTED ***\n");
                    marvin_led();
                }

                collected = 0;
            }
        }

        vRingbufferReturnItem(buf_handle, item);
    }
}

/* =========================================================
   CPU / RAM monitor
   ========================================================= */

static void resource_monitor_task(void *arg)
{
    (void)arg;

    uint32_t prev_total = 0;
    uint32_t prev_idle = 0;

    while (1)
    {
        size_t free_heap = esp_get_free_heap_size();
        size_t total_heap = heap_caps_get_total_size(MALLOC_CAP_8BIT);

        float ram_free =
            ((float)free_heap / (float)total_heap) * 100.0f;

        float ram_used = 100.0f - ram_free;

        UBaseType_t task_count = uxTaskGetNumberOfTasks();
        TaskStatus_t *tasks =
            (TaskStatus_t *)malloc(task_count * sizeof(TaskStatus_t));

        if (tasks != NULL)
        {
            uint32_t total_runtime = 0;
            uint32_t idle_runtime = 0;

            task_count =
                uxTaskGetSystemState(
                    tasks,
                    task_count,
                    &total_runtime);

            for (UBaseType_t i = 0; i < task_count; i++)
            {
                if (strncmp(tasks[i].pcTaskName, "IDLE", 4) == 0)
                    idle_runtime += tasks[i].ulRunTimeCounter;
            }

            uint32_t total_delta = total_runtime - prev_total;
            uint32_t idle_delta  = idle_runtime - prev_idle;

            if (total_delta != 0)
            {
                float cpu_usage =
                    100.0f -
                    (((float)idle_delta /
                      ((float)total_delta * portNUM_PROCESSORS)) *
                     100.0f);

                printf("CPU: %.2f%% | RAM Used: %.2f%% | RAM Free: %.2f%%\n",
                       cpu_usage, ram_used, ram_free);
            }

            prev_total = total_runtime;
            prev_idle = idle_runtime;

            free(tasks);
        }

        vTaskDelay(pdMS_TO_TICKS(5000));
    }
}

/* =========================================================
   ESP-IDF entry point
   ========================================================= */

extern "C" void app_main(void)
{
    inmp_init();

    xTaskCreate(
        show_output,
        "audio_capture",
        4096,
        NULL,
        5,
        NULL);

    xTaskCreate(
        kws_test_task,
        "kws_test",
        8192,
        NULL,
        5,
        NULL);

    xTaskCreate(
        resource_monitor_task,
        "resource_monitor",
        3072,
        NULL,
        1,
        NULL);
}
