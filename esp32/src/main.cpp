/*
 * FREYA ROBOT - ESP32 Firmware with RoboEyes + Servos
 * ====================================================
 * Hardware:
 *   - OLED (SSD1306): SDA=21, SCL=19
 *   - Speaker (MAX98357A): LRC=25, BCLK=26, DIN=22
 *   - Servo 1: GPIO 13
 *   - Servo 2: GPIO 14
 *   - NO microphone (PC handles audio input)
 */

#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <driver/i2s.h>
#include <ESP32Servo.h>

// Create display object BEFORE including RoboEyes
Adafruit_SSD1306 display(128, 64, &Wire, -1);

// RoboEyes uses DEFAULT macro which conflicts with Arduino.h
// Undefine Arduino's DEFAULT and use RoboEyes version
#ifdef DEFAULT
#undef DEFAULT
#endif

// Include RoboEyes library (it will use our 'display' object)
#include "FluxGarage_RoboEyes.h"
roboEyes eyes;

// ===== OLED Display =====
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_SDA 21
#define OLED_SCL 19

// ===== Speaker (I2S) =====
#define I2S_SPEAKER_LRC  25
#define I2S_SPEAKER_BCLK 26
#define I2S_SPEAKER_DIN  22
#define SAMPLE_RATE 22050

// ===== Servos =====
#define SERVO1_PIN 13
#define SERVO2_PIN 14
Servo servo1;
Servo servo2;

// Servo speed values (continuous rotation)
#define STOP 90
#define SLOW_CW 95      // Was 100 - now slower
#define MEDIUM_CW 105   // Was 120 - now slower
#define FAST_CW 120     // Was 180 - now slower
#define SLOW_CCW 85     // Was 80 - now slower
#define MEDIUM_CCW 75   // Was 60 - now slower
#define FAST_CCW 60     // Was 0 - now slower

// ===== Serial Settings =====
#define SERIAL_BAUD 921600

// ===== State Variables =====
String currentEmotion = "neutral";
unsigned long servoStopTime = 0;
bool servoActive = false;

// ===== Function Declarations =====
void setup_i2s_speaker();
void setup_oled();
void setup_servos();
void set_eyes_emotion(String emotion);
void handle_serial_command();
void play_test_beep();
void receive_and_play_audio(int expected_size);
void execute_servo_emotion(String emotion);
void stop_servos();

// ===== SETUP =====
void setup() {
  Serial.begin(SERIAL_BAUD);
  while (!Serial) delay(10);

  // Initialize I2C for OLED
  Wire.begin(OLED_SDA, OLED_SCL);
  
  setup_oled();
  setup_i2s_speaker();
  setup_servos();

  // Show startup message
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(10, 28);
  display.println("FREYA READY!");
  display.display();
  delay(1000);

  // Initialize with neutral eyes
  eyes.open();
  set_eyes_emotion("neutral");

  Serial.println("READY");
}

// ===== MAIN LOOP =====
void loop() {
  // Update eye animations
  eyes.drawEyes();

  // Handle incoming serial commands
  if (Serial.available()) {
    handle_serial_command();
  }

  // Auto-stop servos after duration
  if (servoActive && millis() > servoStopTime) {
    stop_servos();
  }
}

// ===== OLED SETUP =====
void setup_oled() {
  if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    Serial.println("ERROR:OLED_INIT_FAILED");
    while (1);
  }
  display.clearDisplay();
  display.display();
  
  // Initialize RoboEyes with 30 FPS
  eyes.begin(SCREEN_WIDTH, SCREEN_HEIGHT, 30);
  
  // Enable auto-blinking
  eyes.setAutoblinker(ON);
  
  // Enable idle mode - eyes look around randomly
  eyes.setIdleMode(ON);
}

// ===== SERVO SETUP =====
void setup_servos() {
  servo1.attach(SERVO1_PIN);
  servo2.attach(SERVO2_PIN);
  
  // CRITICAL: Immediately stop servos on startup
  servo1.write(STOP);
  servo2.write(STOP);
  delay(100);
  
  // Detach to prevent jitter when not in use
  servo1.detach();
  servo2.detach();
}

