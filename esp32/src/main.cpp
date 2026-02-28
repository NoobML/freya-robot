/*
 * ============================================================
 * FREYA v2 — ESP32 Firmware with Mic + Smart Audio + RoboEyes
 * ============================================================
 *
 * WIRING:
 *
 * OLED SSD1306 (I2C):
 *   VCC  -> 3.3V
 *   GND  -> GND
 *   SDA  -> GPIO 21
 *   SCL  -> GPIO 19
 *
 * MAX98357A Speaker (I2S_NUM_0, TX):
 *   VIN  -> 5V
 *   GND  -> GND
 *   BCLK -> GPIO 26
 *   LRC  -> GPIO 25
 *   DIN  -> GPIO 22
 *
 * INMP441 Microphone (I2S_NUM_1, RX):
 *   VDD  -> 3.3V (NOT 5V!)
 *   GND  -> GND
 *   L/R  -> GND  (left channel)
 *   SCK  -> GPIO 14
 *   WS   -> GPIO 15  (labeled "HS" on some boards — same thing)
 *   SD   -> GPIO 32
 *
 *   + 100nF capacitor between VDD and GND on the mic
 *
 * ============================================================
 * 
 * MEMORY BUDGET (ESP32 has ~320KB usable SRAM):
 *   - Play buffer:    60KB  (plays ~1.36s at 22050/16bit)
 *   - Record buffer:  64KB  (records ~2s at 16000/16bit)
 *   - DMA buffers:    ~16KB (8 × 512 × 4 bytes for mic + speaker)
 *   - OLED framebuf:  ~1KB
 *   - Stack + heap:   ~50KB
 *   - Free headroom:  ~129KB
 *
 * ============================================================
 * 
 *  Wiring: SCK->14, WS->15, SD->32, L/R->GND, VDD->3.3V
 */

#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <driver/i2s.h>

// ==================== PIN DEFINITIONS ====================

// I2C (OLED)
#define I2C_SDA 21
#define I2C_SCL 19

// OLED
#define SCREEN_WIDTH    128
#define SCREEN_HEIGHT   64
#define SCREEN_ADDRESS  0x3C

// I2S Speaker (I2S_NUM_0)
#define SPKR_BCK   26
#define SPKR_WS    25
#define SPKR_DOUT  22

// I2S Microphone (I2S_NUM_1)
// Matches your actual wiring + friend's confirmed working pinout
#define MIC_BCK    32   // SCK (Serial Clock)
#define MIC_WS     33   // WS / HS (Word Select / Left-Right Clock)
#define MIC_DIN    35   // SD (Serial Data out from mic)

// ==================== AUDIO CONFIGURATION ====================

// Speaker: 22050 Hz, 16-bit, mono (matches Piper TTS default output)
#define SPKR_SAMPLE_RATE   22050
#define SPKR_BITS          I2S_BITS_PER_SAMPLE_16BIT

// Microphone: 16000 Hz, 32-bit read (INMP441 outputs 24-bit in 32-bit frame)
// We read 32-bit and convert to 16-bit for Whisper compatibility
#define MIC_SAMPLE_RATE    16000
#define MIC_BITS           I2S_BITS_PER_SAMPLE_32BIT

// ==================== BUFFER SIZES ====================

/*
 * PLAY_BUFFER_SIZE: Maximum audio we can buffer for playback.
 * 60KB = ~1.36 seconds at 22050Hz/16bit/mono.
 * 
 * This is the threshold between direct play and streamed play:
 *   - Audio ≤ 60KB → load entirely, play gap-free (Mode B)
 *   - Audio > 60KB → stream in 60KB chunks (Mode A)
 * 
 * Why 60KB and not 80KB? We need headroom for the record buffer
 * and stack. 80KB was too aggressive and left < 100KB free.
 */
#define PLAY_BUFFER_SIZE  60000

