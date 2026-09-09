#ifndef PREPROCESSING_H
#define PREPROCESSING_H

#include <stdint.h>
#include <stddef.h>

typedef struct
{
    float hpf_x;
    float hpf_y;
    float agc_gain;
} audio_processor_t;

void audio_processor_init(audio_processor_t *p);

void audio_process(
    audio_processor_t *p,
    int16_t *samples,
    size_t sample_count
);

#endif // PREPROCESSING_H