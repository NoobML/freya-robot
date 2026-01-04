"""
FREYA - AI Voice Assistant with ESP32 Hardware
===============================================

Uses:
- INMP441 microphone on ESP32 for voice input
- Whisper for speech-to-text
- Gemini for AI responses
- Piper TTS for text-to-speech
- MAX98357A speaker on ESP32 for audio output
- OLED display for animated eyes
"""

import logging
import sys
import tempfile
import wave
import os
import io
import time
from pathlib import Path

# Add project root to path
sys.path.insert(0, str(Path(__file__).parent))

from services.stt_service import WhisperSTTService
from services.llm_service import LLMService
from services.tts_service import PiperTTSService
from communication.serial_handler import SerialBridge
from config import settings

logging.basicConfig(
    level=logging.INFO,
    format='%(asctime)s - %(name)s - %(levelname)s - %(message)s'
)
logger = logging.getLogger("main")


def create_wav_from_raw(raw_data: bytes, sample_rate: int = 16000) -> bytes:
    """Convert raw PCM to WAV format for Whisper"""
    wav_buffer = io.BytesIO()

    with wave.open(wav_buffer, 'wb') as wav_file:
        wav_file.setnchannels(1)  # Mono
        wav_file.setsampwidth(2)  # 16-bit
        wav_file.setframerate(sample_rate)
        wav_file.writeframes(raw_data)

    return wav_buffer.getvalue()


