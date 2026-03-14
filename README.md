# 🤖 FREYA - AI Voice Assistant Robot

A fully-featured AI voice assistant powered by ESP32, featuring expressive OLED eyes, emotional servo movements, and natural voice interactions.

![Version](https://img.shields.io/badge/version-1.0-blue)
![Platform](https://img.shields.io/badge/platform-ESP32-green)
![License](https://img.shields.io/badge/license-MIT-yellow)

## ✨ Features

- 🎤 **Voice Recognition** - Whisper STT on PC microphone
- 🧠 **AI Brain** - Google Gemini 2.5 Flash (ultra-concise 2-5 word responses)
- 🔊 **Text-to-Speech** - Piper TTS with natural voice
- 👀 **Expressive Eyes** - Animated OLED display with 9 emotions
- 💃 **Body Language** - Dual servo motors with emotion-based movements
- 📡 **Wireless** - Serial communication over USB (ESP32 ↔ PC)

---

## 📋 Table of Contents

- [Hardware Requirements](#-hardware-requirements)
- [Wiring Diagram](#-wiring-diagram)
- [Software Requirements](#-software-requirements)
- [Installation](#-installation)
- [Configuration](#-configuration)
- [Usage](#-usage)
- [Emotion Mapping](#-emotion-mapping)
- [Troubleshooting](#-troubleshooting)
- [Project Structure](#-project-structure)
- [Credits](#-credits)

---

## 🛠 Hardware Requirements

### Core Components

| Component | Model/Type | Quantity | Notes |
|-----------|------------|----------|-------|
| **Microcontroller** | ESP32 WROOM-32 (30-pin) | 1 | Main brain |
| **Display** | SSD1306 OLED (128x64, I2C) | 1 | For animated eyes |
| **Speaker Amplifier** | MAX98357A I2S | 1 | 3.2W audio output |
| **Speaker** | 4Ω 3W+ | 1 | Louder than 8Ω |
| **Servo Motors** | SG90 (360° continuous) | 2 | Emotion movements |
| **Power** | USB Power Bank (5V 2A+) | 1 | Powers everything |
| **Microphone** | PC Microphone | 1 | For voice input |

### Wiring & Accessories

- USB cables (Type-A for power, appropriate cable for ESP32)
- Jumper wires (male-to-male, male-to-female)
- Breadboard (optional, for prototyping)
- 100µF-1000µF capacitor (optional, for stable power)

### Optional Enhancements

- 100KΩ resistor (for MAX98357A GAIN pin - maximum volume)
- External enclosure/robot body
- LED indicators

---

## 🔌 Wiring Diagram

### Power Distribution

```
USB Power Bank (5V output)
├── RED wire (+5V) → 5V Rail
└── BLACK wire (GND) → GND Rail

5V Rail connects to:
├── MAX98357A VIN
├── Servo 1 RED wire
├── Servo 2 RED wire
└── ESP32 VIN pin

GND Rail connects to:
├── MAX98357A GND
├── Servo 1 BROWN wire
├── Servo 2 BROWN wire
├── OLED GND
└── ESP32 GND
```

### Detailed Component Wiring

#### 🖥️ OLED Display (SSD1306)
```
OLED Pin  →  ESP32 Pin
─────────────────────────
VCC       →  3.3V
GND       →  GND
SDA       →  GPIO 21
SCL       →  GPIO 19
```

#### 🔊 Speaker Amplifier (MAX98357A)
```
MAX98357A Pin  →  Connection
──────────────────────────────
VIN            →  5V Rail
GND            →  GND Rail
BCLK           →  ESP32 GPIO 26
LRC (LRCLK)    →  ESP32 GPIO 25
DIN            →  ESP32 GPIO 22
SD             →  Not connected (or tie to GND)
GAIN           →  VDD (5V) for 12dB boost ⭐
               →  OR 100KΩ to GND for 15dB (MAX) ⭐⭐

Speaker Outputs:
+ (positive)   →  Speaker +
- (negative)   →  Speaker -
```

> **💡 Volume Tip:** Connect GAIN pin to VDD (5V) for much louder audio!

#### 🤖 Servo Motors (SG90 - 360° Continuous Rotation)
```
Servo 1 (Left Motor):
BROWN  (GND)    →  GND Rail
RED    (Power)  →  5V Rail
ORANGE (Signal) →  ESP32 GPIO 13

Servo 2 (Right Motor):
BROWN  (GND)    →  GND Rail
RED    (Power)  →  5V Rail
ORANGE (Signal) →  ESP32 GPIO 14
```

#### 🔋 ESP32 Power
```
ESP32 Pin  →  Connection
────────────────────────
VIN        →  5V Rail
GND        →  GND
```

### Complete Pin Summary

| ESP32 GPIO | Connected To | Function |
|------------|--------------|----------|
| **GPIO 13** | Servo 1 Signal | Left motor control |
| **GPIO 14** | Servo 2 Signal | Right motor control |
| **GPIO 19** | OLED SCL | I2C Clock |
| **GPIO 21** | OLED SDA | I2C Data |
| **GPIO 22** | MAX98357A DIN | I2S Audio Data |
| **GPIO 25** | MAX98357A LRC | I2S Word Select |
| **GPIO 26** | MAX98357A BCLK | I2S Bit Clock |
| **VIN** | 5V Rail | Power input |
| **3.3V** | OLED VCC | Display power |
| **GND** | GND Rail | Common ground |

### Visual Wiring Diagram

```
                    ┌─────────────────┐
                    │  USB Power Bank │
                    │    (5V 2A+)     │
                    └────┬───────┬────┘
                         │       │
                    5V Rail    GND Rail
                         │       │
         ┌───────────────┼───────┼──────────────┐
         │               │       │              │
    ┌────▼────┐     ┌───▼───────▼───┐     ┌───▼────┐
    │ ESP32   │     │   MAX98357A   │     │ Servos │
    │         │     │   (Speaker    │     │  x2    │
    │ GPIO 13 ├─────┤    Amp)       │     │        │
    │ GPIO 14 ├─┐   │               │     └────────┘
    │ GPIO 22 ├─┼───┤ DIN    Speaker├─────> 🔊
    │ GPIO 25 ├─┼───┤ LRC           │
    │ GPIO 26 ├─┼───┤ BCLK          │
    │ GPIO 21 ├─┼─┐ │ GAIN → VDD ⭐ │
    │ GPIO 19 ├─┼─┼─┤               │
    │         │ │ │ └───────────────┘
    │  3.3V   ├─┼─┼─┐
    │   GND   ├─┼─┼─┼──> GND Rail
    │   VIN   ├─┼─┼─┘
    └─────────┘ │ │
                │ │   ┌──────────────┐
                │ └───┤ OLED Display │
                │     │  (SSD1306)   │
                └─────┤ SDA  👀      │
                      │ SCL          │
                      └──────────────┘

        PC Microphone 🎤 ──> USB ──> PC
```

---

## 💻 Software Requirements

### PC/Laptop Requirements

- **OS:** Windows 10/11, Linux, or macOS
- **Python:** 3.8 or higher
- **RAM:** 8GB+ (16GB recommended for Whisper)
- **Storage:** 5GB free space (for models)
- **GPU:** Optional (CUDA for faster Whisper)

### Python Dependencies

```bash
pip install pyaudio
pip install openai-whisper
pip install torch torchvision torchaudio  # For Whisper
pip install google-generativeai
pip install pyserial
```

### ESP32 Development Environment

- **PlatformIO** (recommended) or Arduino IDE
- ESP32 board support package
- Libraries (auto-installed via `platformio.ini`):
  - Adafruit GFX Library
  - Adafruit SSD1306
  - ArduinoJson
  - ESP32Servo

---

## 📥 Installation

### 1. Clone Repository

```bash
git clone https://github.com/yourusername/freya-robot.git
cd freya-robot
```

### 2. Install Python Dependencies

```bash
# Create virtual environment (recommended)
python -m venv venv

# Activate virtual environment
# Windows:
venv\Scripts\activate
# Linux/Mac:
source venv/bin/activate

# Install requirements
pip install -r requirements.txt
```

### 3. Download AI Models

#### Whisper STT Model
```python
import whisper
model = whisper.load_model("small.en")  # Downloads automatically on first run
```

#### Piper TTS Model
1. Download Piper from: https://github.com/rhasspy/piper/releases
2. Download voice model (e.g., `en_US-lessac-medium.onnx`)
3. Place in `models/piper/` directory

### 4. Setup ESP32 Firmware

```bash
# Navigate to ESP32 project folder
cd esp32

# Upload firmware using PlatformIO
pio run --target upload

# Or using Arduino IDE:
# 1. Open src/main.cpp
# 2. Select Board: "ESP32 Dev Module"
# 3. Select Port: COM4 (your port)
# 4. Click Upload
```

### 5. Verify Wiring

Double-check all connections match the [Wiring Diagram](#-wiring-diagram) before powering on!

---

## ⚙️ Configuration

### 1. Python Configuration (`config/settings.py`)

```python
# ===== API Keys =====
# Add your Google Gemini API keys to python/api/API_KEYS.py
API_KEYS = [
    "your-api-key-1",
    "your-api-key-2",
]

# ===== STT Settings =====
WHISPER_MODEL = "small.en"       # tiny, base, small, medium, large
WHISPER_DEVICE = "cuda"          # "cuda" for GPU, "cpu" for CPU
MICROPHONE_INDEX = 1             # Your PC mic device index

# ===== TTS Settings =====
PIPER_EXECUTABLE = MODELS_DIR / "piper" / "piper.exe"
PIPER_MODEL = MODELS_DIR / "piper" / "models" / "en_US-lessac-medium.onnx"

# ===== ESP32 Settings =====
ESP32_COM_PORT = "COM4"          # Change to your ESP32 port
ESP32_BAUD_RATE = 921600
```

### 2. Find Your Microphone Index

```python
import pyaudio
p = pyaudio.PyAudio()
for i in range(p.get_device_count()):
    dev = p.get_device_info_by_index(i)
    if dev['maxInputChannels'] > 0:
        print(f"{i}: {dev['name']}")
```

### 3. Get Gemini API Key

1. Visit: https://aistudio.google.com/apikey
2. Create API key
3. Add to `python/api/API_KEYS.py`

### 4. Adjust Volume (if needed)

In `python/main.py`:
```python
# Line ~XX
success = self.esp32.send_audio(audio_bytes, gain=10.0)  # 1.0 to 15.0
```

### 5. Adjust Servo Speed (if needed)

In `esp32/src/main.cpp`:
```cpp
// Lines 40-46
#define SLOW_CW 95      // Decrease = slower (e.g., 93)
#define MEDIUM_CW 105   // Decrease = slower (e.g., 100)
#define FAST_CW 120     // Decrease = slower (e.g., 110)
```

---

## 🚀 Usage

### Starting FREYA

```bash
# Make sure ESP32 is connected and powered
# Activate virtual environment if using one
cd python
python main.py

# Specify different COM port
python main.py --port COM5

# Run hardware diagnostics only
python main.py --test
```

### Interaction Flow

1. **FREYA starts** → Eyes open, servos test
2. **"Listening..."** → Speak into PC microphone (5 seconds)
3. **"Thinking..."** → Gemini processes your speech
4. **"Speaking..."** → FREYA responds with emotion!
   - Eyes change expression
   - Servos perform emotion movement
   - Speaker plays TTS response

### Example Interaction

```
You: "Hello Freya!"
FREYA: "Hey there! (happy)"
       [Eyes: happy expression, Servos: excited wiggle]

You: "How are you?"
FREYA: "Doing great! (happy)"
       [Eyes: smiling, Servos: gentle sway]

You: "Tell me a joke"
FREYA: "Why cross road? (thinking)"
       [Eyes: contemplative, Servos: tilting motion]
```

### Stopping FREYA

Press `Ctrl+C` in terminal
- FREYA will show sleepy eyes
- Servos will stop
- Clean disconnect

---

## 😊 Emotion Mapping

FREYA has 9 emotions with unique eye expressions and servo movements:

| Emotion | Eye Expression | Servo Movement | Duration |
|---------|----------------|----------------|----------|
| **happy** | Smiling eyes + laugh animation | Excited wiggle (opposite directions) | 2.0s |
| **sad** | Droopy tired eyes | Slow synchronized droop | 3.0s |
| **surprised** | Wide open eyes (42x42px) | Quick jolt (opposite directions) | 0.8s |
| **angry** | Angry eyes + shaking | Aggressive rapid alternations | ~2.4s |
| **sleepy** | Half-closed tired eyes | Slow lazy yawn motion | 2.5s |
| **wink** | Left eye closed, right open | Playful single servo twitch | 0.6s |
| **love** | Happy smiling eyes | Gentle synchronized sway | 2.5s |
| **thinking** | Eyes alternating size | Contemplative tilt (alternating) | ~2.0s |
| **neutral** | Round default eyes | No movement (stopped) | - |

### Triggering Emotions

Emotions are automatically detected from Gemini's responses:
```
Gemini output: "I love that idea! (love)"
                                   └─ triggers love emotion
```

Or manually via Python:
```python
from communication.serial_handler import SerialBridge
bridge = SerialBridge("COM4")
bridge.connect()
bridge.send_emotion("happy")  # Triggers eyes + servos
```

---

## 🐛 Troubleshooting

### ESP32 Connection Issues

**Problem:** "Failed to connect to ESP32"

**Solutions:**
- Check USB cable is data-capable (not charge-only)
- Verify COM port: Device Manager (Windows) or `ls /dev/tty*` (Linux)
- Try different USB port
- Press ESP32 reset button
- Check baud rate matches (921600)

### No Audio Output

**Problem:** Speaker silent or very quiet

**Solutions:**

1. **Software:**
   - Test speaker: `python main.py --test`
   - Increase gain in `main.py`: `gain=10.0` (or higher)
   - Check TTS is generating audio (check file sizes)

2. **Hardware:**
   - Verify MAX98357A GAIN pin → VDD (5V) connection ⭐
   - Check 5V power to speaker amp
   - Test with different speaker
   - Verify I2S wiring (GPIO 22, 25, 26)
   - Add 100µF capacitor across VIN/GND

3. **Debugging:**
   ```python
   bridge.test_speaker()  # Should hear loud beep
   ```

### Servo Issues

**Problem:** Servo spinning continuously

**Solution:**
- Re-upload firmware (detach() command stops servos)
- Check servo is genuine SG90 (clones behave differently)
- Verify 5V power to servos
- Servos should stop after emotion duration

**Problem:** Servo jittery when stopped

**Solution:**
- Normal for attached servos
- Firmware uses `detach()` to prevent this
- If persists, add 100µF capacitor across servo power

**Problem:** Servo too fast/slow

**Solution:**
Edit `esp32/src/main.cpp`:
```cpp
#define FAST_CW 120  // Increase = faster, Decrease = slower
```

### OLED Display Issues

**Problem:** Eyes not showing / blank screen

**Solutions:**
- Verify I2C address is 0x3C (check with I2C scanner)
- Check SDA/SCL connections (GPIO 21/19)
- Verify OLED VCC to 3.3V (NOT 5V!)
- Re-upload firmware
- Serial monitor should show "READY"

### Whisper/Microphone Issues

**Problem:** "No speech detected"

**Solutions:**
- Check microphone index in `settings.py`
- Test mic in Windows Sound Settings
- Speak louder and closer to mic
- Increase recording duration in `settings.py`
- Check microphone permissions

**Problem:** Transcription very slow

**Solution:**
- Use smaller model: `WHISPER_MODEL = "tiny"` or `"base"`
- Use GPU if available: `WHISPER_DEVICE = "cuda"`
- Reduce recording duration

### Power Issues

**Problem:** System resets/crashes randomly

**Solutions:**
- Use 2A+ power bank (not 1A)
- Don't power servos from ESP32 3.3V
- Add capacitor (470µF-1000µF) across power rails
- Check for loose connections
- Power ESP32 and peripherals separately if needed

---

## 📁 Project Structure

```
freya-robot/
├── esp32/                          # ESP32 Firmware
│   ├── src/
│   │   ├── main.cpp               # Main firmware code
│   │   └── FluxGarage_RoboEyes.h  # OLED eye animation library
│   ├── platformio.ini             # PlatformIO config
│   └── README.md
│
├── python/                         # PC-side Python code
│   ├── api/
│   │   └── API_KEYS.py            # Gemini API keys
│   ├── config/
│   │   └── settings.py            # Configuration
│   ├── services/
│   │   ├── stt_service.py         # Whisper STT
│   │   ├── llm_service.py         # Gemini LLM
│   │   └── tts_service.py         # Piper TTS
│   ├── communication/
│   │   └── serial_handler.py      # ESP32 serial communication
│   ├── main.py                    # Main entry point
│   └── requirements.txt
│
├── models/                         # AI Models
│   ├── piper/
│   │   ├── piper.exe
│   │   └── models/
│   │       └── en_US-lessac-medium.onnx
│   └── whisper/                   # Auto-downloaded on first run
│
├── docs/                           # Documentation
│   ├── WIRING.md
│   ├── SETUP.md
│   ├── VOLUME_FIX.md
│   └── TROUBLESHOOTING.md
│
├── .gitignore
├── LICENSE
└── README.md                       # This file
```

---
---

## 🎯 Technical Specifications

### Performance

- **Response Time:** ~3-5 seconds (speech → response)
- **STT Accuracy:** ~95% (Whisper small.en model)
- **TTS Quality:** Natural human-like voice (Piper)
- **Emotion Transitions:** Smooth 30 FPS animations
- **Servo Response:** < 50ms from command

### Audio Specifications

- **Speaker Output:** Up to 3.2W @ 4Ω
- **Sample Rate:** 22050 Hz (TTS), 16000 Hz (STT)
- **Bit Depth:** 16-bit
- **Channels:** Mono
- **Frequency Response:** 20Hz - 20kHz

### Power Consumption

| Component | Current Draw | Notes |
|-----------|--------------|-------|
| ESP32 | ~80-240mA | Peaks during WiFi (not used here) |
| OLED | ~20mA | Average |
| MAX98357A | ~100-500mA | Depends on volume |
| 2x Servos | ~200-800mA | Depends on load/speed |
| **Total** | ~400-1760mA | **Use 2A+ power supply** |

### Memory Usage (ESP32)

- **Flash:** ~800KB / 4MB
- **RAM:** ~180KB / 320KB
- **Heap Free:** ~140KB

---

## 🔄 Future Enhancements

### Planned Features

- [ ] Wake word detection ("Hey FREYA")
- [ ] Offline mode (local Whisper + Gemma)
- [ ] Battery level indicator
- [ ] Custom emotion programming
- [ ] Multi-language support
- [ ] WebSocket control interface
- [ ] Bluetooth audio streaming
- [ ] Additional servo movements
- [ ] LED mood lighting
- [ ] Touch sensors for interaction

### Hardware Upgrades

- [ ] ESP32-S3 (for better audio processing)
- [ ] I2S microphone (remove PC dependency)
- [ ] Larger OLED (128x128 or color display)
- [ ] More servos (4-8 for complex movements)
- [ ] IMU sensor (head tracking, balance)
- [ ] 3D printed enclosure

---

## 🤝 Contributing

Contributions are welcome! Please follow these steps:

1. Fork the repository
2. Create feature branch (`git checkout -b feature/AmazingFeature`)
3. Commit changes (`git commit -m 'Add AmazingFeature'`)
4. Push to branch (`git push origin feature/AmazingFeature`)
5. Open Pull Request

### Coding Standards

- **ESP32:** Follow Arduino/C++ style guide
- **Python:** Follow PEP 8
- **Comments:** Document complex logic
- **Testing:** Test on hardware before PR

---

## 📜 License

This project is licensed under the MIT License - see [LICENSE](LICENSE) file for details.

---

## 🙏 Credits

### Libraries & Frameworks

- [FluxGarage RoboEyes](https://github.com/fluxgarage/RoboEyes) - OLED eye animation library
- [Whisper](https://github.com/openai/whisper) - Speech recognition by OpenAI
- [Piper](https://github.com/rhasspy/piper) - Fast, local neural TTS
- [Google Gemini](https://ai.google.dev/) - LLM for conversation
- [ESP32Servo](https://github.com/madhephaestus/ESP32Servo) - Servo control
- [Adafruit GFX](https://github.com/adafruit/Adafruit-GFX-Library) - Graphics library
- [Adafruit SSD1306](https://github.com/adafruit/Adafruit_SSD1306) - OLED driver

### Inspiration

- Boston Dynamics Spot
- Anki Vector
- DIY robotics community

### Special Thanks

- FluxGarage for the amazing RoboEyes library
- OpenAI for Whisper
- Rhasspy community for Piper TTS
- Anthropic Claude for development assistance

---

## 📧 Contact & Support

- **Issues:** [GitHub Issues](https://github.com/yourusername/freya-robot/issues)
- **Discussions:** [GitHub Discussions](https://github.com/yourusername/freya-robot/discussions)
- **Email:** your.email@example.com

---

## 🌟 Star History

[![Star History Chart](https://api.star-history.com/svg?repos=yourusername/freya-robot&type=Date)](https://star-history.com/#yourusername/freya-robot&Date)

---

## 📸 Gallery

### Hardware Setup

![Hardware Setup](docs/images/hardware.jpg)
*Complete wiring setup with all components*

### FREYA in Action

![Happy Expression](docs/images/happy.jpg)
*Happy emotion with excited servo movement*

![Thinking Expression](docs/images/thinking.jpg)
*Thinking emotion with contemplative pose*

---
---
<div align="center">


If you found this project helpful, please consider giving it a ⭐!

[Report Bug](https://github.com/NoobML/freya-robot/issues) • 
[Request Feature](https://github.com/yourusername/freya-robot/issues) • 
[Documentation](https://github.com/yourusername/freya-robot/wiki)

</div>