/*
 * RECORD_BUFFER_SIZE: Maximum recording storage.
 * 64000 bytes = 32000 int16_t samples = 2 seconds at 16kHz.
 * 
 * For longer recordings, we stream to PC in real-time.
 * 2 seconds per chunk is a good balance:
 *   - Short enough to fit in memory
 *   - Long enough for meaningful speech segments
 */
#define RECORD_BUFFER_SIZE  64000

/*
 * DIRECT_PLAY_THRESHOLD: If incoming audio ≤ this, use direct play.
 * Set slightly below PLAY_BUFFER_SIZE to leave safety margin.
 * 
 * TUNING: If you hear audio cutting off, decrease this by 5000.
 *         If short phrases get streamed unnecessarily, increase it.
 */
#define DIRECT_PLAY_THRESHOLD  58000

// Serial
#define SERIAL_BAUD       921600   // Fast baud for audio transfer
#define AUDIO_TIMEOUT_MS  30000

// ==================== GLOBALS ====================

Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);

// Undefine Arduino's DEFAULT before RoboEyes redefines it
#ifdef DEFAULT
#undef DEFAULT
#endif

#include "FluxGarage_RoboEyes.h"
roboEyes eyes;

// Audio buffers (allocated on heap in setup)
uint8_t*  playBuffer   = nullptr;
int16_t*  recordBuffer = nullptr;

// Playback state
size_t playWritePos     = 0;
size_t expectedAudioSize = 0;
bool   audioBuffering   = false;

// Recording state
volatile bool isRecording   = false;
volatile size_t recordPos   = 0;
size_t recordDuration       = 3;  // seconds, overridden by command

// Hardware status
bool speakerOK = false;
bool micOK     = false;
bool oledOK    = false;

// Timing
unsigned long audioStartTime = 0;
unsigned long lastEyeUpdate  = 0;


// ==================== FORWARD DECLARATIONS ====================
// (needed because playStreamedChunk calls updateEyes, which is defined later)
void updateEyes();
void setEmotion(int num);

// ==================== SPEAKER (I2S_NUM_0) ====================

bool initSpeaker() {
    i2s_config_t config = {};
    config.mode                 = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX);
    config.sample_rate          = SPKR_SAMPLE_RATE;
    config.bits_per_sample      = SPKR_BITS;
    config.channel_format       = I2S_CHANNEL_FMT_ONLY_LEFT;
    config.communication_format = I2S_COMM_FORMAT_STAND_I2S;
    config.intr_alloc_flags     = ESP_INTR_FLAG_LEVEL1;
    config.dma_buf_count        = 8;      // 8 DMA descriptors
    config.dma_buf_len          = 512;    // 512 samples each = 1KB per buffer
    config.use_apll             = false;
    config.tx_desc_auto_clear   = true;   // Auto-clear on underflow (silence)
    config.fixed_mclk           = 0;

    i2s_pin_config_t pins = {};
    pins.bck_io_num   = SPKR_BCK;
    pins.ws_io_num    = SPKR_WS;
    pins.data_out_num = SPKR_DOUT;
    pins.data_in_num  = I2S_PIN_NO_CHANGE;

    esp_err_t err = i2s_driver_install(I2S_NUM_0, &config, 0, NULL);
    if (err != ESP_OK) {
        Serial.printf("{\"type\":\"ERROR\",\"msg\":\"spkr_install:%d\"}\n", err);
        return false;
    }

    err = i2s_set_pin(I2S_NUM_0, &pins);
    if (err != ESP_OK) {
        i2s_driver_uninstall(I2S_NUM_0);  // Clean up on failure
        Serial.printf("{\"type\":\"ERROR\",\"msg\":\"spkr_pins:%d\"}\n", err);
        return false;
    }

    return true;
}


// ==================== MICROPHONE (I2S_NUM_1) ====================

