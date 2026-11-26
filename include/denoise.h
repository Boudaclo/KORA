#pragma once
#include <Arduino.h>

// Filtre anti-bruit ESP-SR (NSNet) appliqué à la voix du micro, à 16 kHz, avant Codec2.
// Si ESP-SR n'est pas disponible à la compilation ou si le modèle est absent de la flash,
// la voix passe sans filtre (denoiseActive() renvoie faux).

bool denoiseInit();     // à appeler une fois, depuis la tâche audio
void denoiseReset();    // à appeler au début de chaque émission (vide les tampons)
bool denoiseActive();

// Traite MIC_BLOCK échantillons (640 à 16 kHz, 16 bits) et en renvoie autant.
// Ajoute un retard fixe d'un bloc NSNet (32 ms).
void denoiseProcess(const int16_t* in, int16_t* out);