// ===== I2S SPEAKER SETUP =====
void setup_i2s_speaker() {
  i2s_config_t i2s_config = {
    .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX),
    .sample_rate = SAMPLE_RATE,
    .bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT,
    .channel_format = I2S_CHANNEL_FMT_ONLY_LEFT,
    .communication_format = I2S_COMM_FORMAT_STAND_I2S,
    .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,
    .dma_buf_count = 8,
    .dma_buf_len = 1024,
    .use_apll = false,
    .tx_desc_auto_clear = true,
    .fixed_mclk = 0
  };

  i2s_pin_config_t pin_config = {
    .bck_io_num = I2S_SPEAKER_BCLK,
    .ws_io_num = I2S_SPEAKER_LRC,
    .data_out_num = I2S_SPEAKER_DIN,
    .data_in_num = I2S_PIN_NO_CHANGE
  };

  i2s_driver_install(I2S_NUM_0, &i2s_config, 0, NULL);
  i2s_set_pin(I2S_NUM_0, &pin_config);
  i2s_zero_dma_buffer(I2S_NUM_0);
}

// ===== SERIAL COMMAND HANDLER =====
void handle_serial_command() {
  String command = Serial.readStringUntil('\n');
  command.trim();

  if (command.startsWith("EMOTION:")) {
    String emotion = command.substring(8);
    emotion.toLowerCase();
    currentEmotion = emotion;
    set_eyes_emotion(emotion);
    execute_servo_emotion(emotion);
    Serial.println("OK:EMOTION");

  } else if (command.startsWith("SERVO:")) {
    String emotion = command.substring(6);
    emotion.toLowerCase();
    execute_servo_emotion(emotion);
    Serial.println("OK:SERVO");

  } else if (command.startsWith("AUDIO:")) {
    int size = command.substring(6).toInt();
    receive_and_play_audio(size);

  } else if (command == "TEST:SPEAKER") {
    play_test_beep();
    Serial.println("OK:SPEAKER");

  } else if (command == "STOP:SERVOS") {
    stop_servos();
    Serial.println("OK:SERVO_STOP");
  }
}

// ===== ROBOEYES EMOTION CONTROL =====
void set_eyes_emotion(String emotion) {
  // Reset all states first
  eyes.setMood(DEFAULT);
  eyes.thinking = false;
  eyes.laugh = false;
  eyes.confused = false;
  eyes.defaultAnimation = true;
  
  if (emotion == "happy") {
    eyes.setMood(HAPPY);
    eyes.anim_laugh();
    
  } else if (emotion == "sad") {
    eyes.setMood(TIRED);
    
  } else if (emotion == "surprised") {
    eyes.open();
    eyes.setWidth(42, 42);   // Both eyes
    eyes.setHeight(42, 42);  // Both eyes
    
  } else if (emotion == "angry") {
    eyes.setMood(ANGRY);
    eyes.confused = true;
    
  } else if (emotion == "sleepy") {
    eyes.setMood(TIRED);
    eyes.close();
    
  } else if (emotion == "wink") {
    eyes.setMood(HAPPY);
    eyes.close(true, false);  // Close left eye, keep right open
    
  } else if (emotion == "love") {
    eyes.setMood(HAPPY);
    
  } else if (emotion == "thinking") {
    eyes.thinking = true;
    
  } else {  // neutral or unknown
    eyes.setMood(DEFAULT);
    eyes.open();
  }
}

