/*
 * ============================================
 * FREYA - Complete AI Voice Assistant
 * ESP32 + OLED + Speaker + INMP441 Microphone
 * ============================================
 *
 * WIRING CONFIGURATION:
 *
 * === OLED Display (SSD1306 128x64) ===
 * VCC  -> 3.3V
 * GND  -> GND
 * SDA  -> GPIO 21
 * SCL  -> GPIO 19
 *
 * === Speaker Amplifier (MAX98357A) ===
 * VIN  -> 3.3V
 * GND  -> GND
 * BCLK -> GPIO 26
 * LRC  -> GPIO 25
 * DIN  -> GPIO 22
 *
 * === Microphone (INMP441) ===
 * VDD  -> 3.3V
 * GND  -> GND
 * SCK  -> GPIO 14
 * WS   -> GPIO 15
 * SD   -> GPIO 32
 * L/R  -> GND (left channel)
 *
 * === PlatformIO Settings (platformio.ini) ===
 * [env:esp32dev]
 * platform = espressif32
 * board = esp32dev
 * framework = arduino
 * monitor_speed = 921600
 * lib_deps =
 *     adafruit/Adafruit GFX Library
 *     adafruit/Adafruit SSD1306
 *     bblanchon/ArduinoJson
 *
 * ============================================
 */

#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <ArduinoJson.h>
#include <driver/i2s.h>

// ==================== PIN CONFIGURATION ====================
// OLED
#define I2C_SDA 21
#define I2C_SCL 19
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET -1
#define SCREEN_ADDRESS 0x3C

// Speaker (MAX98357A) - Uses I2S_NUM_0
#define I2S_SPKR_BCK  26
#define I2S_SPKR_WS   25
#define I2S_SPKR_DOUT 22

// Microphone (INMP441) - Uses I2S_NUM_1
#define I2S_MIC_SCK   14
#define I2S_MIC_WS    15
#define I2S_MIC_SD    32

// Recording settings
#define SAMPLE_RATE 16000
#define RECORD_SECONDS 5
#define BUFFER_SIZE (SAMPLE_RATE * RECORD_SECONDS * 2)  // 16-bit = 2 bytes per sample

// Audio buffers
uint8_t* recordBuffer = nullptr;
uint8_t* playBuffer = nullptr;
#define PLAY_BUFFER_SIZE 350000

size_t playWritePos = 0;
bool isRecording = false;
bool isPlaying = false;
bool audioBuffering = false;

Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

// ==================== EYE VARIABLES ====================
int eyeWidthDefault = 36;
int eyeHeightDefault = 36;
int spaceBetween = 10;
int borderRadius = 8;

int eyeLwidth = 36, eyeLheight = 1;
int eyeRwidth = 36, eyeRheight = 1;
int eyeLwidthTarget = 36, eyeLheightTarget = 36;
int eyeRwidthTarget = 36, eyeRheightTarget = 36;
int eyeLx, eyeLy, eyeRx, eyeRy;

int tiredLidHeight = 0, tiredLidTarget = 0;
int angryLidHeight = 0, angryLidTarget = 0;
int happyBottomOffset = 0, happyBottomTarget = 0;

bool eyesOpen = true;
int currentMood = 0;

unsigned long lastFrame = 0;
unsigned long lastBlink = 0;
unsigned long blinkInterval = 3000;

// ==================== I2S SPEAKER FUNCTIONS ====================
bool initSpeaker() {
  i2s_config_t config = {
    .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX),
    .sample_rate = 22050,
    .bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT,
    .channel_format = I2S_CHANNEL_FMT_ONLY_LEFT,
    .communication_format = I2S_COMM_FORMAT_I2S,
    .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,
    .dma_buf_count = 8,
    .dma_buf_len = 512,
    .use_apll = false,
    .tx_desc_auto_clear = true,
    .fixed_mclk = 0
  };

  i2s_pin_config_t pins = {
    .bck_io_num = I2S_SPKR_BCK,
    .ws_io_num = I2S_SPKR_WS,
    .data_out_num = I2S_SPKR_DOUT,
    .data_in_num = I2S_PIN_NO_CHANGE
  };

  if (i2s_driver_install(I2S_NUM_0, &config, 0, NULL) != ESP_OK) return false;
  if (i2s_set_pin(I2S_NUM_0, &pins) != ESP_OK) return false;
  return true;
}