bool initMicrophone() {
    /*
     * INMP441 Configuration Notes:
     * 
     * 1. MUST use 32-bit samples. The INMP441 outputs 24-bit data
     *    left-justified in a 32-bit frame. Using 16-bit reads garbage.
     * 
     * 2. MUST be I2S_MODE_MASTER — ESP32 generates BCK and WS clocks.
     *    INMP441 is always a slave device.
     * 
     * 3. Channel format ONLY_LEFT because L/R pin is tied to GND.
     *    If your L/R is tied to VDD, use ONLY_RIGHT instead.
     * 
     * 4. Sample rate 16000 matches Whisper's expected input.
     *    Recording at other rates would require resampling.
     * 
     * 5. DMA: 8 buffers × 1024 samples × 4 bytes = 32KB.
     *    At 16kHz, this gives 512ms of buffering before overflow.
     */
    i2s_config_t config = {};
    config.mode                 = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_RX);
    config.sample_rate          = MIC_SAMPLE_RATE;
    config.bits_per_sample      = MIC_BITS;  // 32-bit for INMP441!
    config.channel_format       = I2S_CHANNEL_FMT_ONLY_LEFT;
    config.communication_format = I2S_COMM_FORMAT_I2S;  // Match friend's working config
    config.intr_alloc_flags     = ESP_INTR_FLAG_LEVEL1;
    config.dma_buf_count        = 8;
    config.dma_buf_len          = 1024;   // Larger buffers = more stable (friend uses 1024)
    config.use_apll             = false;

    i2s_pin_config_t pins = {};
    pins.bck_io_num   = MIC_BCK;
    pins.ws_io_num    = MIC_WS;
    pins.data_out_num = I2S_PIN_NO_CHANGE;
    pins.data_in_num  = MIC_DIN;

    esp_err_t err = i2s_driver_install(I2S_NUM_1, &config, 0, NULL);
    if (err != ESP_OK) {
        Serial.printf("{\"type\":\"ERROR\",\"msg\":\"mic_install:%d\"}\n", err);
        return false;
    }

    err = i2s_set_pin(I2S_NUM_1, &pins);
    if (err != ESP_OK) {
        i2s_driver_uninstall(I2S_NUM_1);
        Serial.printf("{\"type\":\"ERROR\",\"msg\":\"mic_pins:%d\"}\n", err);
        return false;
    }

    // Clear any garbage in the DMA buffer from power-on
    // Read and discard ~100ms of data to let the mic stabilize
    uint8_t discard[512];
    size_t bytesRead;
    for (int i = 0; i < 10; i++) {
        i2s_read(I2S_NUM_1, discard, sizeof(discard), &bytesRead, 100);
    }

    return true;
}


// ==================== AUDIO VALIDATION ====================

/*
 * Validates that microphone data contains actual audio, not silence or noise.
 * Call this BEFORE sending data to PC/Whisper to avoid wasting time on bad data.
 * 
 * Returns: true if data looks like valid audio
 */
bool validateRecording(int16_t* buffer, size_t sampleCount) {
    if (sampleCount == 0) return false;

    int32_t sumAbs = 0;
    int16_t maxAmp = 0;
    int nonZero = 0;

    // Sample every 4th value for speed (still checks 25% of data)
    for (size_t i = 0; i < sampleCount; i += 4) {
        int16_t val = buffer[i];
        if (val != 0) nonZero++;
        int16_t absVal = abs(val);
        if (absVal > maxAmp) maxAmp = absVal;
        sumAbs += absVal;
    }

    size_t checked = sampleCount / 4;
    float nonZeroPct = (float)nonZero / checked * 100.0f;
    float avgAmp = (float)sumAbs / checked;

    Serial.printf("{\"type\":\"MIC_STATS\",\"max\":%d,\"avg\":%.1f,\"nonzero\":%.1f}\n",
                  maxAmp, avgAmp, nonZeroPct);

    // Failure conditions:
    if (nonZeroPct < 1.0f) {
        Serial.println("{\"type\":\"MIC_ERROR\",\"msg\":\"all_zeros_check_wiring\"}");
        return false;
    }
    if (maxAmp < 10) {
        Serial.println("{\"type\":\"MIC_ERROR\",\"msg\":\"no_signal_check_connections\"}");
        return false;
    }
    if (avgAmp > 30000) {
        Serial.println("{\"type\":\"MIC_ERROR\",\"msg\":\"clipping_check_power\"}");
        return false;
    }

    return true;
}


