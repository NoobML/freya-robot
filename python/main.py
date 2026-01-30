"""
FREYA - AI Voice Assistant (PC Mic + ESP32 Speaker/OLED)
========================================================

Flow:
  Headphone Mic → Whisper STT → Gemini LLM → Piper TTS → ESP32 Speaker
                                    ↓
                              OLED Emotions
"""

import logging
import sys
import time
import re
import tempfile
import os
import wave
import io
from pathlib import Path

import serial
import speech_recognition as sr

# Add project root to path
sys.path.insert(0, str(Path(__file__).parent))

from services.stt_service import WhisperSTTService
from services.llm_service import LLMService
from services.tts_service import PiperTTSService
from config import settings

logging.basicConfig(
    level=logging.INFO,
    format='%(asctime)s - %(levelname)s - %(message)s'
)
logger = logging.getLogger("freya")


def extract_raw_pcm(audio_bytes: bytes) -> tuple:
    """
    Extract raw PCM data from WAV file bytes.
    Returns (pcm_data, sample_rate, channels, sample_width)
    """
    try:
        wav_io = io.BytesIO(audio_bytes)

        with wave.open(wav_io, 'rb') as wav_file:
            n_channels = wav_file.getnchannels()
            sample_width = wav_file.getsampwidth()
            frame_rate = wav_file.getframerate()
            n_frames = wav_file.getnframes()

            logger.info(f"WAV: {n_channels}ch, {sample_width*8}bit, {frame_rate}Hz, {n_frames} frames")

            pcm_data = wav_file.readframes(n_frames)

        return pcm_data, frame_rate, n_channels, sample_width

    except Exception as e:
        logger.error(f"Failed to extract PCM: {e}")
        return audio_bytes, 22050, 1, 2


class ESP32Bridge:
    """Handles communication with ESP32"""

    EMOTION_MAP = {
        "neutral": "0", "normal": "0", "thinking": "0", "listening": "0",
        "happy": "1", "excited": "1", "joy": "1", "laugh": "1",
        "sad": "2", "worried": "2",
        "surprised": "3", "confused": "3", "curious": "3",
        "angry": "4", "annoyed": "4", "frustrated": "4",
        "sleepy": "5", "tired": "5", "bored": "5",
        "wink": "6", "flirty": "6", "playful": "6",
        "love": "7", "loving": "7", "caring": "7",
    }

    def __init__(self, port: str, baudrate: int = 115200):
        self.port = port
        self.baudrate = baudrate
        self.serial = None
        self.connected = False

    def connect(self) -> bool:
        try:
            self.serial = serial.Serial(
                port=self.port,
                baudrate=self.baudrate,
                timeout=2,
                write_timeout=2
            )
            time.sleep(2)  # Wait for ESP32 reset

            # Clear buffers
            self.serial.reset_input_buffer()
            self.serial.reset_output_buffer()

            self.connected = True
            logger.info(f"✅ Connected to ESP32 on {self.port}")

            # Read any startup messages
            time.sleep(0.5)
            while self.serial.in_waiting:
                line = self.serial.readline().decode('utf-8', errors='ignore').strip()
                if line:
                    logger.debug(f"ESP32: {line}")

            return True
        except Exception as e:
            logger.error(f"❌ Failed to connect: {e}")
            self.connected = False
            return False

    def disconnect(self):
        if self.serial and self.serial.is_open:
            self.serial.close()
        self.connected = False

    def send_emotion(self, emotion: str):
        if not self.connected:
            return

        cmd = self.EMOTION_MAP.get(emotion.lower().strip(), "0")

        try:
            self.serial.write(cmd.encode())
            self.serial.flush()
            logger.info(f"😊 Emotion: {emotion} → '{cmd}'")
        except Exception as e:
            logger.error(f"Emotion send failed: {e}")

    def play_melody(self) -> bool:
        if not self.connected:
            return False
        try:
            self.serial.write(b'm')
            self.serial.flush()
            time.sleep(2)
            return True
        except:
            return False

    def play_beep(self):
        if not self.connected:
            return
        try:
            self.serial.write(b'9')
            self.serial.flush()
            time.sleep(0.15)
        except:
            pass

    def _read_responses(self, timeout: float = 0.5) -> list:
        """Read all available responses from ESP32"""
        responses = []
        start = time.time()

        while time.time() - start < timeout:
            if self.serial.in_waiting:
                try:
                    line = self.serial.readline().decode('utf-8', errors='ignore').strip()
                    if line:
                        responses.append(line)
                        logger.debug(f"ESP32: {line}")
                except:
                    pass
            else:
                time.sleep(0.01)

        return responses


    def send_audio(self, wav_audio: bytes) -> bool:
        """Send audio to ESP32 speaker"""
        if not self.connected or not wav_audio:
            return False

        try:
            # Extract raw PCM from WAV
            raw_pcm, sample_rate, channels, sample_width = extract_raw_pcm(wav_audio)

            # Cap to ESP32 buffer size (80000 bytes)
            max_size = 80000
            if len(raw_pcm) > max_size:
                logger.warning(f"Audio too long ({len(raw_pcm)} bytes), truncating to {max_size}")
                raw_pcm = raw_pcm[:max_size]

            audio_size = len(raw_pcm)
            logger.info(f"Sending {audio_size} bytes of PCM audio")

            # Clear serial buffers
            self.serial.reset_input_buffer()
            self.serial.reset_output_buffer()
            time.sleep(0.1)

            # Send AUDIO_START with size
            cmd = f'{{"type":"AUDIO_START","size":{audio_size}}}\n'
            logger.info(f"Sending: {cmd.strip()}")
            self.serial.write(cmd.encode())
            self.serial.flush()

            # Wait for AUDIO_READY
            ready = False
            start_time = time.time()

            while time.time() - start_time < 3:
                if self.serial.in_waiting:
                    response = self.serial.readline().decode('utf-8', errors='ignore').strip()
                    logger.info(f"ESP32: {response}")
                    if "AUDIO_READY" in response:
                        ready = True
                        break
                time.sleep(0.05)

            if not ready:
                logger.warning("No AUDIO_READY received")
                return False

            # Send raw PCM data in chunks
            chunk_size = 1024
            total_sent = 0

            logger.info(f"Streaming {audio_size} bytes...")

            for i in range(0, audio_size, chunk_size):
                chunk = raw_pcm[i:i + chunk_size]
                self.serial.write(chunk)
                total_sent += len(chunk)
                time.sleep(0.005)  # Small delay to prevent overflow

            self.serial.flush()
            logger.info(f"✅ Sent {total_sent} bytes")

            # Wait for playback
            playback_time = audio_size / 44100
            logger.info(f"Waiting {playback_time:.1f}s for playback...")

            start_time = time.time()
            while time.time() - start_time < playback_time + 3:
                if self.serial.in_waiting:
                    response = self.serial.readline().decode('utf-8', errors='ignore').strip()
                    if response:
                        logger.info(f"ESP32: {response}")
                        if "DONE" in response:
                            return True
                time.sleep(0.1)

            return True

        except Exception as e:
            logger.error(f"Audio send failed: {e}")
            import traceback
            traceback.print_exc()
            return False