// ==================== I2S MICROPHONE FUNCTIONS ====================
bool initMicrophone() {
  i2s_config_t config = {
    .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_RX),
    .sample_rate = SAMPLE_RATE,
    .bits_per_sample = I2S_BITS_PER_SAMPLE_32BIT,  // INMP441 outputs 32-bit
    .channel_format = I2S_CHANNEL_FMT_ONLY_LEFT,
    .communication_format = I2S_COMM_FORMAT_I2S,
    .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,
    .dma_buf_count = 8,
    .dma_buf_len = 512,
    .use_apll = false,
    .tx_desc_auto_clear = false,
    .fixed_mclk = 0
  };

  i2s_pin_config_t pins = {
    .bck_io_num = I2S_MIC_SCK,
    .ws_io_num = I2S_MIC_WS,
    .data_out_num = I2S_PIN_NO_CHANGE,
    .data_in_num = I2S_MIC_SD
  };

  if (i2s_driver_install(I2S_NUM_1, &config, 0, NULL) != ESP_OK) return false;
  if (i2s_set_pin(I2S_NUM_1, &pins) != ESP_OK) return false;
  return true;
}

void recordAudio() {
  if (!recordBuffer) {
    Serial.println("{\"type\":\"ERROR\",\"msg\":\"No record buffer\"}");
    return;
  }

  isRecording = true;
  setMood(5);  // Listening/thinking mood

  // Show recording on display
  display.clearDisplay();
  display.setTextSize(2);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(5, 25);
  display.println("Listening..");
  display.display();

  Serial.println("{\"type\":\"RECORDING\"}");

  // Record audio
  size_t totalBytes = 0;
  size_t bytesRead = 0;
  int32_t tempBuffer[512];

  // Clear I2S buffer
  i2s_zero_dma_buffer(I2S_NUM_1);
  delay(100);

  unsigned long startTime = millis();
  unsigned long recordDuration = RECORD_SECONDS * 1000;

  while (millis() - startTime < recordDuration && totalBytes < BUFFER_SIZE) {
    // Read 32-bit samples from INMP441
    i2s_read(I2S_NUM_1, tempBuffer, sizeof(tempBuffer), &bytesRead, portMAX_DELAY);

    // Convert 32-bit to 16-bit and store
    int samples = bytesRead / 4;  // 32-bit = 4 bytes
    for (int i = 0; i < samples && totalBytes < BUFFER_SIZE - 2; i++) {
      // INMP441 data is in upper 24 bits, we take upper 16
      int16_t sample = (int16_t)(tempBuffer[i] >> 14);

      recordBuffer[totalBytes++] = sample & 0xFF;
      recordBuffer[totalBytes++] = (sample >> 8) & 0xFF;
    }
  }

  isRecording = false;

  Serial.print("{\"type\":\"RECORDED\",\"bytes\":");
  Serial.print(totalBytes);
  Serial.println("}");

  // Send audio data to Python
  Serial.println("{\"type\":\"AUDIO_DATA_START\"}");

  // Send in chunks
  size_t offset = 0;
  while (offset < totalBytes) {
    size_t chunkSize = min((size_t)512, totalBytes - offset);
    Serial.write(recordBuffer + offset, chunkSize);
    offset += chunkSize;
    delay(5);  // Small delay to prevent buffer overflow
  }

  Serial.println();
  Serial.println("{\"type\":\"AUDIO_DATA_END\"}");

  setMood(0);  // Back to normal
}