// ==================== RECORDING ====================

/*
 * Records audio from INMP441 and stores as 16-bit PCM in recordBuffer.
 * 
 * The INMP441 gives us 32-bit samples. We extract the top 18 bits
 * and pack into 16-bit for Whisper compatibility.
 * 
 * Memory: durationSec seconds at 16kHz × 2 bytes = 32KB/sec
 * Max duration with 64KB buffer: 2 seconds per call.
 * For longer recordings, call multiple times and stream to PC.
 */
size_t recordAudio(float durationSec) {
    if (!micOK || !recordBuffer) return 0;

    size_t maxSamples = RECORD_BUFFER_SIZE / sizeof(int16_t);  // 32000 samples
    size_t targetSamples = (size_t)(MIC_SAMPLE_RATE * durationSec);
    if (targetSamples > maxSamples) targetSamples = maxSamples;

    // Temporary buffer for 32-bit I2S reads
    // 256 samples × 4 bytes = 1024 bytes on stack — safe
    int32_t rawBuffer[256];
    size_t bytesRead;
    size_t samplesRecorded = 0;

    // Flush old DMA data before recording
    i2s_read(I2S_NUM_1, rawBuffer, sizeof(rawBuffer), &bytesRead, 100);

    while (samplesRecorded < targetSamples) {
        size_t samplesToRead = min((size_t)256, targetSamples - samplesRecorded);
        size_t bytesToRead = samplesToRead * sizeof(int32_t);

        esp_err_t err = i2s_read(I2S_NUM_1, rawBuffer, bytesToRead, &bytesRead, 1000);
        if (err != ESP_OK || bytesRead == 0) break;

        size_t samplesGot = bytesRead / sizeof(int32_t);

        // Convert 32-bit INMP441 data → 16-bit PCM
        for (size_t i = 0; i < samplesGot && samplesRecorded < targetSamples; i++) {
            /*
             * INMP441 data format (32-bit frame):
             *   [24-bit audio data, left-justified] [8 zero bits]
             * 
             * Right-shift by 14 to get ~18 useful bits into int16_t range.
             * 
             * If audio is too quiet:  try >>16 (less gain)
             * If audio clips:        try >>12 (even less gain)  
             * If audio is too loud:  try >>11
             * 
             * Start with >>14 and adjust based on your mic's sensitivity.
             * 
             */
            // Raw INMP441 signal is VERY quiet — apply gain boost
            // GAIN 40 is good for speech. Reduce to 20 if clipping, increase to 60 if still too quiet.
          recordBuffer[samplesRecorded++] = (int16_t)(rawBuffer[i] >> 14);
        }
    }

    return samplesRecorded;
}


// ==================== MIC TEST ====================

/*
 * Quick mic diagnostic: records 0.5s, reports stats.
 * Use this to verify wiring before attempting full recording.
 */
void testMicrophone() {
    if (!micOK) {
        Serial.println("{\"type\":\"MIC_TEST_RESULT\",\"status\":\"no_driver\"}");
        return;
    }

    size_t samples = recordAudio(0.5);  // 500ms test recording
    
    if (samples == 0) {
        Serial.println("{\"type\":\"MIC_TEST_RESULT\",\"status\":\"no_data\"}");
        return;
    }

    bool valid = validateRecording(recordBuffer, samples);
    
    // Find peak amplitude for the test
    int16_t peak = 0;
    for (size_t i = 0; i < samples; i++) {
        int16_t a = abs(recordBuffer[i]);
        if (a > peak) peak = a;
    }

    Serial.printf("{\"type\":\"MIC_TEST_RESULT\",\"status\":\"%s\",\"samples\":%d,\"peak\":%d}\n",
                  valid ? "ok" : "fail", samples, peak);
}