class PCMicListener:
    """Handles PC/Headphone microphone input"""

    def __init__(self, device_index: int = None):
        self.recognizer = sr.Recognizer()
        self.device_index = device_index

        print("\n🎤 Available microphones:")
        for i, mic_name in enumerate(sr.Microphone.list_microphone_names()):
            print(f"   [{i}] {mic_name}")
        print()

        self.microphone = sr.Microphone(device_index=device_index)

        logger.info("🎤 Calibrating microphone...")
        with self.microphone as source:
            self.recognizer.adjust_for_ambient_noise(source, duration=1)
        logger.info("✅ Microphone ready")

    def listen(self, timeout: int = 5, phrase_limit: int = 15) -> bytes:
        try:
            with self.microphone as source:
                audio = self.recognizer.listen(
                    source,
                    timeout=timeout,
                    phrase_time_limit=phrase_limit
                )
                return audio.get_wav_data()
        except sr.WaitTimeoutError:
            logger.warning("⏰ Listening timed out")
            return None
        except Exception as e:
            logger.error(f"Mic error: {e}")
            return None


class EmotionExtractor:
    EMOTION_PATTERN = re.compile(r'\((\w+)\)\s*$')
    VALID_EMOTIONS = {'neutral', 'happy', 'sad', 'surprised', 'angry', 'sleepy', 'wink', 'love',
                      'laugh', 'excited', 'confused', 'worried', 'curious', 'tired', 'playful', 'loving'}

    @classmethod
    def extract(cls, text: str) -> tuple:
        match = cls.EMOTION_PATTERN.search(text)
        if match:
            emotion = match.group(1).lower()
            clean_text = text[:match.start()].strip()
            if emotion in cls.VALID_EMOTIONS:
                return clean_text, emotion
        return text.strip(), "neutral"


