/*
 * ============================================
 * FREYA — INMP441 Microphone Diagnostic Tool
 * ============================================
 * 
 * Flash this FIRST before integrating with main firmware.
 * It tests every failure mode of the INMP441 and reports
 * results over serial at 115200 baud.
 *
 * WIRING (same as main_v2.cpp):
 *   VDD  -> 3.3V
 *   GND  -> GND
 *   L/R  -> GND
 *   SCK  -> GPIO 32
 *   WS   -> GPIO 33
 *   SD   -> GPIO 35
 *   + 100nF cap between VDD and GND
 *
 * Open Serial Monitor at 115200 baud and follow instructions.
 * ============================================
 */

#include <Arduino.h>
#include <driver/i2s.h>

#define MIC_BCK  32
#define MIC_WS   33
#define MIC_DIN  35
#define MIC_SAMPLE_RATE 16000

void setup() {
    Serial.begin(115200);
    delay(1000);

    Serial.println("\n==========================================");
    Serial.println("   INMP441 Microphone Diagnostic Tool");
    Serial.println("==========================================\n");

    // ---- Test 1: I2S Driver Installation ----
    Serial.println("[TEST 1] Installing I2S driver...");

    i2s_config_t config = {};
    config.mode                 = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_RX);
    config.sample_rate          = MIC_SAMPLE_RATE;
    config.bits_per_sample      = I2S_BITS_PER_SAMPLE_32BIT;
    config.channel_format       = I2S_CHANNEL_FMT_ONLY_LEFT;
    config.communication_format = I2S_COMM_FORMAT_STAND_I2S;
    config.intr_alloc_flags     = ESP_INTR_FLAG_LEVEL1;
    config.dma_buf_count        = 8;
    config.dma_buf_len          = 512;
    config.use_apll             = false;

    i2s_pin_config_t pins = {};
    pins.bck_io_num   = MIC_BCK;
    pins.ws_io_num    = MIC_WS;
    pins.data_out_num = I2S_PIN_NO_CHANGE;
    pins.data_in_num  = MIC_DIN;

    esp_err_t err = i2s_driver_install(I2S_NUM_1, &config, 0, NULL);
    if (err != ESP_OK) {
        Serial.printf("   FAIL — i2s_driver_install error: %d\n", err);
        Serial.println("   → Check: is I2S_NUM_1 free? Is I2S_NUM_0 already using it?");
        while (1) delay(1000);
    }
    Serial.println("   OK — I2S driver installed on I2S_NUM_1");

    err = i2s_set_pin(I2S_NUM_1, &pins);
    if (err != ESP_OK) {
        Serial.printf("   FAIL — i2s_set_pin error: %d\n", err);
        Serial.println("   → Check GPIO numbers. GPIO 35 is input-only (correct for mic).");
        while (1) delay(1000);
    }
    Serial.println("   OK — Pins configured (SCK=32, WS=33, SD=35)");

    // Let mic stabilize
    Serial.println("\n   Waiting 500ms for mic to stabilize...");
    delay(500);

    // Flush garbage
    int32_t flush[256];
    size_t flushed;
    for (int i = 0; i < 5; i++) {
        i2s_read(I2S_NUM_1, flush, sizeof(flush), &flushed, 100);
    }

    // ---- Test 2: Read Raw 32-bit Data ----
    Serial.println("\n[TEST 2] Reading raw 32-bit samples...");

    int32_t raw32[512];
    size_t bytesRead;
    err = i2s_read(I2S_NUM_1, raw32, sizeof(raw32), &bytesRead, 2000);

    if (err != ESP_OK || bytesRead == 0) {
        Serial.printf("   FAIL — i2s_read error: %d, bytes: %d\n", err, bytesRead);
        Serial.println("   → Possible causes:");
        Serial.println("     - SD (data) pin not connected");
        Serial.println("     - SCK (clock) pin not connected");
        Serial.println("     - Mic not powered (check VDD = 3.3V)");
        while (1) delay(1000);
    }

    int samplesRead = bytesRead / 4;
    Serial.printf("   OK — Read %d samples (%d bytes)\n", samplesRead, bytesRead);

    // Analyze raw data
    int32_t minVal = raw32[0], maxVal = raw32[0];
    int allZero = 0, allFF = 0;
    int64_t sum = 0;

    for (int i = 0; i < samplesRead; i++) {
        if (raw32[i] == 0) allZero++;
        if (raw32[i] == -1 || raw32[i] == 0x7FFFFFFF) allFF++;
        if (raw32[i] < minVal) minVal = raw32[i];
        if (raw32[i] > maxVal) maxVal = raw32[i];
        sum += abs(raw32[i]);
    }

    Serial.printf("\n   Raw 32-bit analysis (%d samples):\n", samplesRead);
    Serial.printf("     Min:      %d (0x%08X)\n", minVal, minVal);
    Serial.printf("     Max:      %d (0x%08X)\n", maxVal, maxVal);
    Serial.printf("     Zeros:    %d / %d (%.1f%%)\n", allZero, samplesRead, 100.0 * allZero / samplesRead);
    Serial.printf("     All-ones: %d / %d\n", allFF, samplesRead);
    Serial.printf("     Avg abs:  %lld\n", sum / samplesRead);

    // Diagnose
    if (allZero > samplesRead * 0.95) {
        Serial.println("\n   *** DIAGNOSIS: ALL ZEROS ***");
        Serial.println("   Likely causes:");
        Serial.println("     1. SD (data) pin not connected or wrong GPIO");
        Serial.println("     2. L/R pin floating (must be tied to GND or VDD)");
        Serial.println("     3. Mic not powered");
        Serial.println("     4. Wrong channel (L/R tied to VDD but reading LEFT channel)");
    }
    else if (allFF > samplesRead * 0.5) {
        Serial.println("\n   *** DIAGNOSIS: ALL ONES / SATURATED ***");
        Serial.println("   Likely causes:");
        Serial.println("     1. SD pin shorted to VDD");
        Serial.println("     2. Mic damaged");
    }
    else {
        Serial.println("\n   Data looks reasonable — mic is producing output!");
    }

    // ---- Test 3: Convert to 16-bit and analyze ----
    Serial.println("\n[TEST 3] Converting to 16-bit (>>14 shift)...");

    int16_t converted[512];
    for (int i = 0; i < samplesRead; i++) {
        converted[i] = (int16_t)(raw32[i] >> 14);
    }

    int16_t min16 = converted[0], max16 = converted[0];
    int zero16 = 0;
    int32_t sum16 = 0;

    for (int i = 0; i < samplesRead; i++) {
        if (converted[i] == 0) zero16++;
        if (converted[i] < min16) min16 = converted[i];
        if (converted[i] > max16) max16 = converted[i];
        sum16 += abs(converted[i]);
    }

    Serial.printf("   16-bit analysis:\n");
    Serial.printf("     Min:    %d\n", min16);
    Serial.printf("     Max:    %d\n", max16);
    Serial.printf("     Range:  %d\n", max16 - min16);
    Serial.printf("     Zeros:  %d / %d\n", zero16, samplesRead);
    Serial.printf("     Avg:    %d\n", sum16 / samplesRead);

    if (max16 - min16 < 100) {
        Serial.println("\n   *** WARNING: Very low dynamic range ***");
        Serial.println("   Audio will be very quiet. Try:");
        Serial.println("     - Moving closer to mic");
        Serial.println("     - Checking 100nF decoupling capacitor");
        Serial.println("     - Trying >>12 shift instead of >>14");
    }
    else if (max16 > 30000 || min16 < -30000) {
        Serial.println("\n   *** WARNING: Near clipping ***");
        Serial.println("   Try >>16 shift for less gain");
    }
    else {
        Serial.println("   16-bit conversion looks good!");
    }

    // ---- Test 4: Live amplitude monitor ----
    Serial.println("\n[TEST 4] Live amplitude monitor (10 seconds)");
    Serial.println("   Clap or speak near the mic to see amplitude changes.");
    Serial.println("   You should see the bar move when you make sound.\n");

    unsigned long testStart = millis();
    while (millis() - testStart < 10000) {
        int32_t readBuf[256];
        i2s_read(I2S_NUM_1, readBuf, sizeof(readBuf), &bytesRead, 500);
        int samples = bytesRead / 4;

        int16_t peak = 0;
        for (int i = 0; i < samples; i++) {
            int16_t s = abs((int16_t)(readBuf[i] >> 14));
            if (s > peak) peak = s;
        }

        // Draw amplitude bar
        int barLen = map(peak, 0, 20000, 0, 40);
        barLen = constrain(barLen, 0, 40);

        Serial.printf("   [%5d] ", peak);
        for (int i = 0; i < barLen; i++) Serial.print("█");
        for (int i = barLen; i < 40; i++) Serial.print("░");

        if (peak > 15000) Serial.print(" LOUD!");
        else if (peak > 3000) Serial.print(" Speech");
        else if (peak > 500) Serial.print(" Quiet");
        else Serial.print(" Silence");

        Serial.println();
        delay(100);
    }

    // ---- Test 5: Record and dump ----
    Serial.println("\n[TEST 5] Recording 2 seconds of audio...");
    Serial.println("   Speak normally into the microphone.\n");

    const int recSamples = MIC_SAMPLE_RATE * 2;  // 2 seconds
    int16_t* recBuffer = (int16_t*)malloc(recSamples * sizeof(int16_t));

    if (!recBuffer) {
        Serial.println("   FAIL — out of memory!");
    } else {
        int recorded = 0;
        int32_t tempBuf[256];

        // Flush
        i2s_read(I2S_NUM_1, tempBuf, sizeof(tempBuf), &bytesRead, 100);

        while (recorded < recSamples) {
            int toRead = min(256, recSamples - recorded);
            i2s_read(I2S_NUM_1, tempBuf, toRead * 4, &bytesRead, 1000);
            int got = bytesRead / 4;
            for (int i = 0; i < got && recorded < recSamples; i++) {
                recBuffer[recorded++] = (int16_t)(tempBuf[i] >> 14);
            }
        }

        // Statistics
        int16_t recMax = 0;
        int32_t recSum = 0;
        for (int i = 0; i < recorded; i++) {
            int16_t a = abs(recBuffer[i]);
            if (a > recMax) recMax = a;
            recSum += a;
        }

        Serial.printf("   Recorded %d samples (%.1f seconds)\n", recorded, (float)recorded / MIC_SAMPLE_RATE);
        Serial.printf("   Peak amplitude: %d\n", recMax);
        Serial.printf("   Average amplitude: %d\n", (int)(recSum / recorded));

        if (recMax > 1000) {
            Serial.println("   Audio captured successfully!");
        } else {
            Serial.println("   Very quiet — may not produce good transcription.");
        }

        free(recBuffer);
    }

    // ---- Summary ----
    Serial.println("\n==========================================");
    Serial.println("   DIAGNOSTIC COMPLETE");
    Serial.println("==========================================");
    Serial.println("\n   If all tests passed with reasonable amplitudes,");
    Serial.println("   your INMP441 is working correctly.");
    Serial.println("   Flash main_v2.cpp to use it with the full system.");
    Serial.println("\n   If you see all zeros or very low amplitudes:");
    Serial.println("   1. Double-check wiring (especially L/R → GND)");
    Serial.println("   2. Ensure 3.3V power (not 5V!)");
    Serial.println("   3. Add 100nF capacitor between VDD and GND");
    Serial.println("   4. Try different jumper wires (I2S is timing-sensitive)");
    Serial.println("   5. Try shorter wires (< 10cm ideal)");
}

void loop() {
    // Nothing — all diagnostics run in setup()
    delay(10000);
}
