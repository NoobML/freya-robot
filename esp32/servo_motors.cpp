#include <ESP32Servo.h>

// Servo objects
Servo servo1;
Servo servo2;

// Pins
#define SERVO1_PIN 18
#define SERVO2_PIN 19

// Servo speed values for continuous rotation
#define STOP 90
#define SLOW_CW 100
#define FAST_CW 180
#define SLOW_CCW 80
#define FAST_CCW 0

void setup() {
  Serial.begin(115200);
  
  // Attach servos
  servo1.attach(SERVO1_PIN);
  servo2.attach(SERVO2_PIN);
  
  // Stop both servos initially
  servo1.write(STOP);
  servo2.write(STOP);
  
  Serial.println("Emotion Servo System Ready!");
  delay(2000);
}

void loop() {
  // Happy
  Serial.println("Emotion: HAPPY");
  servo1.write(FAST_CW);   // Servo1 clockwise
  servo2.write(FAST_CCW);  // Servo2 counterclockwise
  delay(2000);
  servo1.write(STOP);
  servo2.write(STOP);
  delay(1000);
  
  // Sad
  Serial.println("Emotion: SAD");
  servo1.write(SLOW_CW);   // Both slow clockwise
  servo2.write(SLOW_CW);
  delay(3000);
  servo1.write(STOP);
  servo2.write(STOP);
  delay(1000);
  
  // Angry
  Serial.println("Emotion: ANGRY");
  for(int i = 0; i < 4; i++) {  // Alternate 4 times
    servo1.write(FAST_CW);
    servo2.write(STOP);
    delay(500);
    servo1.write(STOP);
    servo2.write(FAST_CW);
    delay(500);
  }
  servo1.write(STOP);
  servo2.write(STOP);
  delay(1000);
}