// ==================== SMART AUDIO PLAYBACK ====================

/*
 * Mode B: Direct Play — for short audio (≤ DIRECT_PLAY_THRESHOLD bytes)
 * 
 * Loads all audio into buffer first, then plays in one continuous
 * i2s_write stream. No gaps, no interruptions. Sounds clean.
 */
void playDirect() {
    if (playWritePos == 0 || !playBuffer || !speakerOK) return;

    Serial.printf("{\"type\":\"PLAY_DIRECT\",\"bytes\":%d}\n", playWritePos);

    size_t offset = 0;
    size_t written;

    /*
     * Write in 512-byte chunks to I2S.
     * 512 bytes = 256 samples at 16-bit = 11.6ms of audio at 22050Hz.
     * This matches the DMA buffer length for smooth feeding.
     */
    while (offset < playWritePos) {
        size_t toWrite = min((size_t)512, playWritePos - offset);
        i2s_write(I2S_NUM_0, playBuffer + offset, toWrite, &written, 1000 / portTICK_PERIOD_MS);
        offset += written;

        // Brief yield to keep watchdog happy on long clips
        if (offset % 8192 == 0) yield();
    }

    // Let DMA drain the last buffer before clearing
    delay(50);
    i2s_zero_dma_buffer(I2S_NUM_0);

    Serial.printf("{\"type\":\"DONE\",\"played\":%d}\n", offset);
    playWritePos = 0;
}


/*
 * Mode A: Streamed Play — for long audio (> DIRECT_PLAY_THRESHOLD bytes)
 * 
 * Called after each chunk is buffered. Plays the chunk, then signals
 * Python to send the next one. This allows unlimited audio length
 * while using only PLAY_BUFFER_SIZE of memory.
 */
void playStreamedChunk() {
    if (playWritePos == 0 || !playBuffer || !speakerOK) return;

    Serial.printf("{\"type\":\"PLAYING_CHUNK\",\"bytes\":%d}\n", playWritePos);

    size_t offset = 0;
    size_t written;

    while (offset < playWritePos) {
        size_t toWrite = min((size_t)512, playWritePos - offset);
        i2s_write(I2S_NUM_0, playBuffer + offset, toWrite, &written, 1000 / portTICK_PERIOD_MS);
        offset += written;

        // Update eyes periodically during long playback
        if (offset % 4096 == 0) {
            updateEyes();
            yield();
        }
    }

    delay(30);  // Shorter delay between chunks for continuity
    // DON'T zero DMA buffer between chunks — it causes audible clicks

    Serial.printf("{\"type\":\"CHUNK_DONE\",\"played\":%d}\n", offset);
    playWritePos = 0;
}


// ==================== TONE AND MELODY ====================

void playTone(int freq, int duration_ms) {
    if (!speakerOK) return;
    int samples_per_cycle = SPKR_SAMPLE_RATE / freq;
    int total_samples = (SPKR_SAMPLE_RATE * duration_ms) / 1000;
    int16_t buffer[128];
    size_t written;
    int idx = 0;

    for (int i = 0; i < total_samples; i++) {
        buffer[idx++] = ((i % samples_per_cycle) < samples_per_cycle / 2) ? 12000 : -12000;
        if (idx >= 128) {
            i2s_write(I2S_NUM_0, buffer, sizeof(buffer), &written, portMAX_DELAY);
            idx = 0;
        }
    }
    if (idx > 0) i2s_write(I2S_NUM_0, buffer, idx * 2, &written, portMAX_DELAY);
    i2s_zero_dma_buffer(I2S_NUM_0);
}

