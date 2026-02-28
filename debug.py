import wave
import subprocess
from pathlib import Path

# Relative paths from project root
piper_path = Path("models/piper/piper.exe")
model_path = Path("models/piper/models/en_US-amy-medium.onnx")

# Verify files exist
if not piper_path.exists():
    raise FileNotFoundError(f"Piper not found at: {piper_path}")
if not model_path.exists():
    raise FileNotFoundError(f"Model not found at: {model_path}")

# Generate test audio
result = subprocess.run([
    str(piper_path),
    "--model", str(model_path),
    "--output_file", "test_audio.wav"
], input="Hello test", capture_output=True, text=True)

# Check if subprocess succeeded
if result.returncode != 0:
    print("Piper failed:")
    print(result.stderr)
    exit(1)

# Check the format
with wave.open("test_audio.wav", 'rb') as wf:
    print(f"Sample Rate:   {wf.getframerate()} Hz")
    print(f"Channels:      {wf.getnchannels()}")
    print(f"Sample Width:  {wf.getsampwidth() * 8} bits")
    print(f"Total Frames:  {wf.getnframes()}")
    print(f"Duration:      {wf.getnframes() / wf.getframerate():.2f} seconds")
