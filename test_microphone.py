"""
FREYA - Simple Microphone Test
==============================

This script tests:
1. Speaker (plays melody)
2. Microphone (records 5 seconds)
3. Whisper transcription

Run this AFTER flashing main_test.cpp to ESP32
"""

import serial
import json
import time
import wave
import io
import sys
import os

# Add parent directory to path for imports
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

# ==================== CONFIGURATION ====================
COM_PORT = "COM4"  # <-- CHANGE THIS TO YOUR PORT!
BAUD_RATE = 921600


def create_wav_from_raw(raw_data: bytes, sample_rate: int = 16000) -> bytes:
    """Convert raw PCM to WAV format"""
    wav_buffer = io.BytesIO()
    with wave.open(wav_buffer, 'wb') as wav_file:
        wav_file.setnchannels(1)
        wav_file.setsampwidth(2)
        wav_file.setframerate(sample_rate)
        wav_file.writeframes(raw_data)
    return wav_buffer.getvalue()


def wait_for_json(ser, expected_type, timeout=10):
    """Wait for specific JSON response"""
    start = time.time()
    while time.time() - start < timeout:
        if ser.in_waiting:
            line = ser.readline().decode('utf-8', errors='ignore').strip()
            if line:
                print(f"  ESP32: {line}")
                if line.startswith('{'):
                    try:
                        data = json.loads(line)
                        if data.get("type") == expected_type:
                            return data
                    except:
                        pass
        time.sleep(0.01)
    return None


def main():
    print("\n" + "=" * 50)
    print("FREYA - Microphone & Speaker Test")
    print("=" * 50)

    # Connect to ESP32
    print(f"\nConnecting to ESP32 on {COM_PORT}...")

    try:
        ser = serial.Serial(COM_PORT, BAUD_RATE, timeout=1)
        time.sleep(2)
        ser.reset_input_buffer()
    except Exception as e:
        print(f"❌ Failed to connect: {e}")
        print("\nMake sure:")
        print("  1. ESP32 is connected via USB")
        print("  2. Correct COM port is set")
        print("  3. Serial Monitor is NOT open in PlatformIO")
        return

    # Wait for READY
    print("Waiting for ESP32 to be ready...")
    response = wait_for_json(ser, "READY", timeout=10)
    if response:
        print("✅ ESP32 is ready!\n")
    else:
        print("⚠️ No READY message, continuing anyway...\n")

    # ==================== TEST 1: SPEAKER ====================
    print("-" * 40)
    print("TEST 1: Speaker")
    print("-" * 40)
    input("Press Enter to play test sound...")

    ser.write(b'{"type":"TEST_SOUND"}\n')
    response = wait_for_json(ser, "ACK", timeout=5)

    if response:
        print("✅ Speaker test complete!")
    else:
        print("⚠️ No ACK received")

    heard = input("\nDid you hear the melody? (y/n): ").lower()
    if heard == 'y':
        print("✅ Speaker working!\n")
    else:
        print("❌ Speaker not working. Check wiring:")
        print("   BCLK -> GPIO 26")
        print("   LRC  -> GPIO 25")
        print("   DIN  -> GPIO 22")
        print("   VIN  -> 3.3V")
        print("   GND  -> GND\n")

    # ==================== TEST 2: MICROPHONE ====================
    print("-" * 40)
    print("TEST 2: Microphone Signal")
    print("-" * 40)
    print("Testing microphone signal level...")

    ser.write(b'{"type":"TEST_MIC"}\n')
    response = wait_for_json(ser, "MIC_TEST_RESULT", timeout=5)

    if response:
        amplitude = response.get("max_amplitude", 0)
        print(f"Microphone amplitude: {amplitude}")

        if amplitude > 100:
            print("✅ Microphone signal detected!\n")
        elif amplitude > 10:
            print("⚠️ Weak signal. Try speaking louder or check wiring.\n")
        else:
            print("❌ No signal. Check microphone wiring:")
            print("   SCK  -> GPIO 14")
            print("   WS   -> GPIO 15")
            print("   SD   -> GPIO 32")
            print("   VDD  -> 3.3V")
            print("   GND  -> GND")
            print("   L/R  -> GND\n")
    else:
        print("❌ No response from microphone test\n")

    # ==================== TEST 3: RECORDING ====================
    print("-" * 40)
    print("TEST 3: Record & Transcribe")
    print("-" * 40)
    input("Press Enter to start 5-second recording...")
    print("🎤 Recording... SPEAK NOW!")

    ser.write(b'{"type":"RECORD"}\n')

    # Wait for RECORDING
    response = wait_for_json(ser, "RECORDING", timeout=3)
    if not response:
        print("❌ Recording did not start")
        ser.close()
        return

    # Wait for RECORDED
    print("Recording in progress...")
    response = wait_for_json(ser, "RECORDED", timeout=10)
    if not response:
        print("❌ Recording failed")
        ser.close()
        return

    recorded_bytes = response.get("bytes", 0)
    print(f"ESP32 recorded {recorded_bytes} bytes")

    # Wait for AUDIO_DATA_START
    response = wait_for_json(ser, "AUDIO_DATA_START", timeout=3)
    if not response:
        print("❌ No audio data start")
        ser.close()
        return

    # Read audio data
    print("Receiving audio data...")
    audio_data = b""
    start_time = time.time()

    while time.time() - start_time < 15:
        if ser.in_waiting:
            chunk = ser.read(ser.in_waiting)

            if b'{"type":"AUDIO_DATA_END"}' in chunk:
                end_idx = chunk.find(b'{"type":"AUDIO_DATA_END"}')
                audio_data += chunk[:end_idx]
                break

            audio_data += chunk
        else:
            time.sleep(0.01)

    print(f"Received {len(audio_data)} bytes from ESP32")

    if len(audio_data) < 1000:
        print("❌ Not enough audio data received")
        ser.close()
        return

    # Save raw audio
    with open("test_recording.raw", "wb") as f:
        f.write(audio_data)
    print("Saved raw audio to: test_recording.raw")

    # Convert to WAV
    wav_data = create_wav_from_raw(audio_data, sample_rate=16000)
    with open("test_recording.wav", "wb") as f:
        f.write(wav_data)
    print("Saved WAV audio to: test_recording.wav")

    # ==================== TEST 4: TRANSCRIPTION ====================
    print("\n" + "-" * 40)
    print("TEST 4: Whisper Transcription")
    print("-" * 40)

    try:
        # Try to import and use Whisper
        print("Loading Whisper model...")
        import whisper

        model = whisper.load_model("small.en")
        print("Transcribing...")

        result = model.transcribe("test_recording.wav")
        text = result.get("text", "").strip()

        print("\n" + "=" * 40)
        print("🎯 TRANSCRIPTION RESULT:")
        print("=" * 40)
        print(f"\n\"{text}\"\n")
        print("=" * 40)

        if text:
            print("\n✅ SUCCESS! Microphone and transcription working!")
        else:
            print("\n⚠️ Empty transcription. Try speaking louder or closer to mic.")

    except ImportError:
        print("⚠️ Whisper not installed. Install with:")
        print("   pip install openai-whisper")
        print("\nYou can still play test_recording.wav to verify audio quality.")
    except Exception as e:
        print(f"❌ Transcription error: {e}")

    # Cleanup
    ser.close()
    print("\n✅ Test complete!")
    print("\nFiles created:")
    print("  - test_recording.raw (raw PCM)")
    print("  - test_recording.wav (playable audio)")


if __name__ == "__main__":
    main()








