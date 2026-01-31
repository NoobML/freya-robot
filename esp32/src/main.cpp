/*
 * ============================================
 * FREYA - RoboEyes + Real-time Audio Streaming
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
 *   VIN  -> 5V
 *   GND  -> GND
 *   BCLK -> GPIO 26
 *   LRC  -> GPIO 25
 *   DIN  -> GPIO 22
 *
 * ============================================
 */

/*
 * FREYA - Working Audio + RoboEyes
 */

#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <driver/i2s.h>

#define I2C_SDA 21
#define I2C_SCL 19
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define SCREEN_ADDRESS 0x3C

#define I2S_SPKR_BCK  26
#define I2S_SPKR_WS   25
#define I2S_SPKR_DOUT 22

#define PLAY_BUFFER_SIZE 80000
#define SERIAL_BAUD 115200
#define AUDIO_TIMEOUT_MS 30000

Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);

#include "FluxGarage_RoboEyes.h"
roboEyes eyes;

uint8_t* playBuffer = nullptr;
size_t playWritePos = 0;
size_t expectedAudioSize = 0;
bool audioBuffering = false;
bool speakerOK = false;
bool oledOK = false;
unsigned long audioStartTime = 0;
unsigned long lastEyeUpdate = 0;

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

void playTone(int freq, int duration_ms) {
  if (!speakerOK) return;
  int samples_per_cycle = 22050 / freq;
  int total_samples = (22050 * duration_ms) / 1000;
  int16_t buffer[128];
  size_t written;
  int idx = 0;

  for (int i = 0; i < total_samples; i++) {
    buffer[idx++] = ((i % samples_per_cycle) < samples_per_cycle/2) ? 16000 : -16000;
    if (idx >= 128) {
      i2s_write(I2S_NUM_0, buffer, sizeof(buffer), &written, portMAX_DELAY);
      idx = 0;
    }
  }
  if (idx > 0) i2s_write(I2S_NUM_0, buffer, idx * 2, &written, portMAX_DELAY);
  i2s_zero_dma_buffer(I2S_NUM_0);
}

void playMelody() {
  playTone(523, 150); playTone(659, 150); playTone(784, 150); playTone(1047, 300);
}

void playBufferedAudio() {
  if (playWritePos == 0 || !playBuffer || !speakerOK) return;

  Serial.printf("{\"type\":\"PLAYING\",\"bytes\":%d}\n", playWritePos);

  size_t offset = 0;
  size_t written;
  while (offset < playWritePos) {
    size_t toWrite = min((size_t)512, playWritePos - offset);
    i2s_write(I2S_NUM_0, playBuffer + offset, toWrite, &written, 1000 / portTICK_PERIOD_MS);
    offset += written;
  }

  delay(100);
  i2s_zero_dma_buffer(I2S_NUM_0);

  Serial.printf("{\"type\":\"DONE\",\"played\":%d}\n", offset);
  playWritePos = 0;
}

// ==================== EMOTIONS ====================
void setEmotion(int num) {
  eyes.tired = false; eyes.angry = false; eyes.happy = false;
  eyes.idle = false; eyes.thinking = false;
  
  switch (num) {
    case 0: eyes.idle = true; eyes.autoblinker = true; eyes.setMood(DEFAULT); break;
    case 1: eyes.happy = true; eyes.autoblinker = true; eyes.setMood(HAPPY); break;
    case 2: eyes.tired = true; eyes.autoblinker = true; eyes.setMood(TIRED); break;
    case 3: eyes.autoblinker = false; eyes.setMood(DEFAULT); eyes.open(); break;
    case 4: eyes.angry = true; eyes.autoblinker = false; eyes.setMood(ANGRY); break;
    case 5: eyes.tired = true; eyes.autoblinker = true; eyes.setMood(TIRED); break;
    case 6: eyes.happy = true; eyes.setMood(HAPPY); eyes.blink(1, 0); break;
    case 7: eyes.happy = true; eyes.autoblinker = true; eyes.setMood(HAPPY); break;
    case 8: eyes.thinking = true; eyes.autoblinker = true; break;
    case 9: eyes.idle = false; eyes.autoblinker = true; break;
    default: eyes.idle = true; eyes.autoblinker = true; eyes.setMood(DEFAULT); break;
  }
}

