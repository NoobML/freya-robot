import whisper
import pyaudio
import wave
import logging
from pathlib import Path
import tempfile
from config import settings

logger = logging.getLogger("stt_service")


class WhisperSTTService:
    def __init__(self, model_name: str = None, device: str = None):
        """
        Initialize Whisper STT

        Args:
            model_name: Whisper model size (overrides settings)
            device: "cpu" or "cuda" (overrides settings)
        """
        self.model_name = model_name or settings.WHISPER_MODEL
        self.device = device or settings.WHISPER_DEVICE
        self.sample_rate = settings.SAMPLE_RATE

        logger.info(f"Loading Whisper model: {self.model_name}")
        self.model = whisper.load_model(self.model_name, device=self.device)
        logger.info(f"Whisper STT initialized with {self.model_name} model on {self.device}")

    def record_audio(self, duration: int = None, device_index: int = None) -> str:
        """
        Record audio from microphone

        Args:
            duration: Recording duration in seconds (uses settings default if None)
            device_index: Microphone device index (uses settings default if None)

        Returns:
            Path to recorded audio file
        """
        duration = duration or settings.RECORDING_DURATION
        device_index = device_index or settings.MICROPHONE_INDEX

        CHUNK = 1024
        FORMAT = pyaudio.paInt16
        CHANNELS = 1

        p = pyaudio.PyAudio()

        stream = p.open(
            format=FORMAT,
            channels=CHANNELS,
            rate=self.sample_rate,
            input=True,
            input_device_index=device_index,
            frames_per_buffer=CHUNK
        )

        logger.info(f"Recording for {duration} seconds...")
        frames = []

        for _ in range(0, int(self.sample_rate / CHUNK * duration)):
            data = stream.read(CHUNK)
            frames.append(data)

        stream.stop_stream()
        stream.close()
        p.terminate()

        # Save to temp file
        temp_file = tempfile.NamedTemporaryFile(delete=False, suffix='.wav')
        wf = wave.open(temp_file.name, 'wb')
        wf.setnchannels(CHANNELS)
        wf.setsampwidth(p.get_sample_size(FORMAT))
        wf.setframerate(self.sample_rate)
        wf.writeframes(b''.join(frames))
        wf.close()

        logger.info(f"Audio saved to {temp_file.name}")
        return temp_file.name

    def transcribe(self, audio_file: str, language: str = None) -> str:
        """
        Transcribe audio file using Whisper

        Args:
            audio_file: Path to audio file
            language: Language code (uses settings default if None)

        Returns:
            Transcribed text
        """
        try:
            language = language or settings.WHISPER_LANGUAGE
            logger.info("Transcribing audio...")

            result = self.model.transcribe(
                audio_file,
                language=language,
                fp16=False,
                temperature=settings.WHISPER_TEMPERATURE,
                no_speech_threshold=0.6,
                logprob_threshold=-1.0,
                compression_ratio_threshold=2.4
            )

            transcription = result["text"].strip()
            logger.info(f"Transcription: {transcription}")
            return transcription

        except Exception as e:
            logger.error(f"Transcription error: {str(e)}")
            return ""

    def listen_and_transcribe(self, duration: int = None, device_index: int = None) -> str:
        """
        Record and transcribe in one step

        Args:
            duration: Recording duration (uses settings default if None)
            device_index: Microphone index (uses settings default if None)

        Returns:
            Transcribed text
        """
        audio_file = self.record_audio(duration, device_index)

        # Check file size
        file_size = Path(audio_file).stat().st_size
        logger.info(f"Audio file size: {file_size} bytes")

        if file_size < 1000:
            logger.error("Audio file too small - microphone not recording!")
            Path(audio_file).unlink(missing_ok=True)
            return ""

        transcription = self.transcribe(audio_file)
        Path(audio_file).unlink(missing_ok=True)
        return transcription


if __name__ == "__main__":
    logging.basicConfig(level=logging.INFO)

    # List available microphones
    p = pyaudio.PyAudio()
    print("\nAvailable input devices:")
    for i in range(p.get_device_count()):
        dev = p.get_device_info_by_index(i)
        if dev['maxInputChannels'] > 0:
            print(f"{i}: {dev['name']}")
    p.terminate()

    # Initialize STT (uses settings.py defaults)
    stt = WhisperSTTService()

    # Test
    print("\n🎤 Listening... Speak now!")
    text = stt.listen_and_transcribe()
    print(f"\nYou said: {text}")