"""
FREYA — Serial Handler (PC Mic, ESP32 Speaker/Eyes/Servos)
===========================================================
ESP32 handles: Speaker output, OLED eyes, Servo emotions
PC handles: Microphone input (via Whisper STT)
"""

import serial
import logging
import time
from typing import Optional

logger = logging.getLogger("serial_bridge")

class SerialBridge:

    def __init__(self, port: str, baudrate: int = 921600):
        self.port = port
        self.baudrate = baudrate
        self.serial: Optional[serial.Serial] = None
        self.connected = False

    # ==================== CONNECTION ====================

    def connect(self, timeout: float = 5.0) -> bool:
        """Connect to ESP32."""
        try:
            logger.info(f"Connecting to {self.port} at {self.baudrate} baud...")
            self.serial = serial.Serial(
                self.port,
                self.baudrate,
                timeout=2.0,
                write_timeout=5
            )
            time.sleep(3)  # ESP32 reset delay
            self.serial.reset_input_buffer()

            # Wait for READY signal
            start = time.time()
            while time.time() - start < timeout:
                if self.serial.in_waiting:
                    line = self.serial.readline().decode('utf-8', errors='ignore').strip()
                    if line:
                        logger.debug(f"ESP32: {line}")
                        if "READY" in line:
                            self.connected = True
                            logger.info(f"ESP32 ready at {self.baudrate} baud")
                            return True
                time.sleep(0.1)

            # No READY, but assume connected if serial port opened
            self.connected = True
            logger.info(f"Connected at {self.baudrate} baud (no READY signal)")
            return True

        except Exception as e:
            logger.error(f"Connection failed: {e}")
            return False

    def disconnect(self):
        """Disconnect from ESP32."""
        if self.serial and self.serial.is_open:
            self.serial.close()
        self.serial = None
        self.connected = False
        logger.info("Disconnected")

    # ==================== EMOTION CONTROL ====================

    def send_emotion(self, emotion: str):
        """
        Send emotion command to ESP32.
        This triggers both OLED eyes AND servo movements.
        """
        if not self.connected:
            return False

        try:
            command = f"EMOTION:{emotion}\n"
            self.serial.write(command.encode('utf-8'))
            self.serial.flush()
            logger.debug(f"Sent emotion: {emotion}")

            # Wait for acknowledgment
            time.sleep(0.1)
            if self.serial.in_waiting:
                response = self.serial.readline().decode('utf-8', errors='ignore').strip()
                logger.debug(f"ESP32: {response}")

            return True

        except Exception as e:
            logger.error(f"Emotion send failed: {e}")
            return False

    def stop_servos(self):
        """Immediately stop servo movements."""
        if not self.connected:
            return False

        try:
            self.serial.write(b"STOP:SERVOS\n")
            self.serial.flush()
            logger.debug("Stopped servos")
            return True
        except Exception as e:
            logger.error(f"Servo stop failed: {e}")
            return False

    # ==================== AUDIO PLAYBACK ====================

    def send_audio(self, audio_bytes: bytes, gain: float = 2.0) -> bool:
        """
        Send audio to ESP32 speaker.
        Audio must be WAV format (will strip header automatically).
        """
        if not self.connected:
            logger.error("Not connected to ESP32")
            return False

        try:
            # Strip WAV header if present
            pcm_data = self._strip_wav_header(audio_bytes)

            # Amplify audio
            pcm_data = self._amplify_pcm(pcm_data, gain)

            # Send size header
            audio_size = len(pcm_data)
            command = f"AUDIO:{audio_size}\n"
            self.serial.write(command.encode('utf-8'))
            self.serial.flush()

            logger.info(f"Sending {audio_size} bytes to ESP32...")

            # Wait brief moment for ESP32 to prepare
            time.sleep(0.05)

            # Send audio data in chunks
            chunk_size = 512
            sent = 0

            while sent < audio_size:
                end = min(sent + chunk_size, audio_size)
                chunk = pcm_data[sent:end]
                self.serial.write(chunk)
                sent += len(chunk)
                time.sleep(0.005)  # Small delay between chunks

            self.serial.flush()
            logger.info("Audio sent successfully")

            # Wait for ESP32 to finish playing
            play_duration = audio_size / 44100  # Rough estimate
            time.sleep(play_duration + 0.5)

            # Check for OK response
            if self.serial.in_waiting:
                response = self.serial.readline().decode('utf-8', errors='ignore').strip()
                logger.debug(f"ESP32: {response}")

            return True

        except Exception as e:
            logger.error(f"Audio send failed: {e}")
            return False

    # ==================== TEST FUNCTIONS ====================

    def test_speaker(self) -> bool:
        """Play test beep on ESP32 speaker."""
        if not self.connected:
            return False

        try:
            self.serial.write(b"TEST:SPEAKER\n")
            self.serial.flush()
            time.sleep(1)

            if self.serial.in_waiting:
                response = self.serial.readline().decode('utf-8', errors='ignore').strip()
                logger.debug(f"ESP32: {response}")
                return "OK" in response

            return True
        except Exception as e:
            logger.error(f"Speaker test failed: {e}")
            return False

    def play_beep(self):
        """Quick beep sound."""
        return self.test_speaker()

    # ==================== HELPER FUNCTIONS ====================

    @staticmethod
    def _strip_wav_header(audio_data: bytes) -> bytes:
        """Remove WAV header, return raw PCM."""
        if audio_data[:4] == b'RIFF':
            # Find 'data' chunk
            pos = audio_data.find(b'data')
            if pos != -1:
                # Skip 'data' marker (4 bytes) + size (4 bytes)
                return audio_data[pos + 8:]
        return audio_data

    @staticmethod
    def _amplify_pcm(pcm_data: bytes, gain: float = 2.0) -> bytes:
        """Amplify 16-bit PCM audio."""
        import struct

        if len(pcm_data) % 2 != 0:
            pcm_data = pcm_data[:-1]

        n_samples = len(pcm_data) // 2
        samples = struct.unpack(f'<{n_samples}h', pcm_data)

        amplified = []
        for s in samples:
            val = int(s * gain)
            val = max(-32768, min(32767, val))  # Clamp to 16-bit range
            amplified.append(val)

        return struct.pack(f'<{len(amplified)}h', *amplified)


# ==================== STANDALONE TEST ====================

if __name__ == "__main__":
    logging.basicConfig(
        level=logging.DEBUG,
        format='%(asctime)s [%(name)s] %(levelname)s: %(message)s'
    )

    import sys
    port = sys.argv[1] if len(sys.argv) > 1 else "COM4"

    bridge = SerialBridge(port)

    if not bridge.connect():
        print("Failed to connect!")
        sys.exit(1)

    print(f"\nConnected at {bridge.baudrate} baud")

    print("\n=== Speaker Test ===")
    bridge.test_speaker()
    time.sleep(1)

    print("\n=== Emotion Test ===")
    for emotion in ["happy", "surprised", "angry", "sad", "love", "sleepy", "neutral"]:
        print(f"  Testing: {emotion}")
        bridge.send_emotion(emotion)
        time.sleep(3)  # Wait for servo movement to complete

    print("\n=== Stop Servos ===")
    bridge.stop_servos()

    bridge.disconnect()
    print("\nDone!")