void playMelody() {
    playTone(523, 120);
    playTone(659, 120);
    playTone(784, 120);
    playTone(1047, 250);
}

void playBeep() {
    playTone(880, 100);
}


// ==================== EMOTIONS ====================

void setEmotion(int num) {
    eyes.tired = false;
    eyes.angry = false;
    eyes.happy = false;
    eyes.idle = false;
    eyes.thinking = false;

    switch (num) {
        case 0: // Neutral/Idle
            eyes.idle = true;
            eyes.autoblinker = true;
            eyes.setMood(DEFAULT);
            break;
        case 1: // Happy
            eyes.happy = true;
            eyes.autoblinker = true;
            eyes.setMood(HAPPY);
            break;
        case 2: // Sad/Tired
            eyes.tired = true;
            eyes.autoblinker = true;
            eyes.setMood(TIRED);
            break;
        case 3: // Surprised
            eyes.autoblinker = false;
            eyes.setMood(DEFAULT);
            eyes.open();
            break;
        case 4: // Angry
            eyes.angry = true;
            eyes.autoblinker = false;
            eyes.setMood(ANGRY);
            break;
        case 5: // Sleepy
            eyes.tired = true;
            eyes.autoblinker = true;
            eyes.setMood(TIRED);
            break;
        case 6: // Wink
            eyes.happy = true;
            eyes.setMood(HAPPY);
            eyes.blink(1, 0);
            break;
        case 7: // Love
            eyes.happy = true;
            eyes.autoblinker = true;
            eyes.setMood(HAPPY);
            break;
        case 8: // Thinking / Listening
            eyes.thinking = true;
            eyes.autoblinker = true;
            break;
        case 9: // Recording (alert look)
            eyes.autoblinker = false;
            eyes.setMood(DEFAULT);
            eyes.open();
            break;
        default:
            eyes.idle = true;
            eyes.autoblinker = true;
            eyes.setMood(DEFAULT);
            break;
    }
}

void updateEyes() {
    if (millis() - lastEyeUpdate >= 20) {
        eyes.update();
        lastEyeUpdate = millis();
    }
}


// ==================== COMMAND HANDLER ====================

/*
 * Processes JSON commands from Python over serial.
 * 
 * Commands:
 *   {"type":"AUDIO_START","size":N}          — Begin receiving audio
 *   {"type":"AUDIO_START","size":N,"mode":"direct"}  — Force direct mode
 *   {"type":"RECORD","duration":3}           — Record N seconds from mic
 *   {"type":"TEST_MIC"}                      — Quick mic diagnostic
 *   {"type":"TEST_SOUND"}                    — Play test melody
 *   Single digit '0'-'9'                     — Set emotion
 *   'm'                                      — Play melody
 *   'b'                                      — Play beep
 */
