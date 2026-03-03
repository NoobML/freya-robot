"""
FREYA — AI Voice Assistant (ESP32 Only)
========================================

Full pipeline on ESP32 hardware:
  ESP32 INMP441 Mic → Serial → Whisper STT → Gemini LLM → Piper TTS → Serial → ESP32 Speaker

No PC microphone. No PC speaker fallback. ESP32 only.

Usage:
  python main.py                # default
  python main.py --port COM5    # specify COM port
  python main.py --test         # hardware diagnostics only
"""

import logging
import sys
import time
import re
import tempfile
import os
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))

logging.basicConfig(
    level=logging.INFO,
    format='%(asctime)s [%(name)s] %(levelname)s: %(message)s'
)
logger = logging.getLogger("freya")


class EmotionExtractor:
    """Extracts emotion tags from LLM responses: 'Hello! (happy)' -> ('Hello!', 'happy')"""

    PATTERN = re.compile(r'\((\w+)\)\s*$')
    VALID = {
        'neutral', 'happy', 'sad', 'surprised', 'angry', 'sleepy',
        'wink', 'love', 'thinking'
    }

    @classmethod
    def extract(cls, text: str) -> tuple:
        match = cls.PATTERN.search(text)
        if match:
            emotion = match.group(1).lower()
            if emotion in cls.VALID:
                return text[:match.start()].strip(), emotion
        return text.strip(), "neutral"


class FreyaAssistant:
    def __init__(self, esp_port: str = None):
        print("\n" + "=" * 50)
        print("   FREYA — AI Voice Assistant (ESP32 Only)")
        print("=" * 50 + "\n")

        # ---- Import services ----
        from services.stt_service import WhisperSTTService
        from services.llm_service import LLMService
        from services.tts_service import PiperTTSService
        from communication.serial_handler import SerialBridge
        from config import settings

        # ---- Whisper STT ----
        logger.info("Loading Whisper...")
        self.stt = WhisperSTTService()
        logger.info("Whisper ready")

        # ---- Gemini LLM ----
        logger.info("Loading Gemini...")
        self.llm = LLMService()
        logger.info("Gemini ready")

        # ---- Piper TTS ----
        logger.info("Loading Piper TTS...")
        self.tts = PiperTTSService()
        logger.info("Piper ready")

        # ---- ESP32 Connection ----
        port = esp_port or getattr(settings, 'ESP32_COM_PORT', 'COM4')
        self.esp32 = SerialBridge(port=port)

        if not self.esp32.connect():
            logger.error("Failed to connect to ESP32!")
            print("\nCould not connect to ESP32. Check:")
            print("  1. ESP32 is plugged in via USB")
            print("  2. Correct COM port (use --port COMx)")
            print("  3. No other program has the port open")
            print("  4. New firmware (main.cpp) is flashed")
            sys.exit(1)

        # ---- Hardware test ----
        self._run_hardware_test()
        print("\nFreya is ready!\n")

    def _run_hardware_test(self):
        print("--- Hardware Check ---")

        # Speaker
        print("  Speaker: ", end="", flush=True)
        if self.esp32.test_speaker():
            print("OK")
        else:
            print("No response (check firmware)")

        # Eyes & Servos - quick animation
        print("  Eyes:    ", end="", flush=True)
        for e in ["happy", "surprised", "neutral"]:
            self.esp32.send_emotion(e)
            time.sleep(0.5)
        print("OK")

        print("  Servos:  ", end="", flush=True)
        self.esp32.send_emotion("wink")
        time.sleep(1)
        self.esp32.stop_servos()
        print("OK")

        print("-" * 22)

    # ==================== LISTEN (PC MICROPHONE ONLY) ====================

    def listen(self) -> str:
        """Record from PC microphone and transcribe with Whisper."""
        print("Listening... (speak into PC mic)")
        self.esp32.send_emotion("thinking")

        # Record from PC microphone
        text = self.stt.listen_and_transcribe()

        if text:
            print(f"You: {text}")
        else:
            print("(no speech detected)")

        return text or ""

    # ==================== THINK ====================

    def think(self, user_input: str) -> tuple:
        """Send to Gemini and extract response + emotion."""
        print("Thinking...")
        self.esp32.send_emotion("thinking")

        response_text, llm_emotion = self.llm.generate_response(user_input)

        # Double-check emotion extraction
        if not llm_emotion or llm_emotion == "neutral":
            response_text, llm_emotion = EmotionExtractor.extract(response_text)

        print(f"Freya: {response_text}")
        print(f"  [{llm_emotion}]")

        return response_text, llm_emotion

    # ==================== SPEAK (ESP32 SPEAKER ONLY) ====================

    def speak(self, text: str, emotion: str):
        """Synthesize speech and play on ESP32 speaker."""
        print("Speaking...")
        self.esp32.send_emotion(emotion)
        time.sleep(0.3)

        audio_bytes = self.tts.synthesize_to_bytes(text)
        if not audio_bytes:
            logger.error("TTS failed — no audio generated")
            self.esp32.send_emotion("neutral")
            return

        logger.info(f"TTS: {len(audio_bytes)} bytes")

        # Send audio to ESP32 speaker
        success = self.esp32.send_audio(audio_bytes, gain=5.0)  # Increased from 3.0 to 5.0
        if not success:
            logger.error("ESP32 playback failed!")

        time.sleep(0.3)
        self.esp32.send_emotion("neutral")
        print("Done\n")

    # ==================== MAIN LOOP ====================

    def run(self):
        print("=" * 50)
        print("  Mic:    PC Microphone (Whisper STT)")
        print("  Brain:  Gemini LLM")
        print("  Voice:  Piper TTS -> ESP32 Speaker")
        print("  Face:   ESP32 OLED Eyes")
        print("  Body:   ESP32 Servo Motors")
        print("=" * 50)
        print("\nPress Ctrl+C to exit\n")

        try:
            while True:
                user_input = self.listen()

                if not user_input or len(user_input.strip()) < 2:
                    print("(no speech detected)\n")
                    self.esp32.send_emotion("neutral")
                    continue

                response_text, emotion = self.think(user_input)
                self.speak(response_text, emotion)
                print("-" * 40 + "\n")

        except KeyboardInterrupt:
            print("\n\nGoodbye!")
            self.esp32.send_emotion("sleepy")
            time.sleep(1)
            self.esp32.send_emotion("neutral")
            self.esp32.disconnect()


# ==================== CLI ====================

def main():
    import argparse

    parser = argparse.ArgumentParser(description="FREYA Voice Assistant (ESP32 Only)")
    parser.add_argument("--port", type=str, default=None, help="ESP32 COM port (e.g. COM4)")
    parser.add_argument("--test", action="store_true", help="Run hardware diagnostics only")

    args = parser.parse_args()

    if args.test:
        from communication.serial_handler import SerialBridge
        from config import settings
        port = args.port or getattr(settings, 'ESP32_COM_PORT', 'COM4')
        bridge = SerialBridge(port)
        if bridge.connect():
            print(f"\nConnected at {bridge.baudrate} baud\n")
            print("Speaker test...")
            bridge.test_speaker()
            time.sleep(1)
            print("\nEmotion test...")
            for e in ["happy", "sad", "surprised", "angry", "love", "neutral"]:
                print(f"  {e}")
                bridge.send_emotion(e)
                time.sleep(2)
            bridge.disconnect()
        else:
            print("Connection failed!")
        return

    freya = FreyaAssistant(esp_port=args.port)
    freya.run()


if __name__ == "__main__":
    main()