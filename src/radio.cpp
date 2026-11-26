#include <RadioLib.h>
#include <SPI.h>
#include "app.h"
#include "config.h"
#include "link.h"

static SX1276 radio = new Module(PIN_LORA_NSS, PIN_LORA_DIO0, PIN_LORA_RST, PIN_LORA_DIO1, SPI);
static volatile bool irqFlag = false;

static void IRAM_ATTR onRadioIrq() { irqFlag = true; }

bool radioInit() {
  SPI.begin(PIN_LORA_SCK, PIN_LORA_MISO, PIN_LORA_MOSI, PIN_LORA_NSS);
  int16_t st = radio.begin(LORA_FREQ_MHZ, LORA_BW_KHZ, LORA_SF, LORA_CR,
                           LORA_SYNC_WORD, LORA_POWER_DBM, LORA_PREAMBLE);
  if (st != RADIOLIB_ERR_NONE) {
    Serial.printf("Radio : echec d'initialisation (code %d)\n", st);
    return false;
  }
  radio.setCRC(true);
  Serial.printf("Radio OK : %.3f MHz, SF%d, BW %.0f kHz, %d dBm\n",
                LORA_FREQ_MHZ, LORA_SF, LORA_BW_KHZ, LORA_POWER_DBM);
  return true;
}

static void enterRx() {
  irqFlag = false;
  radio.setDio0Action(onRadioIrq, RISING);
  radio.startReceive();
}

static void enterTx() {
  radio.clearDio0Action();   // transmit() est bloquant : on coupe l'interruption de réception
  radio.standby();
}

void radioTask(void*) {
  static uint8_t pkt[MAX_PACKET];
  VoiceChunk chunk;
  bool inTx = false;
  enterRx();

  for (;;) {
    // ----- Émission : tant que PTT est tenu, puis jusqu'à vider la file -----
    const bool wantTx = txBusy || uxQueueMessagesWaiting(txQueue) > 0;
    if (wantTx) {
      if (!inTx) { enterTx(); inTx = true; }
      if (xQueueReceive(txQueue, &chunk, pdMS_TO_TICKS(20)) == pdTRUE) {
        size_t n = linkEncode(chunk, pkt);
        if (n > 0) {
          int16_t st = radio.transmit(pkt, n);
          if (st != RADIOLIB_ERR_NONE) Serial.printf("TX erreur %d\n", st);
        }
      }
      continue;
    }

    // ----- Réception -----
    if (inTx) { enterRx(); inTx = false; }
    if (irqFlag) {
      irqFlag = false;
      size_t len = radio.getPacketLength();
      if (len > 0 && len <= sizeof(pkt)) {
        int16_t st = radio.readData(pkt, len);
        if (st == RADIOLIB_ERR_NONE) {
          VoiceChunk rx;
          if (linkDecode(pkt, len, rx)) {
            xQueueSend(rxQueue, &rx, 0);
            Serial.printf("RX %u trames, RSSI %.0f dBm, SNR %.1f dB\n",
                          rx.frames, radio.getRSSI(), radio.getSNR());
          }
        }
      }
      radio.startReceive();
    }
    vTaskDelay(pdMS_TO_TICKS(2));
  }
}
