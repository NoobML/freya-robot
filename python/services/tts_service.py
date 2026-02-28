import subprocess
import logging
import wave
from pathlib import Path
import pyaudio
import io
import tempfile
from config import settings

logger = logging.getLogger("tts_service")


class PiperTTSService:
    def __init__(self, piper_path: str = None, model_path: str = None):
        """
        Initialize Piper TTS

        Args:
            piper_path: Path to piper.exe (uses settings default if None)
            model_path: Path to .onnx model (uses settings default if None)
        """
        self.piper_path = Path(piper_path) if piper_path else settings.PIPER_EXECUTABLE
        self.model_path = Path(model_path) if model_path else settings.PIPER_MODEL

        # Validate files
        if not self.piper_path.exists():
            raise FileNotFoundError(f"Piper executable not found: {self.piper_path}")
        if not self.model_path.exists():
            raise FileNotFoundError(f"Model not found: {self.model_path}")

        logger.info(f"Piper TTS initialized with model: {self.model_path}")

    def synthesize(self, text: str, output_file: str = "output.wav") -> str:
        """
        Synthesize text to audio file

        Args:
            text: Text to convert
            output_file: Output filename

        Returns:
            Path to generated audio file
        """
        try:
            cmd = [
                str(self.piper_path),
                "--model", str(self.model_path),
                "--output_file", output_file
            ]

            result = subprocess.run(
                cmd,
                input=text.encode('utf-8'),
                capture_output=True,
                check=True
            )

            logger.info(f"Generated audio: {output_file}")
            return output_file

        except subprocess.CalledProcessError as e:
            logger.error(f"Piper failed: {e.stderr.decode()}")
            raise
        except Exception as e:
            logger.error(f"Synthesis error: {str(e)}")
            raise

    def synthesize_to_bytes(self, text: str) -> bytes:
        """
        Convert text to speech and return raw audio bytes (for ESP32 streaming)

        Args:
            text: Text to convert

        Returns:
            Raw WAV audio data as bytes
        """
        try:
            # Create temp file
            with tempfile.NamedTemporaryFile(suffix='.wav', delete=False) as tmp:
                temp_path = tmp.name

            # Generate to file
            cmd = [
                str(self.piper_path),
                "--model", str(self.model_path),
                "--length_scale", "0.8",  # 20% faster speech
                "--output_file", temp_path
            ]

            subprocess.run(
                cmd,
                input=text.encode('utf-8'),
                capture_output=True,
                check=True
            )

            # Read file into bytes
            with open(temp_path, 'rb') as f:
                audio_bytes = f.read()

            # Delete temp file
            Path(temp_path).unlink()

            logger.info(f"Generated {len(audio_bytes)} bytes of audio data")
            return audio_bytes

        except subprocess.CalledProcessError as e:
            logger.error(f"Piper failed: {e.stderr.decode()}")
            raise
        except Exception as e:
            logger.error(f"Synthesis error: {str(e)}")
            raise

    def play_audio_bytes(self, audio_bytes: bytes):
        """
        Play audio from bytes (works for both file and byte sources)

        Args:
            audio_bytes: Raw WAV audio data
        """
        try:
            # Wrap bytes in file-like object
            audio_stream = io.BytesIO(audio_bytes)
            wf = wave.open(audio_stream, 'rb')

            # Initialize PyAudio
            p = pyaudio.PyAudio()

            # Open audio stream
            stream = p.open(
                format=p.get_format_from_width(wf.getsampwidth()),
                channels=wf.getnchannels(),
                rate=wf.getframerate(),
                output=True
            )

            # Play audio in chunks
            CHUNK = 1024
            data = wf.readframes(CHUNK)

            while data:
                stream.write(data)
                data = wf.readframes(CHUNK)

            # Cleanup
            stream.stop_stream()
            stream.close()
            p.terminate()
            wf.close()

            logger.info(f"Played {len(audio_bytes)} bytes from memory")

        except Exception as e:
            logger.error(f"Playback error: {str(e)}")
            raise

    def play_audio(self, audio_file: str):
        """
        Play audio from file

        Args:
            audio_file: Path to audio file
        """
        try:
            with open(audio_file, 'rb') as f:
                audio_bytes = f.read()
            self.play_audio_bytes(audio_bytes)

        except Exception as e:
            logger.error(f"Playback error: {str(e)}")
            raise


if __name__ == '__main__':
    logging.basicConfig(level=logging.INFO)

    # Initialize (uses settings.py defaults)
    tts = PiperTTSService()

    # Test bytes-based synthesis and playback
    print("Testing TTS...")
    audio_bytes = tts.synthesize_to_bytes("Hello, this is a test of the Piper text to speech system.")
    tts.play_audio_bytes(audio_bytes)
    print("Done!")