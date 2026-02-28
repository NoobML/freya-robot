from pathlib import Path

# ===== Project Paths =====
PROJECT_ROOT = Path(__file__).parent.parent.parent  # Goes up to freya-robot/
MODELS_DIR = PROJECT_ROOT / "models"


# ===== LLM Settings =====
LLM_MODEL = "gemini-2.5-flash"

# IMPORTANT: Keep this SHORT. Long system prompts eat into Gemini's context
# and the model tends to ignore complex multi-rule instructions.
# The emotion list MUST match what the ESP32 firmware actually supports.
SYSTEM_PROMPT = (
    "You are Freya, a cute small robot assistant. "
    "RULES: Reply in 1-2 short sentences only. Be warm and friendly. "
    "No emojis. No lists. No made-up facts about yourself. "
    "End every reply with ONE emotion in parentheses. "
    "Valid emotions: happy, sad, surprised, angry, sleepy, wink, love, neutral, thinking. "
    "Example: I'm doing great, thanks! (happy)"
)

# Conversation memory
MAX_CONVERSATION_TOKENS = 4000

# ===== TTS Settings (Piper) =====
PIPER_EXECUTABLE = MODELS_DIR / "piper" / "piper.exe"
PIPER_MODEL = MODELS_DIR / "piper" / "models" / "en_US-lessac-medium.onnx"

# ===== STT Settings (Whisper) =====
WHISPER_MODEL = "small.en"       # Options: tiny, base, small, medium, large
WHISPER_DEVICE = "cuda"          # "cuda" for GPU, "cpu" for CPU
MICROPHONE_INDEX = 1             # Your Redragon mic device index
WHISPER_LANGUAGE = "en"
WHISPER_TEMPERATURE = 0.0

# ===== Audio Settings =====
RECORDING_DURATION = 5           # seconds
SAMPLE_RATE = 16000              # Hz (matches Whisper's expected input)

# ===== ESP32 Settings =====
ESP32_COM_PORT = "COM4"
ESP32_BAUD_RATE = 921600         # Must match firmware SERIAL_BAUD