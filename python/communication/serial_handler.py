"""
FREYA — Serial Handler with Smart Audio + INMP441 Mic Pipeline
================================================================

Two audio output modes:
  - Direct:   PCM <= 58KB sent in one shot, played gap-free
  - Streamed: PCM > 58KB sent in chunks, each played sequentially

Auto-detects baud rate (tries 921600 first, falls back to 115200).
"""

import serial
import json
import struct
import logging
import time
import wave
import io
import tempfile
from typing import Optional, Dict
from pathlib import Path

logger = logging.getLogger("serial_bridge")

# Must match main.cpp definitions
DIRECT_PLAY_MAX = 58000   # Matches DIRECT_PLAY_THRESHOLD in firmware
ESP32_BUFFER_SIZE = 60000  # Matches PLAY_BUFFER_SIZE in firmware

# Baud rates to try in order
BAUD_RATES = [921600, 115200]


class SerialBridge:

    def __init__(self, port: str, baudrate: int = None):
        self.port = port
        self.baudrate = baudrate  # None = auto-detect
        self.serial: Optional[serial.Serial] = None
        self.connected = False

    # ==================== CONNECTION ====================

    def connect(self, timeout: float = 10.0) -> bool:
        """
        Connect to ESP32 with auto baud detection.
        Tries each baud rate, checks for valid JSON.
        On Windows, must fully close + sleep between attempts.
        """
        baud_list = [self.baudrate] if self.baudrate else BAUD_RATES

        for baud in baud_list:
            logger.info(f"Trying {self.port} at {baud} baud...")
            try:
                # CRITICAL: fully release port before each attempt
                # Windows needs ~1.5s to actually free the COM port handle
                if self.serial is not None:
                    if self.serial.is_open:
                        self.serial.close()
                    self.serial = None
                    time.sleep(1.5)

                self.serial = serial.Serial(
                    self.port, baud,
                    timeout=1.0,
                    write_timeout=5
                )
                time.sleep(2.5)  # ESP32 resets when serial connects
                self.serial.reset_input_buffer()

                # Read lines — look for valid JSON and READY
                start = time.time()
                got_valid_json = False
                garbled_count = 0

                while time.time() - start < timeout:
                    if self.serial.in_waiting:
                        line = self.serial.readline().decode('utf-8', errors='ignore').strip()
                        if not line:
                            continue

                        logger.debug(f"ESP32 [{baud}]: {line[:80]}")

                        try:
                            data = json.loads(line)
                            got_valid_json = True
                            if data.get("type") == "READY":
                                self.connected = True
                                self.baudrate = baud
                                logger.info(
                                    f"ESP32 ready at {baud} baud — "
                                    f"speaker:{data.get('speaker')}, "
                                    f"mic:{data.get('mic')}, oled:{data.get('oled')}"
                                )
                                return True
                        except json.JSONDecodeError:
                            garbled_count += 1
                            # 3+ garbled lines = definitely wrong baud
                            if garbled_count >= 3:
                                logger.info(f"Garbled data at {baud} — wrong baud")
                                break
                    time.sleep(0.1)

                # Got JSON but READY already passed
                if got_valid_json:
                    self.connected = True
                    self.baudrate = baud
                    logger.info(f"Connected at {baud} baud (READY already passed)")
                    return True

                if garbled_count == 0:
                    logger.info(f"No response at {baud}")

                # Close before next attempt
                self.serial.close()
                self.serial = None
                time.sleep(1.5)

            except serial.SerialException as e:
                logger.warning(f"Port error at {baud}: {e}")
                if self.serial is not None and self.serial.is_open:
                    self.serial.close()
                self.serial = None
                time.sleep(1.5)
                continue

        # All auto-detect failed — force the first baud as last resort
        fallback = baud_list[0]
        logger.warning(f"Auto-detect failed, forcing {fallback}")
        try:
            if self.serial is not None and self.serial.is_open:
                self.serial.close()
                time.sleep(1.5)
            self.serial = serial.Serial(self.port, fallback, timeout=1.0, write_timeout=5)
            time.sleep(2)
            self.serial.reset_input_buffer()
            self.connected = True
            self.baudrate = fallback
            logger.info(f"Connected at {fallback} baud (forced)")
            return True
        except Exception as e:
            logger.error(f"Connection failed: {e}")

        return False

    def disconnect(self):
        if self.serial and self.serial.is_open:
            self.serial.close()
        self.serial = None
        self.connected = False
        logger.info("Disconnected")

    # ==================== LOW-LEVEL IO ====================

    def _send_json(self, data: Dict) -> bool:
        if not self.connected:
            return False
        try:
            msg = json.dumps(data, separators=(',', ':')) + "\n"
            self.serial.write(msg.encode('utf-8'))
            self.serial.flush()
            return True
        except Exception as e:
            logger.error(f"Send error: {e}")
            return False

    def _wait_for(self, expected_type: str, timeout: float = 5.0) -> Optional[Dict]:
        """Wait for a specific JSON response type from ESP32."""
        start = time.time()
        while time.time() - start < timeout:
            if self.serial.in_waiting:
                try:
                    line = self.serial.readline().decode('utf-8', errors='ignore').strip()
                    if line and line.startswith('{'):
                        data = json.loads(line)
                        logger.debug(f"ESP32: {data}")
                        if data.get("type") == expected_type:
                            return data
                except (json.JSONDecodeError, UnicodeDecodeError):
                    pass
            time.sleep(0.01)
        return None

    def _drain_serial(self, timeout: float = 0.3):
        """Read and discard all pending serial data."""
        start = time.time()
        while time.time() - start < timeout:
            if self.serial.in_waiting:
                self.serial.readline()
            else:
                time.sleep(0.01)

    # ==================== WAV HELPERS ====================

    @staticmethod
    def strip_wav_header(audio_data: bytes) -> bytes:
        """Extract raw PCM from WAV data. Returns original if not WAV."""
        if audio_data[:4] == b'RIFF':
            pos = audio_data.find(b'data')
            if pos != -1:
                return audio_data[pos + 8:]
        return audio_data

    @staticmethod
    def pcm_to_wav(pcm_data: bytes, sample_rate: int = 16000,
                   channels: int = 1, sample_width: int = 2) -> bytes:
        """Wrap raw PCM in a proper WAV header for Whisper."""
        buf = io.BytesIO()
        with wave.open(buf, 'wb') as wf:
            wf.setnchannels(channels)
            wf.setsampwidth(sample_width)
            wf.setframerate(sample_rate)
            wf.writeframes(pcm_data)
        return buf.getvalue()

    @staticmethod
    def amplify_pcm(pcm_data: bytes, gain: float = 2.0) -> bytes:
        """Amplify PCM audio. gain=1.0 is unchanged, 2.0 is double volume."""
        if len(pcm_data) % 2 != 0:
            pcm_data = pcm_data[:-1]

        n_samples = len(pcm_data) // 2
        samples = struct.unpack(f'<{n_samples}h', pcm_data)
        amplified = [max(-32768, min(32767, int(s * gain))) for s in samples]
        return struct.pack(f'<{n_samples}h', *amplified)

    # ==================== SMART AUDIO OUTPUT ====================

    def send_audio(self, wav_audio: bytes, gain: float = 2.5) -> bool:
        """
        Smart audio send — auto-chooses direct or streamed mode.

        Args:
            wav_audio: WAV audio bytes (with header)
            gain: Volume amplification (1.0 = original, 2.5 = recommended)
        """
        if not self.connected or not wav_audio:
            return False

        pcm = self.strip_wav_header(wav_audio)
        if gain != 1.0:
            pcm = self.amplify_pcm(pcm, gain)

        total_size = len(pcm)
        logger.info(f"Audio: {total_size} bytes ({total_size / 44100:.1f}s at 22050Hz)")

        if total_size <= DIRECT_PLAY_MAX:
            return self._send_direct(pcm)
        else:
            return self._send_streamed(pcm)

    def _send_direct(self, pcm: bytes) -> bool:
        """Mode B: Send all audio at once for gap-free playback."""
        total = len(pcm)
        logger.info(f"Direct mode: {total} bytes")

        self._drain_serial()

        self._send_json({"type": "AUDIO_START", "size": total})

        resp = self._wait_for("AUDIO_READY", timeout=5.0)
        if not resp:
            logger.error("No AUDIO_READY")
            return False

        # Send data — pace based on actual baud rate
        offset = 0
        baud = self.baudrate or 115200
        bytes_per_sec = baud / 10  # rough serial throughput
        burst_size = min(2048, int(bytes_per_sec * 0.02))  # 20ms worth
        burst_size = max(256, burst_size)

        while offset < total:
            chunk = pcm[offset:offset + burst_size]
            self.serial.write(chunk)
            offset += len(chunk)
            time.sleep(0.008)

        self.serial.flush()

        play_time = total / 44100
        resp = self._wait_for("DONE", timeout=play_time + 8)
        if resp:
            logger.info(f"Direct play complete: {resp.get('played', 0)} bytes")
        return True

    def _send_streamed(self, pcm: bytes) -> bool:
        """Mode A: Send audio in chunks, each played before next is sent."""
        total = len(pcm)
        chunk_max = ESP32_BUFFER_SIZE - 2000
        n_chunks = (total + chunk_max - 1) // chunk_max

        logger.info(f"Streamed mode: {total} bytes in {n_chunks} chunks")

        baud = self.baudrate or 115200
        burst_size = min(2048, max(256, int(baud / 10 * 0.02)))

        offset = 0
        chunk_num = 1

        while offset < total:
            chunk = pcm[offset:offset + chunk_max]
            chunk_size = len(chunk)

            logger.info(f"Chunk {chunk_num}/{n_chunks}: {chunk_size} bytes")

            # Clear stale responses
            if self.serial.in_waiting:
                time.sleep(0.1)
                while self.serial.in_waiting:
                    self.serial.readline()

            self._send_json({"type": "AUDIO_START", "size": chunk_size})

            resp = self._wait_for("AUDIO_READY", timeout=5.0)
            if not resp:
                logger.error(f"No AUDIO_READY for chunk {chunk_num}")
                return False

            # Send chunk
            sent = 0
            while sent < chunk_size:
                burst = chunk[sent:sent + burst_size]
                self.serial.write(burst)
                sent += len(burst)
                time.sleep(0.008)

            self.serial.flush()

            # Wait for playback to finish
            # Accept CHUNK_DONE (new firmware) or DONE (old firmware)
            play_time = chunk_size / 44100
            total_wait = play_time + 10

            start_wait = time.time()
            chunk_done = False
            while time.time() - start_wait < total_wait:
                if self.serial.in_waiting:
                    try:
                        line = self.serial.readline().decode('utf-8', errors='ignore').strip()
                        if line and line.startswith('{'):
                            data = json.loads(line)
                            msg_type = data.get("type", "")
                            logger.debug(f"ESP32: {data}")
                            if msg_type in ("CHUNK_DONE", "DONE"):
                                chunk_done = True
                                break
                    except (json.JSONDecodeError, UnicodeDecodeError):
                        pass
                time.sleep(0.02)

            if not chunk_done:
                logger.warning(f"Chunk {chunk_num} — no done response, continuing anyway")
                time.sleep(1)

            offset += chunk_size
            chunk_num += 1

        logger.info("All chunks played")
        return True

    # ==================== MICROPHONE ====================

    def record_audio(self, duration: float = 3.0) -> Optional[bytes]:
        """Record from ESP32 INMP441. Returns WAV bytes or None."""
        if not self.connected:
            return None

        logger.info(f"Recording {duration}s from ESP32 mic...")

        self._drain_serial()
        self._send_json({"type": "RECORD", "duration": duration})

        resp = self._wait_for("RECORDING", timeout=3.0)
        if not resp:
            logger.error("ESP32 did not start recording")
            return None

        resp = self._wait_for("RECORDED", timeout=duration + 5)
        if not resp:
            logger.error("Recording timed out")
            return None

        is_valid = resp.get("valid") == "true"
        recorded_bytes = resp.get("bytes", 0)
        logger.info(f"Recorded {recorded_bytes} bytes, valid={is_valid}")

        resp = self._wait_for("AUDIO_DATA_START", timeout=3.0)
        if not resp:
            logger.error("No AUDIO_DATA_START")
            return None

        expected_bytes = resp.get("bytes", recorded_bytes)
        sample_rate = resp.get("rate", 16000)

        # Receive raw PCM data
        audio_data = b""
        start_time = time.time()
        recv_timeout = max(15.0, expected_bytes / 50000)

        while time.time() - start_time < recv_timeout:
            if self.serial.in_waiting:
                chunk = self.serial.read(self.serial.in_waiting)

                end_marker = b'{"type":"AUDIO_DATA_END"'
                if end_marker in chunk:
                    idx = chunk.find(end_marker)
                    audio_data += chunk[:idx]
                    break

                audio_data += chunk

                if len(audio_data) >= expected_bytes:
                    break
            else:
                time.sleep(0.005)

        self._wait_for("AUDIO_DATA_END", timeout=2.0)

        logger.info(f"Received {len(audio_data)} bytes (expected {expected_bytes})")

        if len(audio_data) < 100:
            logger.error("Too little audio data received")
            return None

        audio_data = self.amplify_pcm(audio_data, gain=30.0)
        wav_data = self.pcm_to_wav(audio_data, sample_rate=sample_rate)
        logger.info(f"Created WAV: {len(wav_data)} bytes")
        return wav_data

    def record_to_file(self, duration: float = 3.0, filepath: str = None) -> Optional[str]:
        """Record from ESP32 mic and save as WAV file."""
        wav_data = self.record_audio(duration)
        if not wav_data:
            return None

        if filepath is None:
            tmp = tempfile.NamedTemporaryFile(suffix='.wav', delete=False)
            filepath = tmp.name
            tmp.close()

        with open(filepath, 'wb') as f:
            f.write(wav_data)

        logger.info(f"Saved recording to {filepath}")
        return filepath

    # ==================== DIAGNOSTICS ====================

    def test_microphone(self) -> Optional[Dict]:
        """Run mic diagnostic on ESP32."""
        self._send_json({"type": "TEST_MIC"})
        return self._wait_for("MIC_TEST_RESULT", timeout=5.0)

    def test_speaker(self) -> bool:
        """Play test melody on ESP32 speaker."""
        self._send_json({"type": "TEST_SOUND"})
        return self._wait_for("ACK", timeout=3.0) is not None

    # ==================== EMOTION ====================

    EMOTION_MAP = {
        "neutral": "0", "normal": "0", "thinking": "8", "listening": "8",
        "happy": "1", "excited": "1", "joy": "1", "laugh": "1",
        "sad": "2", "worried": "2",
        "surprised": "3", "confused": "3", "curious": "3",
        "angry": "4", "annoyed": "4", "frustrated": "4",
        "sleepy": "5", "tired": "5", "bored": "5",
        "wink": "6", "flirty": "6", "playful": "6",
        "love": "7", "loving": "7", "caring": "7",
        "recording": "9",
    }

    def send_emotion(self, emotion: str):
        """Send emotion to OLED eyes."""
        if not self.connected:
            return
        cmd = self.EMOTION_MAP.get(emotion.lower().strip(), "0")
        try:
            self.serial.write(cmd.encode())
            self.serial.flush()
            logger.debug(f"Emotion: {emotion} -> {cmd}")
        except Exception as e:
            logger.error(f"Emotion send failed: {e}")

    def play_melody(self):
        if self.connected:
            self.serial.write(b'm')
            self.serial.flush()
            time.sleep(1.5)

    def play_beep(self):
        if self.connected:
            self.serial.write(b'b')
            self.serial.flush()
            time.sleep(0.15)


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
    time.sleep(2)

    print("\n=== Mic Test ===")
    result = bridge.test_microphone()
    print(f"  Result: {result}")

    print("\n=== Emotions ===")
    for e in ["happy", "surprised", "angry", "sad", "love", "neutral"]:
        print(f"  {e}")
        bridge.send_emotion(e)
        time.sleep(1)

    bridge.disconnect()
    print("\nDone!")