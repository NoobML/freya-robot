import serial
import wave
import time

PORT = "COM4"
BAUD = 115200


def record_audio():
    try:
        ser = serial.Serial(PORT, BAUD, timeout=10)
        print(f"Connected to {PORT}")
    except:
        print("Could not open Port. Is Serial Monitor still open in VS Code?")
        return

    while True:
        cmd = input("Press Enter to Record 3s (q to quit): ")
        if cmd.lower() == 'q': break

        ser.write(b'R')

        # Wait for the Start Flag
        print("Waiting for ESP32...")
        while True:
            line = ser.readline().decode(errors='ignore').strip()
            if "DATA_START:" in line:
                expected_bytes = int(line.split(":")[1])
                break

        print(f"Recording... capturing {expected_bytes} bytes")

        raw_data = b''
        while len(raw_data) < expected_bytes:
            chunk = ser.read(expected_bytes - len(raw_data))
            if chunk:
                raw_data += chunk

        # Save file
        with wave.open("test_record.wav", "wb") as wf:
            wf.setnchannels(1)
            wf.setsampwidth(2)
            wf.setframerate(16000)
            wf.writeframes(raw_data)

        print("Success! Saved to test_record.wav")
        # Read the 'DATA_END' message to clear the buffer
        ser.readline()


if __name__ == "__main__":
    record_audio()