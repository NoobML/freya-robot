/*
 * ============================================
 * FREYA - Complete: Emotions + Audio Streaming
 * ============================================
 *
 * WIRING:
 *
 * OLED SSD1306 (I2C):
 *   VCC  -> 3.3V
 *   GND  -> GND
 *   SDA  -> GPIO 21
 *   SCL  -> GPIO 19
 *
 * MAX98357A Speaker (I2S):
 *   VIN  -> 5V (or 3.3V)
 *   GND  -> GND
 *   BCLK -> GPIO 26
 *   LRC  -> GPIO 25
 *   DIN  -> GPIO 22
 *
 * ============================================
 */

#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <driver/i2s.h>

// ==================== PINS ====================
#define I2C_SDA 21
#define I2C_SCL 19
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define SCREEN_ADDRESS 0x3C

#define I2S_SPKR_BCK  26
#define I2S_SPKR_WS   25
#define I2S_SPKR_DOUT 22

// ==================== SETTINGS ====================
#define PLAY_BUFFER_SIZE 80000
#define SERIAL_BAUD 115200
#define AUDIO_TIMEOUT_MS 30000

// ==================== GLOBALS ====================
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);
size_t expectedAudioSize = 0;


uint8_t* playBuffer = nullptr;
size_t playWritePos = 0;
bool audioBuffering = false;
bool speakerOK = false;
bool oledOK = false;
unsigned long audioStartTime = 0;

// Current emotion
enum Emotion { NEUTRAL=0, HAPPY=1, SAD=2, SURPRISED=3, ANGRY=4, SLEEPY=5, WINK=6, LOVE=7 };
Emotion currentEmotion = NEUTRAL;

// ==================== EMOTION DISPLAY ====================
void displayEmotion(Emotion emotion) {
  if (!oledOK) return;
  
  display.clearDisplay();
  int centerX = SCREEN_WIDTH / 2;
  
  switch (emotion) {
    case NEUTRAL:
      display.fillRoundRect(30, 20, 20, 25, 8, SSD1306_WHITE);
      display.fillRoundRect(78, 20, 20, 25, 8, SSD1306_WHITE);
      display.drawFastHLine(54, 52, 20, SSD1306_WHITE);
      break;
      
    case HAPPY:
      display.drawFastHLine(30, 28, 20, SSD1306_WHITE);
      display.drawFastHLine(78, 28, 20, SSD1306_WHITE);
      for (int i = 0; i < 5; i++) {
        display.drawPixel(30 + i, 28 - i, SSD1306_WHITE);
        display.drawPixel(49 - i, 28 - i, SSD1306_WHITE);
        display.drawPixel(78 + i, 28 - i, SSD1306_WHITE);
        display.drawPixel(97 - i, 28 - i, SSD1306_WHITE);
      }
      display.drawCircle(centerX, 48, 15, SSD1306_WHITE);
      display.fillRect(centerX - 16, 35, 33, 15, SSD1306_BLACK);
      break;
      
    case SAD:
      display.fillRoundRect(30, 25, 20, 20, 6, SSD1306_WHITE);
      display.fillRoundRect(78, 25, 20, 20, 6, SSD1306_WHITE);
      display.drawLine(28, 18, 52, 22, SSD1306_WHITE);
      display.drawLine(76, 22, 100, 18, SSD1306_WHITE);
      display.drawCircle(centerX, 62, 12, SSD1306_WHITE);
      display.fillRect(centerX - 14, 50, 29, 14, SSD1306_BLACK);
      break;
      
    case SURPRISED:
      display.fillCircle(40, 28, 14, SSD1306_WHITE);
      display.fillCircle(88, 28, 14, SSD1306_WHITE);
      display.fillCircle(40, 28, 6, SSD1306_BLACK);
      display.fillCircle(88, 28, 6, SSD1306_BLACK);
      display.drawCircle(centerX, 52, 8, SSD1306_WHITE);
      break;
      
    case ANGRY:
      display.fillRoundRect(30, 25, 22, 18, 4, SSD1306_WHITE);
      display.fillRoundRect(76, 25, 22, 18, 4, SSD1306_WHITE);
      display.fillTriangle(25, 15, 55, 22, 25, 22, SSD1306_WHITE);
      display.fillTriangle(103, 15, 73, 22, 103, 22, SSD1306_WHITE);
      display.drawRect(44, 50, 40, 10, SSD1306_WHITE);
      display.drawFastVLine(54, 50, 10, SSD1306_WHITE);
      display.drawFastVLine(64, 50, 10, SSD1306_WHITE);
      display.drawFastVLine(74, 50, 10, SSD1306_WHITE);
      break;
      
    case SLEEPY:
      display.drawFastHLine(28, 30, 24, SSD1306_WHITE);
      display.drawFastHLine(76, 30, 24, SSD1306_WHITE);
      display.drawFastHLine(30, 31, 20, SSD1306_WHITE);
      display.drawFastHLine(78, 31, 20, SSD1306_WHITE);
      display.drawCircle(centerX, 52, 6, SSD1306_WHITE);
      display.setTextSize(1);
      display.setCursor(100, 5);  display.print("Z");
      display.setCursor(105, 12); display.print("z");
      display.setCursor(110, 18); display.print("z");
      break;
      
    case WINK:
      display.fillRoundRect(30, 20, 20, 25, 8, SSD1306_WHITE);
      display.drawFastHLine(78, 32, 20, SSD1306_WHITE);
      display.drawFastHLine(80, 33, 16, SSD1306_WHITE);
      display.drawCircle(centerX, 50, 12, SSD1306_WHITE);
      display.fillRect(centerX - 14, 38, 29, 14, SSD1306_BLACK);
      break;
      
    case LOVE:
      display.fillCircle(35, 25, 8, SSD1306_WHITE);
      display.fillCircle(45, 25, 8, SSD1306_WHITE);
      display.fillTriangle(27, 28, 53, 28, 40, 45, SSD1306_WHITE);
      display.fillCircle(83, 25, 8, SSD1306_WHITE);
      display.fillCircle(93, 25, 8, SSD1306_WHITE);
      display.fillTriangle(75, 28, 101, 28, 88, 45, SSD1306_WHITE);
      display.drawCircle(centerX, 52, 10, SSD1306_WHITE);
      display.fillRect(centerX - 12, 42, 25, 12, SSD1306_BLACK);
      break;
  }
  
  display.display();
}

