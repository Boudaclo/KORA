#pragma once
#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>

// Files d'échange entre les tâches (éléments de type VoiceChunk, voir link.h)
extern QueueHandle_t txQueue;          // audio -> radio
extern QueueHandle_t rxQueue;          // radio -> audio

extern volatile bool pttPressed;       // état du bouton PTT (antirebond fait dans loop())
extern volatile bool txBusy;           // vrai de l'appui PTT jusqu'à l'envoi du dernier paquet

void audioTask(void*);                 // capture, Codec2, lecture (coeur 1)
void radioTask(void*);                 // chiffrement et LoRa (coeur 0)
bool radioInit();
