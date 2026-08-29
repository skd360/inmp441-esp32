# INMP441 + ESP32

Interfacing the INMP441 I2S MEMS microphone with an ESP32 using ESP-IDF.

## Hardware

- ESP32
- INMP441 I2S MEMS microphone

## Goal

Capture PCM audio samples from the INMP441 using the ESP32's I2S peripheral.

Later this project will be extended toward:

- Audio preprocessing
- Mel filterbank / MFCC extraction
- Keyword spotting
- Sending audio/features to a speech server

## Development Environment

- ESP-IDF
- C
- ESP32