// ===== SERVO EMOTION MOVEMENTS =====
void execute_servo_emotion(String emotion) {
  // Attach servos before use
  if (!servo1.attached()) servo1.attach(SERVO1_PIN);
  if (!servo2.attached()) servo2.attach(SERVO2_PIN);

  if (emotion == "happy") {
    // Excited wiggle dance!
    servo1.write(FAST_CW);
    servo2.write(FAST_CCW);
    servoStopTime = millis() + 2000;
    servoActive = true;

  } else if (emotion == "sad") {
    // Slow droop motion
    servo1.write(SLOW_CW);
    servo2.write(SLOW_CW);
    servoStopTime = millis() + 3000;
    servoActive = true;

  } else if (emotion == "surprised") {
    // Quick jolt! Fast burst
    servo1.write(FAST_CCW);
    servo2.write(FAST_CW);
    servoStopTime = millis() + 800;
    servoActive = true;

  } else if (emotion == "angry") {
    // Aggressive shaking - 4 rapid alternations
    for (int i = 0; i < 4; i++) {
      servo1.write(FAST_CW);
      servo2.write(STOP);
      delay(300);
      servo1.write(STOP);
      servo2.write(FAST_CW);
      delay(300);
    }
    stop_servos();

  } else if (emotion == "sleepy") {
    // Slow lazy yawn motion
    servo1.write(SLOW_CCW);
    servo2.write(SLOW_CW);
    servoStopTime = millis() + 2500;
    servoActive = true;

  } else if (emotion == "wink") {
    // Playful quick wink - one servo twitches
    servo1.write(MEDIUM_CW);
    servo2.write(STOP);
    servoStopTime = millis() + 600;
    servoActive = true;

  } else if (emotion == "love") {
    // Heart flutter - gentle synchronized sway
    servo1.write(MEDIUM_CCW);
    servo2.write(MEDIUM_CCW);
    servoStopTime = millis() + 2500;
    servoActive = true;

  } else if (emotion == "thinking") {
    // Contemplative tilt - slow alternating lean
    servo1.write(SLOW_CW);
    servo2.write(STOP);
    delay(1000);
    servo1.write(STOP);
    servo2.write(SLOW_CW);
    servoStopTime = millis() + 1000;
    servoActive = true;

  } else if (emotion == "neutral") {
    // No movement, just stop
    stop_servos();

  } else {
    // Unknown emotion, default to neutral
    stop_servos();
  }
}

// ===== STOP SERVOS =====
void stop_servos() {
  if (servo1.attached()) {
    servo1.write(STOP);
    delay(50);
    servo1.detach();  // Detach to prevent power drain and jitter
  }
  
  if (servo2.attached()) {
    servo2.write(STOP);
    delay(50);
    servo2.detach();  // Detach to prevent power drain and jitter
  }
  
  servoActive = false;
}

// ===== PLAY TEST BEEP =====
void play_test_beep() {
  int16_t beep_sample[100];
  for (int i = 0; i < 100; i++) {
    beep_sample[i] = (i % 20 < 10) ? 8000 : -8000;
  }

  size_t bytes_written;
  for (int j = 0; j < 30; j++) {
    i2s_write(I2S_NUM_0, beep_sample, sizeof(beep_sample), &bytes_written, portMAX_DELAY);
  }
  
  // Clear buffer
  i2s_zero_dma_buffer(I2S_NUM_0);
}

// ===== RECEIVE AND PLAY AUDIO =====
void receive_and_play_audio(int expected_size) {
  uint8_t buffer[1024];
  int bytes_received = 0;
  size_t bytes_written;
  unsigned long timeout = millis() + 30000; // 30 sec max

  // Read audio data and play in real-time
  while (bytes_received < expected_size && millis() < timeout) {
    if (Serial.available() > 0) {
      int to_read = min((int)Serial.available(), (int)sizeof(buffer));
      to_read = min(to_read, expected_size - bytes_received);
      
      int actually_read = Serial.readBytes(buffer, to_read);
      
      if (actually_read > 0) {
        // Write to I2S speaker
        i2s_write(I2S_NUM_0, buffer, actually_read, &bytes_written, portMAX_DELAY);
        bytes_received += actually_read;
      }
    }
    yield();  // Allow other tasks to run
  }

  // Flush any remaining data
  delay(100);
  i2s_zero_dma_buffer(I2S_NUM_0);
  
  Serial.println("OK:AUDIO");
}