// ==================== SOUND FUNCTIONS ====================
void playTone(int freq, int duration_ms) {
  int samples_per_cycle = 22050 / freq;
  int total_samples = (22050 * duration_ms) / 1000;
  int16_t buffer[256];
  size_t written;
  int idx = 0;

  for (int i = 0; i < total_samples; i++) {
    int pos = i % samples_per_cycle;
    buffer[idx++] = (pos < samples_per_cycle / 2) ? 16000 : -16000;
    if (idx >= 256) {
      i2s_write(I2S_NUM_0, buffer, sizeof(buffer), &written, portMAX_DELAY);
      idx = 0;
    }
  }
  if (idx > 0) {
    i2s_write(I2S_NUM_0, buffer, idx * 2, &written, portMAX_DELAY);
  }
  delay(30);
}

void playStartupMelody() {
  playTone(523, 150);
  playTone(659, 150);
  playTone(784, 150);
  playTone(1047, 300);
  i2s_zero_dma_buffer(I2S_NUM_0);
}

void playBeep() {
  playTone(800, 50);
  i2s_zero_dma_buffer(I2S_NUM_0);
}

void playBufferedAudio() {
  if (playWritePos == 0) {
    Serial.println("{\"type\":\"ERROR\",\"msg\":\"No audio\"}");
    return;
  }

  isPlaying = true;

  Serial.print("{\"type\":\"PLAYING\",\"bytes\":");
  Serial.print(playWritePos);
  Serial.println("}");

  size_t offset = 0;
  size_t written;

  while (offset < playWritePos) {
    size_t toWrite = min((size_t)1024, playWritePos - offset);
    i2s_write(I2S_NUM_0, playBuffer + offset, toWrite, &written, portMAX_DELAY);
    offset += written;
  }

  delay(100);
  i2s_zero_dma_buffer(I2S_NUM_0);

  isPlaying = false;

  Serial.print("{\"type\":\"AUDIO_DONE\",\"played\":");
  Serial.print(offset);
  Serial.println("}");

  playWritePos = 0;
}

// ==================== EYE FUNCTIONS ====================
void calculatePositions() {
  eyeLx = (SCREEN_WIDTH - (eyeWidthDefault + spaceBetween + eyeWidthDefault)) / 2;
  eyeLy = (SCREEN_HEIGHT - eyeHeightDefault) / 2;
  eyeRx = eyeLx + eyeWidthDefault + spaceBetween;
  eyeRy = eyeLy;
}

void drawEyes() {
  // Smooth transitions
  eyeLwidth = (eyeLwidth + eyeLwidthTarget) / 2;
  eyeLheight = (eyeLheight + eyeLheightTarget) / 2;
  eyeRwidth = (eyeRwidth + eyeRwidthTarget) / 2;
  eyeRheight = (eyeRheight + eyeRheightTarget) / 2;

  tiredLidHeight = (tiredLidHeight + tiredLidTarget) / 2;
  angryLidHeight = (angryLidHeight + angryLidTarget) / 2;
  happyBottomOffset = (happyBottomOffset + happyBottomTarget) / 2;

  if (eyesOpen) {
    if (eyeLheight < 3) eyeLheightTarget = eyeHeightDefault;
    if (eyeRheight < 3) eyeRheightTarget = eyeHeightDefault;
  }

  int eyeLyDraw = eyeLy + (eyeHeightDefault - eyeLheight) / 2;
  int eyeRyDraw = eyeRy + (eyeHeightDefault - eyeRheight) / 2;

  display.clearDisplay();

  // Main eyes
  display.fillRoundRect(eyeLx, eyeLyDraw, eyeLwidth, eyeLheight, borderRadius, SSD1306_WHITE);
  display.fillRoundRect(eyeRx, eyeRyDraw, eyeRwidth, eyeRheight, borderRadius, SSD1306_WHITE);

  // Tired eyelids
  if (tiredLidHeight > 0) {
    display.fillTriangle(eyeLx, eyeLyDraw-1, eyeLx+eyeLwidth, eyeLyDraw-1, eyeLx, eyeLyDraw+tiredLidHeight, SSD1306_BLACK);
    display.fillTriangle(eyeRx, eyeRyDraw-1, eyeRx+eyeRwidth, eyeRyDraw-1, eyeRx+eyeRwidth, eyeRyDraw+tiredLidHeight, SSD1306_BLACK);
  }

  // Angry eyelids
  if (angryLidHeight > 0) {
    display.fillTriangle(eyeLx, eyeLyDraw-1, eyeLx+eyeLwidth, eyeLyDraw-1, eyeLx+eyeLwidth, eyeLyDraw+angryLidHeight, SSD1306_BLACK);
    display.fillTriangle(eyeRx, eyeRyDraw-1, eyeRx+eyeRwidth, eyeRyDraw-1, eyeRx, eyeRyDraw+angryLidHeight, SSD1306_BLACK);
  }

  // Happy bottom
  if (happyBottomOffset > 0) {
    display.fillRoundRect(eyeLx-1, eyeLyDraw+eyeLheight-happyBottomOffset+1, eyeLwidth+2, eyeHeightDefault, borderRadius, SSD1306_BLACK);
    display.fillRoundRect(eyeRx-1, eyeRyDraw+eyeRheight-happyBottomOffset+1, eyeRwidth+2, eyeHeightDefault, borderRadius, SSD1306_BLACK);
  }

  display.display();
}