class FreyaAssistant:
    def __init__(self, use_esp32_mic: bool = True):
        """
        Initialize all services

        Args:
            use_esp32_mic: True to use INMP441 on ESP32, False to use PC mic
        """
        self.use_esp32_mic = use_esp32_mic

        logger.info("Initializing Freya Assistant...")

        # STT - Whisper
        self.stt = WhisperSTTService()

        # LLM - Gemini
        self.llm = LLMService()

        # TTS - Piper
        self.tts = PiperTTSService()

        # Serial Bridge - ESP32
        self.esp32 = SerialBridge(
            port=settings.ESP32_COM_PORT,
            baudrate=settings.ESP32_BAUD_RATE
        )

        # Connect to ESP32
        if self.esp32.connect():
            logger.info("✅ ESP32 connected!")
            self._test_hardware()
        else:
            logger.error("❌ ESP32 connection failed!")
            logger.error("Check that:")
            logger.error("  1. ESP32 is connected via USB")
            logger.error("  2. Correct COM port in config/settings.py")
            logger.error("  3. Serial Monitor is NOT open")
            if use_esp32_mic:
                sys.exit(1)
            else:
                logger.warning("⚠️ Continuing without ESP32 (PC mic mode)")

        logger.info("✅ Freya is ready!")

    def _test_hardware(self):
        """Test ESP32 hardware on startup"""
        print("\n--- Hardware Test ---")

        # Test speaker
        print("Testing speaker...")
        if self.esp32.test_speaker():
            print("✅ Speaker OK")
        else:
            print("⚠️ Speaker may need checking")

        time.sleep(1)

        # Test microphone (only if using ESP32 mic)
        if self.use_esp32_mic:
            print("Testing microphone...")
            amplitude = self.esp32.test_microphone()
            if amplitude is not None:
                if amplitude > 50:
                    print(f"✅ Microphone OK (amplitude: {amplitude})")
                else:
                    print(f"⚠️ Microphone low signal (amplitude: {amplitude})")
            else:
                print("⚠️ Microphone test failed")

        print("-" * 20 + "\n")

    def _transcribe_audio_file(self, filepath: str) -> str:
        """
        Transcribe audio file using Whisper
        Works with different STT service implementations
        """
        # Try different method names that might exist
        if hasattr(self.stt, 'transcribe_file'):
            return self.stt.transcribe_file(filepath)
        elif hasattr(self.stt, 'transcribe'):
            return self.stt.transcribe(filepath)
        elif hasattr(self.stt, 'transcribe_audio'):
            return self.stt.transcribe_audio(filepath)
        else:
            # Fallback: load audio and use model directly
            import whisper
            if hasattr(self.stt, 'model'):
                result = self.stt.model.transcribe(filepath)
                return result.get("text", "").strip()
            else:
                logger.error("Cannot find transcription method in STT service")
                return ""

    def listen_from_esp32(self) -> str:
        """Listen via ESP32 INMP441 microphone"""
        logger.info("🎤 Listening... (speak into ESP32 microphone)")

        # Set listening emotion
        self.esp32.send_emotion("listening")

        # Record from ESP32
        raw_audio = self.esp32.record_audio()

        if not raw_audio or len(raw_audio) < 1000:
            logger.warning("No audio captured from ESP32")
            self.esp32.send_emotion("normal")
            return ""

        logger.info(f"Received {len(raw_audio)} bytes from microphone")

        # Convert to WAV for Whisper
        wav_audio = create_wav_from_raw(raw_audio, sample_rate=16000)

        # Save to temp file
        with tempfile.NamedTemporaryFile(suffix=".wav", delete=False) as f:
            f.write(wav_audio)
            temp_path = f.name

        try:
            # Transcribe using Whisper
            self.esp32.send_emotion("thinking")
            logger.info("Transcribing audio...")

            user_text = self._transcribe_audio_file(temp_path)

            if user_text:
                logger.info(f"👤 User: {user_text}")

            return user_text or ""

        finally:
            # Clean up temp file
            try:
                os.unlink(temp_path)
            except:
                pass

    def listen_from_pc(self) -> str:
        """Listen via PC microphone"""
        logger.info("🎤 Listening... (PC microphone)")
        user_text = self.stt.listen_and_transcribe()

        if user_text:
            logger.info(f"👤 User: {user_text}")
        return user_text or ""

    def listen(self) -> str:
        """Listen for user input from configured source"""
        if self.use_esp32_mic and self.esp32.connected:
            return self.listen_from_esp32()
        else:
            return self.listen_from_pc()

    def think(self, user_input: str) -> tuple:
        """Generate response using LLM and extract emotion"""
        logger.info("🤔 Thinking...")

        if self.esp32.connected:
            self.esp32.send_emotion("thinking")

        text, emotion = self.llm.generate_response(user_input)
        logger.info(f"🤖 Freya: {text} [Emotion: {emotion}]")
        return text, emotion

    def speak(self, text: str, emotion: str):
        """Convert text to speech and play through ESP32"""
        logger.info("🔊 Speaking...")

        # Send emotion to OLED
        if self.esp32.connected:
            self.esp32.send_emotion(emotion)
            time.sleep(0.2)

        # Generate audio
        audio_bytes = self.tts.synthesize_to_bytes(text)

        if audio_bytes and self.esp32.connected:
            # Send to ESP32 speaker
            logger.info("Sending audio to ESP32...")
            success = self.esp32.send_audio_bytes(audio_bytes)

            if success:
                logger.info("✅ Audio sent to ESP32")
            else:
                logger.warning("⚠️ ESP32 audio failed, playing on PC")
                if hasattr(self.tts, 'play_audio_bytes'):
                    self.tts.play_audio_bytes(audio_bytes)
        elif audio_bytes:
            # Fallback to PC speakers
            logger.warning("ESP32 not connected, playing on PC")
            if hasattr(self.tts, 'play_audio_bytes'):
                self.tts.play_audio_bytes(audio_bytes)
        else:
            logger.error("❌ TTS failed to generate audio")

        # Return to normal expression
        if self.esp32.connected:
            self.esp32.send_emotion("normal")

        logger.info("✅ Done speaking")

    def run(self):
        """Main conversation loop"""
        print("\n" + "=" * 50)
        print("🤖 FREYA VOICE ASSISTANT")
        print("=" * 50)

        if self.use_esp32_mic:
            print("🎤 Input: ESP32 INMP441 microphone")
        else:
            print("🎤 Input: PC microphone")

        print("🔊 Output: ESP32 speaker")
        print("\nPress Ctrl+C to exit\n")

        try:
            while True:
                # Listen for user input
                user_input = self.listen()

                if not user_input or len(user_input.strip()) < 3:
                    print("⚠️  No speech detected. Listening again...\n")
                    if self.esp32.connected:
                        self.esp32.send_emotion("normal")
                    continue

                # Generate response
                response_text, emotion = self.think(user_input)

                # Speak response
                self.speak(response_text, emotion)

                print("\n" + "-" * 50 + "\n")

        except KeyboardInterrupt:
            print("\n\n👋 Shutting down Freya...")
            if self.esp32.connected:
                self.esp32.send_emotion("normal")
                self.esp32.disconnect()
            logger.info("Freya shut down gracefully")


if __name__ == "__main__":
    # Check command line args
    use_esp32_mic = True

    if len(sys.argv) > 1:
        if sys.argv[1] == "--pc-mic":
            use_esp32_mic = False
            print("Using PC microphone (--pc-mic flag)")
        elif sys.argv[1] == "--help":
            print("FREYA Voice Assistant")
            print("\nUsage:")
            print("  python main.py           # Use ESP32 INMP441 microphone")
            print("  python main.py --pc-mic  # Use PC microphone")
            sys.exit(0)

    freya = FreyaAssistant(use_esp32_mic=use_esp32_mic)
    freya.run()