#include "preprocessing.h"

#define HPF_COEFF 0.98f

#define AGC_TARGET_PEAK 16000.0f
#define AGC_SILENCE_PEAK 500

#define AGC_MIN_GAIN 0.5f
#define AGC_MAX_GAIN 4.0f

#define AGC_GAIN_DOWN_SPEED 0.5f
#define AGC_GAIN_UP_SPEED 0.05f

#define OUTPUT_BOOST 1.0f

void audio_processor_init(audio_processor_t *p)
{
    p->hpf_x = 0.0f;
    p->hpf_y = 0.0f;
    p->agc_gain = 1.0f;
}

void audio_process(audio_processor_t *p,int16_t *samples,size_t sample_count)
{
    int32_t peak = 0;

    // ------------------------------------------------------------
    // 1. High-pass filter + peak detection
    // ------------------------------------------------------------

    for (size_t i = 0; i < sample_count; i++)
    {
        float x = (float)samples[i];

        float y =
            x - p->hpf_x + HPF_COEFF * p->hpf_y;

        p->hpf_x = x;
        p->hpf_y = y;

        // Clamp filtered sample to int16 range
        if (y > 32767.0f)
            y = 32767.0f;

        if (y < -32768.0f)
            y = -32768.0f;

        samples[i] = (int16_t)y;

        int32_t abs_value =
            (y < 0) ? -(int32_t)y : (int32_t)y;

        if (abs_value > peak)
            peak = abs_value;
    }

    // ------------------------------------------------------------
    // 2. Automatic Gain Control
    // ------------------------------------------------------------

    if (peak > AGC_SILENCE_PEAK)
    {
        float desired_gain =
            AGC_TARGET_PEAK / (float)peak;

        if (desired_gain < AGC_MIN_GAIN)
            desired_gain = AGC_MIN_GAIN;

        if (desired_gain > AGC_MAX_GAIN)
            desired_gain = AGC_MAX_GAIN;

        // Signal became louder -> reduce gain quickly
        if (desired_gain < p->agc_gain)
        {
            p->agc_gain =
                p->agc_gain * (1.0f - AGC_GAIN_DOWN_SPEED) + desired_gain * AGC_GAIN_DOWN_SPEED;
        }

        // Signal became quieter -> increase gain slowly
        else
        {
            p->agc_gain =
                p->agc_gain * (1.0f - AGC_GAIN_UP_SPEED) + desired_gain * AGC_GAIN_UP_SPEED;
        }
    }

    float gain = p->agc_gain * OUTPUT_BOOST;

    // ------------------------------------------------------------
    // 3. Apply gain + soft limiter
    // ------------------------------------------------------------

    for (size_t i = 0; i < sample_count; i++)
    {
        float v = (float)samples[i] * gain;

        // Soft knee
        if (v > 30000.0f)
        {
            v = 30000.0f +
                (v - 30000.0f) * 0.2f;
        }

        else if (v < -30000.0f)
        {
            v = -30000.0f +
                (v + 30000.0f) * 0.2f;
        }

        // Final int16 protection
        if (v > 32767.0f)
            v = 32767.0f;

        if (v < -32768.0f)
            v = -32768.0f;

        samples[i] = (int16_t)v;
    }
}