void blink() {
  eyeLheightTarget = 1;
  eyeRheightTarget = 1;
  eyesOpen = true;
}

void setMood(int mood) {
  currentMood = mood;
  tiredLidTarget = 0;
  angryLidTarget = 0;
  happyBottomTarget = 0;
  eyeLwidthTarget = eyeWidthDefault;
  eyeLheightTarget = eyeHeightDefault;
  eyeRwidthTarget = eyeWidthDefault;
  eyeRheightTarget = eyeHeightDefault;

  switch (mood) {
    case 1: happyBottomTarget = eyeHeightDefault/2; break;  // Happy
    case 2: tiredLidTarget = eyeHeightDefault/2; break;     // Sad
    case 3: angryLidTarget = eyeHeightDefault/2; break;     // Angry
    case 4:  // Surprised
      eyeLwidthTarget = eyeWidthDefault + 8;
      eyeLheightTarget = eyeHeightDefault + 8;
      eyeRwidthTarget = eyeWidthDefault + 8;
      eyeRheightTarget = eyeHeightDefault + 8;
      break;
    case 5:  // Listening/Thinking
      eyeLwidthTarget = eyeWidthDefault + 4;
      eyeLheightTarget = eyeHeightDefault + 4;
      eyeRwidthTarget = eyeWidthDefault + 4;
      eyeRheightTarget = eyeHeightDefault + 4;
      break;
  }
}

void setEmotion(String emotion) {
  emotion.toLowerCase();

  if (emotion == "happy" || emotion == "joy" || emotion == "laugh") setMood(1);
  else if (emotion == "sad" || emotion == "crying" || emotion == "tired") setMood(2);
  else if (emotion == "angry" || emotion == "mad") setMood(3);
  else if (emotion == "surprised" || emotion == "shocked") setMood(4);
  else if (emotion == "confused" || emotion == "thinking" || emotion == "listening") setMood(5);
  else setMood(0);

  Serial.print("{\"type\":\"ACK\",\"emotion\":\"");
  Serial.print(emotion);
  Serial.println("\"}");
}

void updateEyes() {
  if (isRecording || isPlaying) return;

  unsigned long now = millis();
  if (now - lastFrame >= 20) {
    lastFrame = now;
    drawEyes();
  }

  if (currentMood == 0 && now - lastBlink >= blinkInterval) {
    blink();
    lastBlink = now;
    blinkInterval = 2000 + random(3000);
  }
}