void updateEyes() {
  if (millis() - lastEyeUpdate >= 20) {
    eyes.update();
    lastEyeUpdate = millis();
  }
}

// ==================== SETUP ====================
void setup() {
  Serial.setRxBufferSize(8192);
  Serial.begin(SERIAL_BAUD);
  delay(500);

  Serial.println("\n========================================");
  Serial.println("FREYA - Audio + RoboEyes");
  Serial.println("========================================");

  playBuffer = (uint8_t*)malloc(PLAY_BUFFER_SIZE);
  Serial.printf("Buffer: %s (%d bytes)\n", playBuffer ? "OK" : "FAIL", PLAY_BUFFER_SIZE);

  Wire.begin(I2C_SDA, I2C_SCL);
  oledOK = display.begin(SSD1306_SWITCHCAPVCC, SCREEN_ADDRESS);
  Serial.printf("OLED: %s\n", oledOK ? "OK" : "FAIL");
  
  if (oledOK) {
    eyes.begin(SCREEN_WIDTH, SCREEN_HEIGHT, 50);
    eyes.setAutoblinker(ON, 3, 2);
    eyes.setIdleMode(ON, 2, 3);
    eyes.open();
  }

  speakerOK = initSpeaker();
  Serial.printf("Speaker: %s\n", speakerOK ? "OK" : "FAIL");
  
  if (speakerOK) { delay(300); playMelody(); }

  setEmotion(0);
  Serial.println("{\"type\":\"READY\"}");
}

// ==================== LOOP ====================
void loop() {
  if (!audioBuffering) updateEyes();

  if (audioBuffering && (millis() - audioStartTime > AUDIO_TIMEOUT_MS)) {
    Serial.println("{\"type\":\"TIMEOUT\"}");
    audioBuffering = false;
    playWritePos = 0;
  }

  if (!Serial.available()) { delay(1); return; }

  if (audioBuffering) {
    while (Serial.available() && playWritePos < expectedAudioSize) {
      size_t available = Serial.available();
      size_t remaining = expectedAudioSize - playWritePos;
      size_t toRead = min(available, remaining);
      if (playWritePos + toRead > PLAY_BUFFER_SIZE) toRead = PLAY_BUFFER_SIZE - playWritePos;
      if (toRead > 0 && playBuffer) {
        playWritePos += Serial.readBytes(playBuffer + playWritePos, toRead);
      }
    }
    if (playWritePos >= expectedAudioSize || playWritePos >= PLAY_BUFFER_SIZE) {
      Serial.printf("{\"type\":\"BUFFERED\",\"bytes\":%d}\n", playWritePos);
      audioBuffering = false;
      playBufferedAudio();
    }
  } else {
    char first = Serial.peek();
    if (first == '{') {
      String line = Serial.readStringUntil('\n');
      line.trim();
      if (line.indexOf("AUDIO_START") >= 0) {
        int idx = line.indexOf("\"size\":");
        expectedAudioSize = (idx >= 0) ? line.substring(idx + 7).toInt() : PLAY_BUFFER_SIZE;
        if (expectedAudioSize > PLAY_BUFFER_SIZE) expectedAudioSize = PLAY_BUFFER_SIZE;
        playWritePos = 0;
        audioBuffering = true;
        audioStartTime = millis();
        Serial.printf("{\"type\":\"AUDIO_READY\",\"expecting\":%d}\n", expectedAudioSize);
      }
    }
    else if (first >= '0' && first <= '9') { setEmotion(Serial.read() - '0'); }
    else if (first == 'm') { Serial.read(); playMelody(); }
    else { Serial.read(); }
  }
}