class FreyaAssistant:
    def __init__(self, mic_device: int = None):
        print("\n" + "=" * 50)
        print("🤖 FREYA VOICE ASSISTANT - Initializing")
        print("=" * 50 + "\n")

        self.mic = PCMicListener(device_index=mic_device)

        logger.info("Loading Whisper STT...")
        self.stt = WhisperSTTService()
        logger.info("✅ Whisper ready")

        logger.info("Loading Gemini LLM...")
        self.llm = LLMService()
        logger.info("✅ Gemini ready")

        logger.info("Loading Piper TTS...")
        self.tts = PiperTTSService()
        logger.info("✅ Piper ready")

        self.esp32 = ESP32Bridge(
            port=settings.ESP32_COM_PORT,
            baudrate=115200
        )

        if self.esp32.connect():
            print("\n--- Hardware Test ---")
            print("🔊 Testing speaker...")
            self.esp32.play_melody()
            print("✅ Speaker OK")

            print("😊 Testing emotions...")
            for emotion in ["happy", "surprised", "love"]:
                self.esp32.send_emotion(emotion)
                time.sleep(0.5)
            self.esp32.send_emotion("neutral")
            print("✅ OLED OK")
            print("-" * 20 + "\n")

        print("✅ Freya is ready!\n")

    def listen(self) -> str:
        print("🎤 Listening... (speak now)")
        self.esp32.send_emotion("neutral")
        self.esp32.play_beep()

        audio_data = self.mic.listen()

        if not audio_data:
            return ""

        self.esp32.send_emotion("thinking")
        print("🧠 Transcribing...")

        try:
            with tempfile.NamedTemporaryFile(suffix=".wav", delete=False) as f:
                f.write(audio_data)
                temp_path = f.name

            if hasattr(self.stt, 'transcribe_file'):
                text = self.stt.transcribe_file(temp_path)
            elif hasattr(self.stt, 'transcribe'):
                text = self.stt.transcribe(temp_path)
            elif hasattr(self.stt, 'model'):
                result = self.stt.model.transcribe(temp_path)
                text = result.get("text", "").strip()
            else:
                text = ""

            os.unlink(temp_path)

            if text:
                print(f"👤 You: {text}")

            return text or ""

        except Exception as e:
            logger.error(f"Transcription error: {e}")
            return ""

    def think(self, user_input: str) -> tuple:
        print("🤔 Thinking...")
        self.esp32.send_emotion("thinking")

        response_text, emotion = self.llm.generate_response(user_input)

        if not emotion or emotion == "neutral":
            response_text, emotion = EmotionExtractor.extract(response_text)

        print(f"🤖 Freya: {response_text}")
        print(f"   [Emotion: {emotion}]")

        return response_text, emotion

    def speak(self, text: str, emotion: str):
        print("🔊 Speaking...")

        self.esp32.send_emotion(emotion)
        time.sleep(0.3)

        audio_bytes = self.tts.synthesize_to_bytes(text)

        if not audio_bytes:
            logger.error("❌ TTS failed")
            return

        logger.info(f"TTS generated {len(audio_bytes)} bytes")

        if self.esp32.connected:
            success = self.esp32.send_audio(audio_bytes)
            if not success:
                self._play_on_pc(audio_bytes)
        else:
            self._play_on_pc(audio_bytes)

        time.sleep(0.5)
        self.esp32.send_emotion("neutral")
        print("✅ Done\n")

    def _play_on_pc(self, audio_bytes: bytes):
        logger.info("Playing on PC...")
        try:
            if hasattr(self.tts, 'play_audio_bytes'):
                self.tts.play_audio_bytes(audio_bytes)
            else:
                with tempfile.NamedTemporaryFile(suffix=".wav", delete=False) as f:
                    f.write(audio_bytes)
                    temp_path = f.name
                try:
                    import pygame
                    pygame.mixer.init()
                    pygame.mixer.music.load(temp_path)
                    pygame.mixer.music.play()
                    while pygame.mixer.music.get_busy():
                        time.sleep(0.1)
                except ImportError:
                    from playsound import playsound
                    playsound(temp_path)
                os.unlink(temp_path)
        except Exception as e:
            logger.error(f"PC playback failed: {e}")

    def run(self):
        print("=" * 50)
        print("🎤 Input:  PC Microphone → Whisper")
        print("🧠 Brain:  Gemini LLM")
        print("🔊 Output: ESP32 Speaker")
        print("😊 Face:   ESP32 OLED")
        print("=" * 50)
        print("\nPress Ctrl+C to exit\n")
        print("-" * 50 + "\n")

        try:
            while True:
                user_input = self.listen()

                if not user_input or len(user_input.strip()) < 2:
                    print("⚠️ No speech detected\n")
                    self.esp32.send_emotion("neutral")
                    continue

                response_text, emotion = self.think(user_input)
                self.speak(response_text, emotion)

                print("-" * 50 + "\n")

        except KeyboardInterrupt:
            print("\n\n👋 Goodbye!")
            self.esp32.send_emotion("sleepy")
            time.sleep(1)
            self.esp32.send_emotion("neutral")
            self.esp32.disconnect()


def main():
    if "--help" in sys.argv:
        print("Usage: python main.py [--mic N] [--list-mics]")
        sys.exit(0)

    if "--list-mics" in sys.argv:
        for i, name in enumerate(sr.Microphone.list_microphone_names()):
            print(f"[{i}] {name}")
        sys.exit(0)

    mic_device = None
    if "--mic" in sys.argv:
        idx = sys.argv.index("--mic")
        if idx + 1 < len(sys.argv):
            mic_device = int(sys.argv[idx + 1])

    freya = FreyaAssistant(mic_device=mic_device)
    freya.run()


if __name__ == "__main__":
    main()