// ==================== SETUP ====================
void setup() {
  Serial.setRxBufferSize(16384);
  Serial.begin(921600);
  delay(1000);

  Serial.println("{\"type\":\"DEBUG\",\"msg\":\"Starting Freya...\"}");

  // Allocate buffers
  recordBuffer = (uint8_t*)malloc(BUFFER_SIZE);
  playBuffer = (uint8_t*)malloc(PLAY_BUFFER_SIZE);

  if (!recordBuffer || !playBuffer) {
    Serial.println("{\"type\":\"ERROR\",\"msg\":\"Buffer allocation failed\"}");
  }

  // Init I2C & OLED
  Wire.begin(I2C_SDA, I2C_SCL);

  if (!display.begin(SSD1306_SWITCHCAPVCC, SCREEN_ADDRESS)) {
    Serial.println("{\"type\":\"ERROR\",\"msg\":\"OLED failed\"}");
    while(1);
  }

  // Startup screen
  display.clearDisplay();
  display.setTextSize(2);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(20, 5);
  display.println("Freya");
  display.setCursor(30, 25);
  display.println("is");
  display.setCursor(15, 45);
  display.println("Ready!");
  display.display();

  // Init Speaker
  if (initSpeaker()) {
    Serial.println("{\"type\":\"DEBUG\",\"msg\":\"Speaker OK\"}");
    delay(500);
    playStartupMelody();
  } else {
    Serial.println("{\"type\":\"ERROR\",\"msg\":\"Speaker failed\"}");
  }

  // Init Microphone
  if (initMicrophone()) {
    Serial.println("{\"type\":\"DEBUG\",\"msg\":\"Microphone OK\"}");
  } else {
    Serial.println("{\"type\":\"ERROR\",\"msg\":\"Microphone failed\"}");
  }

  delay(1500);
  calculatePositions();
  eyesOpen = true;
  lastBlink = millis();
  lastFrame = millis();

  Serial.println("{\"type\":\"READY\"}");
}

// ==================== MAIN LOOP ====================
void loop() {
  if (Serial.available()) {
    if (!audioBuffering) {
      String msg = Serial.readStringUntil('\n');
      msg.trim();

      if (msg.length() > 0 && msg[0] == '{') {
        JsonDocument doc;
        if (deserializeJson(doc, msg) == DeserializationError::Ok) {
          const char* type = doc["type"];

          if (strcmp(type, "EMOTION") == 0) {
            setEmotion(doc["value"].as<String>());
          }
          else if (strcmp(type, "RECORD") == 0) {
            playBeep();
            delay(200);
            recordAudio();
          }
          else if (strcmp(type, "AUDIO_START") == 0) {
            playWritePos = 0;
            audioBuffering = true;
            Serial.println("{\"type\":\"AUDIO_READY\"}");
          }
          else if (strcmp(type, "BLINK") == 0) {
            blink();
            Serial.println("{\"type\":\"ACK\"}");
          }
          else if (strcmp(type, "TEST_SOUND") == 0) {
            playStartupMelody();
            Serial.println("{\"type\":\"ACK\"}");
          }
          else if (strcmp(type, "TEST_MIC") == 0) {
            // Quick 1 second test
            Serial.println("{\"type\":\"MIC_TEST_START\"}");
            int32_t samples[256];
            size_t bytesRead;
            i2s_read(I2S_NUM_1, samples, sizeof(samples), &bytesRead, 1000);

            int32_t maxVal = 0;
            for (int i = 0; i < 256; i++) {
              int32_t val = abs(samples[i] >> 14);
              if (val > maxVal) maxVal = val;
            }

            Serial.print("{\"type\":\"MIC_TEST_RESULT\",\"max_amplitude\":");
            Serial.print(maxVal);
            Serial.println("}");
          }
        }
      }
    }
    else {
      // Audio buffering
      while (Serial.available()) {
        uint8_t buf[2048];
        size_t avail = Serial.available();
        size_t toRead = min(avail, sizeof(buf));
        size_t len = Serial.readBytes(buf, toRead);

        if (len > 0 && len < 64 && buf[0] == '{') {
          String check = String((char*)buf, len);
          if (check.indexOf("AUDIO_END") >= 0) {
            audioBuffering = false;
            Serial.print("{\"type\":\"BUFFERED\",\"size\":");
            Serial.print(playWritePos);
            Serial.println("}");
            playBufferedAudio();
            return;
          }
        }

        if (playBuffer && playWritePos + len <= PLAY_BUFFER_SIZE) {
          memcpy(playBuffer + playWritePos, buf, len);
          playWritePos += len;
        }
      }
    }
  }

  updateEyes();
}