#include <Arduino.h>
#include "app.h"
#include "config.h"
#include "link.h"

QueueHandle_t txQueue;
QueueHandle_t rxQueue;
volatile bool pttPressed = false;
volatile bool txBusy = false;

static void fatalBlink() {
  for (;;) {
    digitalWrite(PIN_LED_TX, HIGH); delay(150);
    digitalWrite(PIN_LED_TX, LOW);  delay(150);
  }
}

void setup() {
  Serial.begin(115200);
  delay(300);
  Serial.printf("\nTalkie LoRa - appareil %d\n", DEVICE_ID);

  pinMode(PIN_PTT, INPUT_PULLUP);
  pinMode(PIN_LED_TX, OUTPUT);
  digitalWrite(PIN_LED_TX, LOW);

  linkInit();
  txQueue = xQueueCreate(6, sizeof(VoiceChunk));
  rxQueue = xQueueCreate(6, sizeof(VoiceChunk));

#ifndef LOOPBACK_TEST
  if (!radioInit()) fatalBlink();                     // LED qui clignote vite = radio non détectée
  xTaskCreatePinnedToCore(radioTask, "radio", 8192, nullptr, 4, nullptr, 0);
#else
  Serial.println("MODE LOOPBACK : pas de radio, enregistre pendant PTT et rejoue au relachement");
#endif
  xTaskCreatePinnedToCore(audioTask, "audio", 16384, nullptr, 5, nullptr, 1);
}

void loop() {
  // Antirebond du bouton PTT
  static bool lastRaw = false, stable = false;
  static uint32_t tChange = 0;
  const bool raw = (digitalRead(PIN_PTT) == LOW);
  if (raw != lastRaw) { lastRaw = raw; tChange = millis(); }
  if (millis() - tChange > DEBOUNCE_MS && stable != raw) {
    stable = raw;
    pttPressed = stable;
    Serial.println(stable ? "PTT appuye" : "PTT relache");
  }

  // LED émission : allumée pendant tout l'envoi
  digitalWrite(PIN_LED_TX, (pttPressed || txBusy) ? HIGH : LOW);
  delay(5);
}