void handleCommand(String& line) {
    line.trim();

    // ---- AUDIO_START: Begin receiving audio data ----
    if (line.indexOf("AUDIO_START") >= 0) {
        int sizeIdx = line.indexOf("\"size\":");
        expectedAudioSize = (sizeIdx >= 0) ? line.substring(sizeIdx + 7).toInt() : PLAY_BUFFER_SIZE;
        if (expectedAudioSize > PLAY_BUFFER_SIZE) expectedAudioSize = PLAY_BUFFER_SIZE;

        playWritePos = 0;
        audioBuffering = true;
        audioStartTime = millis();

        Serial.printf("{\"type\":\"AUDIO_READY\",\"expecting\":%d,\"mode\":\"%s\"}\n",
                      expectedAudioSize,
                      expectedAudioSize <= DIRECT_PLAY_THRESHOLD ? "direct" : "stream");
    }

    // ---- RECORD: Capture audio from INMP441 ----
    else if (line.indexOf("RECORD") >= 0) {
        // Parse optional duration
        float duration = 3.0;  // default 3 seconds
        int durIdx = line.indexOf("\"duration\":");
        if (durIdx >= 0) {
            duration = line.substring(durIdx + 11).toFloat();
            if (duration <= 0 || duration > 10) duration = 3.0;
        }

        setEmotion(9);  // Alert/recording eyes
        Serial.printf("{\"type\":\"RECORDING\",\"duration\":%.1f}\n", duration);

        // Record
        size_t samples = recordAudio(duration);

        if (samples == 0) {
            Serial.println("{\"type\":\"RECORD_FAIL\",\"msg\":\"no_samples\"}");
            setEmotion(0);
            return;
        }

        // Validate before sending
        bool valid = validateRecording(recordBuffer, samples);
        size_t dataBytes = samples * sizeof(int16_t);

        Serial.printf("{\"type\":\"RECORDED\",\"samples\":%d,\"bytes\":%d,\"valid\":%s}\n",
                      samples, dataBytes, valid ? "true" : "false");

     // Don't skip sending — let Whisper decide if audio is usable
        // valid=false just means it's quiet, not necessarily broken

        // Send raw PCM data over serial
        Serial.printf("{\"type\":\"AUDIO_DATA_START\",\"bytes\":%d,\"rate\":%d,\"bits\":16,\"channels\":1}\n",
                      dataBytes, MIC_SAMPLE_RATE);

        // Small delay to let Python prepare
        delay(50);

        // Send in 1024-byte chunks for flow control
        uint8_t* dataPtr = (uint8_t*)recordBuffer;
        size_t sent = 0;
        while (sent < dataBytes) {
            size_t chunkSize = min((size_t)1024, dataBytes - sent);
            Serial.write(dataPtr + sent, chunkSize);
            sent += chunkSize;
            // Pace the transfer to avoid serial buffer overflow
            // At 921600 baud: ~92KB/s theoretical, use ~80KB/s
            delayMicroseconds(chunkSize * 11);  // ~11µs per byte at 921600
        }

        Serial.flush();
        delay(50);
        Serial.printf("{\"type\":\"AUDIO_DATA_END\",\"sent\":%d}\n", sent);

        setEmotion(0);  // Back to neutral
    }

    // ---- TEST_MIC: Quick diagnostic ----
    else if (line.indexOf("TEST_MIC") >= 0) {
        testMicrophone();
    }

    // ---- TEST_SOUND: Play melody ----
    else if (line.indexOf("TEST_SOUND") >= 0) {
        playMelody();
        Serial.println("{\"type\":\"ACK\",\"cmd\":\"TEST_SOUND\"}");
    }

    // ---- EMOTION: Set face ----
    else if (line.indexOf("EMOTION") >= 0) {
        int valIdx = line.indexOf("\"value\":");
        if (valIdx >= 0) {
            int emotion = line.substring(valIdx + 8).toInt();
            setEmotion(emotion);
            Serial.println("{\"type\":\"ACK\",\"cmd\":\"EMOTION\"}");
        }
    }
}


// ==================== SETUP ====================

