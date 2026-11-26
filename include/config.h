#pragma once
#include <Arduino.h>

// ---------- Identité de l'appareil (défini par platformio.ini) ----------
#ifndef DEVICE_ID
#define DEVICE_ID 1
#endif

// ---------- Broches ESP32-S3 DevKitC-1 (voir schéma de câblage) ----------
// Radio SX1276 (SPI)
constexpr int PIN_LORA_NSS  = 10;
constexpr int PIN_LORA_MOSI = 11;
constexpr int PIN_LORA_SCK  = 12;
constexpr int PIN_LORA_MISO = 13;
constexpr int PIN_LORA_RST  = 14;
constexpr int PIN_LORA_DIO0 = 21;
constexpr int PIN_LORA_DIO1 = 47;   // facultatif
// Micro INMP441 (I2S entrée)
constexpr int PIN_MIC_SCK = 4;
constexpr int PIN_MIC_WS  = 5;
constexpr int PIN_MIC_SD  = 6;
// Ampli MAX98357A (I2S sortie)
constexpr int PIN_SPK_BCLK = 15;
constexpr int PIN_SPK_LRC  = 16;
constexpr int PIN_SPK_DIN  = 17;
// Commandes
constexpr int PIN_PTT    = 7;       // bouton vers GND (INPUT_PULLUP)
constexpr int PIN_LED_TX = 8;       // LED via 220 ohms vers GND
constexpr uint32_t DEBOUNCE_MS = 30;

// ---------- Paramètres LoRa (à valider selon la réglementation locale) ----------
constexpr float    LORA_FREQ_MHZ   = 433.175f;
constexpr float    LORA_BW_KHZ     = 125.0f;
constexpr uint8_t  LORA_SF         = 7;
constexpr uint8_t  LORA_CR         = 5;      // 4/5
constexpr uint8_t  LORA_SYNC_WORD  = 0x12;   // réseau privé
constexpr int8_t   LORA_POWER_DBM  = 10;     // 10 dBm pour les premiers essais
constexpr uint16_t LORA_PREAMBLE   = 8;

// ---------- Audio / Codec2 (mode 1200 bit/s) ----------
constexpr int MIC_RATE         = 16000;  // capture INMP441, ramenée à 8 kHz pour Codec2
constexpr int SPK_RATE         = 8000;   // lecture
constexpr int FRAME_SAMPLES    = 320;    // 40 ms à 8 kHz
constexpr int FRAME_BYTES      = 6;      // octets Codec2 par trame
constexpr int FRAMES_PER_PACKET = 10;    // 10 trames = 400 ms de voix par paquet radio
constexpr int MIC_BLOCK      = FRAME_SAMPLES * 2;   // 640 échantillons à 16 kHz = 40 ms
constexpr int MIC_SHIFT        = 14;     // gain du micro : 16 = faible, 14 = +12 dB
constexpr int SPK_GAIN_PERCENT = 100;    // volume de lecture
constexpr int PREFILL_MS       = 200;    // silence avant lecture (tampon anti-saccades)
constexpr int RX_TIMEOUT_MS    = 800;    // sans paquet pendant ce délai : fin de réception

// ---------- Filtre anti-bruit ESP-SR (NSNet) ----------
// Actif seulement si ESP-SR est disponible à la compilation et si le modèle est dans la flash (voir README).
// Sinon la voix passe sans filtre. Mettre -DNS_ENABLED=0 pour le désactiver.
#ifndef NS_ENABLED
#define NS_ENABLED 1
#endif

// Détection de voix par énergie (n'envoie pas les paquets entièrement silencieux)
constexpr bool  VAD_ENABLED       = false;
constexpr float VAD_RMS_THRESHOLD = 300.0f;

// ---------- Chiffrement AES-256-GCM ----------
// ATTENTION : changer cette clé (32 octets) et la mettre IDENTIQUE sur les deux appareils.
static const uint8_t AES_KEY[32] = {
  0x00,0x11,0x22,0x33,0x44,0x55,0x66,0x77,0x88,0x99,0xAA,0xBB,0xCC,0xDD,0xEE,0xFF,
  0xFF,0xEE,0xDD,0xCC,0xBB,0xAA,0x99,0x88,0x77,0x66,0x55,0x44,0x33,0x22,0x11,0x00
};
