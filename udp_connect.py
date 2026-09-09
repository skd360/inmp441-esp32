import socket
import sounddevice as sd

ESP32_IP = "192.168.4.1"
UDP_PORT = 4210

SAMPLE_RATE = 16000
CHANNELS = 1
BLOCKSIZE = 1024


def main():

    # Create UDP socket
    sock = socket.socket(
        socket.AF_INET,
        socket.SOCK_DGRAM
    )

    # Listen on UDP port 4210
    sock.bind(("", UDP_PORT))

    print(f"Listening for audio on UDP port {UDP_PORT}")

    # Open laptop speaker
    stream = sd.RawOutputStream(
        samplerate=SAMPLE_RATE,
        channels=CHANNELS,
        dtype="int16",
        blocksize=BLOCKSIZE,
        latency="low",
    )

    stream.start()

    print("Speaker ready.")

    try:
        while True:

            # Receive UDP audio packet
            data, addr = sock.recvfrom(4096)

            print(
                f"\rReceived {len(data)} bytes from {addr}",
                end=""
            )

            # Make sure we have complete int16 samples
            if len(data) % 2 != 0:
                data = data[:-1]

            # Play audio
            stream.write(data)

    except KeyboardInterrupt:
        print("\nStopping...")

    finally:
        stream.stop()
        stream.close()
        sock.close()


if __name__ == "__main__":
    main()