void setup() {
    // Large RX buffer for incoming audio data
    Serial.setRxBufferSize(16384);
    Serial.begin(SERIAL_BAUD);
    delay(500);

    Serial.println("\n========================================");
    Serial.println("FREYA v2 — Audio + Mic + RoboEyes");
    Serial.printf("Serial: %d baud\n", SERIAL_BAUD);
    Serial.println("========================================");

    // ---- Allocate buffers ----
    playBuffer = (uint8_t*)malloc(PLAY_BUFFER_SIZE);
    Serial.printf("Play buffer:   %s (%d bytes)\n",
                  playBuffer ? "OK" : "FAIL", PLAY_BUFFER_SIZE);

    recordBuffer = (int16_t*)malloc(RECORD_BUFFER_SIZE);
    Serial.printf("Record buffer: %s (%d bytes)\n",
                  recordBuffer ? "OK" : "FAIL", RECORD_BUFFER_SIZE);

    Serial.printf("Free heap:     %d bytes\n", ESP.getFreeHeap());

    // ---- OLED ----
    Wire.begin(I2C_SDA, I2C_SCL);
    oledOK = display.begin(SSD1306_SWITCHCAPVCC, SCREEN_ADDRESS);
    Serial.printf("OLED:          %s\n", oledOK ? "OK" : "FAIL");

    if (oledOK) {
        eyes.begin(SCREEN_WIDTH, SCREEN_HEIGHT, 50);
        eyes.setAutoblinker(ON, 3, 2);
        eyes.setIdleMode(ON, 2, 3);
        eyes.open();
    }

    // ---- Speaker ----
    speakerOK = initSpeaker();
    Serial.printf("Speaker:       %s\n", speakerOK ? "OK" : "FAIL");

    if (speakerOK) {
        delay(200);
        playMelody();
    }

    // ---- Microphone ----
    micOK = initMicrophone();
    Serial.printf("Microphone:    %s\n", micOK ? "OK" : "FAIL");

    // Quick mic test at startup
    if (micOK) {
        delay(200);
        testMicrophone();
    }

    // ---- Ready ----
    setEmotion(0);
    Serial.printf("{\"type\":\"READY\",\"speaker\":%s,\"mic\":%s,\"oled\":%s,\"heap\":%d}\n",
                  speakerOK ? "true" : "false",
                  micOK ? "true" : "false",
                  oledOK ? "true" : "false",
                  ESP.getFreeHeap());
}


// ==================== MAIN LOOP ====================

void loop() {
    // Always update eyes when not doing time-critical audio work
    if (!audioBuffering) {
        updateEyes();
    }

    // Timeout for stuck audio transfers
    if (audioBuffering && (millis() - audioStartTime > AUDIO_TIMEOUT_MS)) {
        Serial.println("{\"type\":\"TIMEOUT\"}");
        audioBuffering = false;
        playWritePos = 0;
    }

    // Nothing to process? Update eyes and yield
    if (!Serial.available()) {
        delay(1);
        return;
    }

    // ---- AUDIO BUFFERING MODE ----
    if (audioBuffering) {
        // Read as much audio data as available
        while (Serial.available() && playWritePos < expectedAudioSize) {
            size_t available = Serial.available();
            size_t remaining = expectedAudioSize - playWritePos;
            size_t toRead = min(available, remaining);

            // Safety: don't overflow the buffer
            if (playWritePos + toRead > PLAY_BUFFER_SIZE) {
                toRead = PLAY_BUFFER_SIZE - playWritePos;
            }

            if (toRead > 0 && playBuffer) {
                playWritePos += Serial.readBytes(playBuffer + playWritePos, toRead);
            }
        }

        // All data received? Play it
        if (playWritePos >= expectedAudioSize || playWritePos >= PLAY_BUFFER_SIZE) {
            Serial.printf("{\"type\":\"BUFFERED\",\"bytes\":%d}\n", playWritePos);
            audioBuffering = false;

            // SMART DECISION: Direct or Streamed
            if (playWritePos <= DIRECT_PLAY_THRESHOLD) {
                playDirect();          // Mode B: gap-free
            } else {
                playStreamedChunk();   // Mode A: chunk-by-chunk
            }
        }
    }

    // ---- COMMAND MODE ----
    else {
        char first = Serial.peek();

        if (first == '{') {
            // JSON command
            String line = Serial.readStringUntil('\n');
            handleCommand(line);
        }
        else if (first >= '0' && first <= '9') {
            // Single-digit emotion shortcut
            setEmotion(Serial.read() - '0');
        }
        else if (first == 'm') {
            Serial.read();
            playMelody();
        }
        else if (first == 'b') {
            Serial.read();
            playBeep();
        }
        else {
            Serial.read();  // Discard unknown byte
        }
    }
}