void setEmotion(int num) {
  if (num >= 0 && num <= 7) {
    currentEmotion = (Emotion)num;
    displayEmotion(currentEmotion);
  }
}

// ==================== SPEAKER ====================
bool initSpeaker() {
  i2s_config_t config = {
    .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX),
    .sample_rate = 22050,
    .bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT,
    .channel_format = I2S_CHANNEL_FMT_ONLY_LEFT,
    .communication_format = I2S_COMM_FORMAT_STAND_I2S,
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

void playTone(int freq, int duration_ms, int volume = 16000) {
  if (!speakerOK) return;
  int samples_per_cycle = 22050 / freq;
  int total_samples = (22050 * duration_ms) / 1000;
  int16_t buffer[128];
  size_t written;
  int idx = 0;

  for (int i = 0; i < total_samples; i++) {
    int pos = i % samples_per_cycle;
    buffer[idx++] = (pos < samples_per_cycle / 2) ? volume : -volume;
    if (idx >= 128) {
      i2s_write(I2S_NUM_0, buffer, sizeof(buffer), &written, portMAX_DELAY);
      idx = 0;
    }
  }
  if (idx > 0) {
    i2s_write(I2S_NUM_0, buffer, idx * 2, &written, portMAX_DELAY);
  }
}

void playMelody() {
  playTone(523, 150); playTone(659, 150); playTone(784, 150); playTone(1047, 300);
  i2s_zero_dma_buffer(I2S_NUM_0);
}

void playBeep() {
  playTone(800, 100);
  i2s_zero_dma_buffer(I2S_NUM_0);
}

// ==================== AUDIO PLAYBACK ====================
void playBufferedAudio() {
  if (playWritePos == 0 || !playBuffer || !speakerOK) {
    Serial.println("{\"type\":\"ERROR\",\"msg\":\"No audio data\"}");
    return;
  }

  Serial.print("{\"type\":\"PLAYING\",\"bytes\":");
  Serial.print(playWritePos);
  Serial.println("}");

  // Play audio through I2S
  size_t offset = 0;
  size_t written;
  
  while (offset < playWritePos) {
    size_t toWrite = min((size_t)512, playWritePos - offset);
    esp_err_t err = i2s_write(I2S_NUM_0, playBuffer + offset, toWrite, &written, 1000 / portTICK_PERIOD_MS);
    if (err != ESP_OK) {
      Serial.printf("{\"type\":\"ERROR\",\"msg\":\"I2S write error: %d\"}\n", err);
      break;
    }
    offset += written;
  }

  // Wait for DMA to finish and clear
  delay(100);
  i2s_zero_dma_buffer(I2S_NUM_0);

  Serial.print("{\"type\":\"DONE\",\"played\":");
  Serial.print(offset);
  Serial.println("}");
  
  playWritePos = 0;
}

// ==================== SETUP ====================
void setup() {
  Serial.setRxBufferSize(8192);  // MUST be BEFORE begin()
  Serial.begin(SERIAL_BAUD);
  Serial.setRxBufferSize(8192);
  delay(500);

  Serial.println("\n========================================");
  Serial.println("FREYA - Emotions + Audio");
  Serial.println("========================================");

  // Allocate buffer
  playBuffer = (uint8_t*)malloc(PLAY_BUFFER_SIZE);
  Serial.printf("Buffer: %s (%d bytes)\n", playBuffer ? "OK" : "FAIL", PLAY_BUFFER_SIZE);;

  // OLED
  Wire.begin(I2C_SDA, I2C_SCL);
  oledOK = display.begin(SSD1306_SWITCHCAPVCC, SCREEN_ADDRESS);
  Serial.printf("OLED: %s\n", oledOK ? "OK" : "FAIL");
  
  if (oledOK) {
    display.clearDisplay();
    display.setTextSize(2);
    display.setTextColor(SSD1306_WHITE);
    display.setCursor(10, 20);
    display.println("Freya");
    display.setCursor(10, 40);
    display.println("Ready!");
    display.display();
  }

  // Speaker
  speakerOK = initSpeaker();
  Serial.printf("Speaker: %s\n", speakerOK ? "OK" : "FAIL");
  
  if (speakerOK) {
    delay(300);
    playMelody();
  }

  delay(500);
  displayEmotion(NEUTRAL);

  Serial.println("========================================");
  Serial.println("{\"type\":\"READY\"}");
}

// ==================== MAIN LOOP ====================
// ==================== REPLACE YOUR loop() FUNCTION WITH THIS ====================

void loop() {
  // Check for audio timeout
  if (audioBuffering && (millis() - audioStartTime > AUDIO_TIMEOUT_MS)) {
    Serial.println("{\"type\":\"TIMEOUT\"}");
    audioBuffering = false;
    playWritePos = 0;
  }

  if (!Serial.available()) {
    delay(1);
    return;
  }

  if (audioBuffering) {
    // === AUDIO BUFFERING MODE ===
    // Read in bulk, not byte-by-byte
    while (Serial.available() && playWritePos < expectedAudioSize) {
      size_t available = Serial.available();
      size_t remaining = expectedAudioSize - playWritePos;
      size_t toRead = min(available, remaining);

      // Don't overflow buffer
      if (playWritePos + toRead > PLAY_BUFFER_SIZE) {
        toRead = PLAY_BUFFER_SIZE - playWritePos;
      }

      if (toRead > 0 && playBuffer) {
        size_t bytesRead = Serial.readBytes(playBuffer + playWritePos, toRead);
        playWritePos += bytesRead;
      }
    }

    // Check if we received all expected bytes
    if (playWritePos >= expectedAudioSize || playWritePos >= PLAY_BUFFER_SIZE) {
      Serial.print("{\"type\":\"BUFFERED\",\"bytes\":");
      Serial.print(playWritePos);
      Serial.println("}");

      audioBuffering = false;
      playBufferedAudio();
    }
  } else {
    // === COMMAND MODE ===
    char first = Serial.peek();

    if (first == '{') {
      String line = Serial.readStringUntil('\n');
      line.trim();

      if (line.indexOf("AUDIO_START") >= 0) {
        // Parse size from JSON: {"type":"AUDIO_START","size":12345}
        int sizeIndex = line.indexOf("\"size\":");
        if (sizeIndex >= 0) {
          expectedAudioSize = line.substring(sizeIndex + 7).toInt();
        } else {
          expectedAudioSize = PLAY_BUFFER_SIZE; // fallback
        }

        // Cap to buffer size
        if (expectedAudioSize > PLAY_BUFFER_SIZE) {
          expectedAudioSize = PLAY_BUFFER_SIZE;
        }

        playWritePos = 0;
        audioBuffering = true;
        audioStartTime = millis();

        Serial.print("{\"type\":\"AUDIO_READY\",\"expecting\":");
        Serial.print(expectedAudioSize);
        Serial.println("}");
      }
      else if (line.indexOf("TEST") >= 0) {
        playMelody();
        Serial.println("{\"type\":\"TEST_OK\"}");
      }
    }
    else if (first >= '0' && first <= '7') {
      char cmd = Serial.read();
      setEmotion(cmd - '0');
    }
    else if (first == '9') {
      Serial.read();
      playBeep();
    }
    else if (first == 'm' || first == 'M') {
      Serial.read();
      playMelody();
    }
    else if (first == '\n' || first == '\r') {
      Serial.read();
    }
    else {
      Serial.read();
    }
  }
}