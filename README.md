# INMP441 MEMS Microphone Interfacing with ESP32

**Real-time audio acquisition, preprocessing, and UDP streaming using ESP-IDF**

This project interfaces an **INMP441 digital MEMS microphone** with an **ESP32** using I²S. The ESP32 captures audio samples, passes them between FreeRTOS tasks through a ring buffer, applies basic audio preprocessing, and streams the resulting PCM audio over UDP to a Python receiver for playback.

## Project image

<!-- ADD PROJECT PHOTO HERE: upload a photograph of your ESP32 + INMP441 setup to images/setup.jpg. -->
![ESP32 and INMP441 microphone setup](images/setup.jpg)

> **Image placeholder:** Create an `images` folder in the repository and upload your hardware photograph as `setup.jpg`. You can change the path or caption above to match your image.

## How it works

```text
INMP441 microphone
       |
       v
ESP32 I2S capture (16 kHz, mono)
       |
       v
FreeRTOS ring buffer
       |
       v
High-pass filter -> Automatic Gain Control -> Output limiting
       |
       v
ESP32 Wi-Fi access point -> UDP packets (port 4210)
       |
       v
Python receiver -> Audio playback
```

## Key features

- **I²S microphone interfacing:** Captures audio from the INMP441 at **16 kHz**, configured for mono reception and 24-bit I²S slots.
- **FreeRTOS task separation:** Acquires audio and transfers samples to the UDP transmission task using a ring buffer.
- **Audio preprocessing:** Implements a high-pass filter, peak-based automatic gain control (AGC), and output limiting.
- **Wireless audio streaming:** Configures the ESP32 as a Wi-Fi access point and sends 16-bit PCM audio over UDP.
- **PC-side playback:** Uses a Python UDP listener and `sounddevice` to play incoming audio.
- **Resource monitoring:** Includes a task for logging CPU and RAM usage.

## Hardware and software

| Category | Components |
| --- | --- |
| Hardware | ESP32, INMP441 I²S MEMS microphone |
| Firmware | C, ESP-IDF, FreeRTOS, I²S driver, UDP sockets |
| PC receiver | Python, `sounddevice` |
| Signal processing | High-pass filtering, AGC, sample limiting |

### I²S pins used in the code

| INMP441 signal | ESP32 GPIO |
| --- | --- |
| SCK / BCLK | GPIO 26 |
| WS / LRCLK | GPIO 25 |
| SD / DOUT | GPIO 33 |

Also connect microphone power and ground appropriately; confirm the microphone board's voltage specifications. The I²S configuration uses the default left slot for mono capture.

## Running the project

1. Install **ESP-IDF** and configure it for the ESP32 target.
2. Connect the INMP441 using the pin mapping above.
3. From the project root, build, flash, and monitor the firmware:

   ```bash
   idf.py set-target esp32
   idf.py build
   idf.py -p PORT flash monitor
   ```

   Replace `PORT` with your board's serial port.

4. Connect the computer to the ESP32's Wi-Fi access point. The current firmware defines the SSID, password, and UDP destination in `main/esp_udp.c`. **Change the example credentials before sharing or deploying the setup.** The hard-coded receiver address is `192.168.4.2` and the UDP port is `4210`; ensure the receiving computer has the expected IP address, or update the configuration.
5. Install the Python playback dependency and run the receiver:

   ```bash
   pip install sounddevice
   python udp_connect.py
   ```

The PC listener binds to UDP port `4210` and plays received data as **16 kHz, mono, 16-bit PCM**. UDP delivery and playback quality depend on the network and audio configuration.

## Code structure

```text
.
├── main/
│   ├── esp_udp.c        # Wi-Fi SoftAP, UDP streaming, resource monitoring
│   ├── inmp441.c        # I2S microphone setup and audio capture
│   ├── inmp441.h
│   ├── preprocessing.c  # High-pass filter, AGC, and limiter
│   ├── preprocessing.h
│   ├── sih2026.c        # Earlier alternate entry-point experiment (not active)
│   └── CMakeLists.txt   # Active ESP-IDF source selection
├── udp_connect.py       # UDP reception and speaker playback
├── CMakeLists.txt
└── README.md
```

## Current scope and future work

The active firmware demonstrates microphone capture, preprocessing, and UDP audio streaming. Some DSP and TensorFlow Lite Micro dependencies are present, but **MFCC extraction and on-device keyword spotting are not part of the active application**. Potential extensions include more robust audio buffering, audio-quality evaluation, feature extraction, and keyword recognition.

---

*Developed as a hands-on embedded audio processing project using the ESP32 and INMP441 microphone.*
