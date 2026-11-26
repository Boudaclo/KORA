#pragma once
#include <Arduino.h>
#include "config.h"

// Un "chunk" = jusqu'à 10 trames Codec2 (400 ms de voix)
struct VoiceChunk {
  uint8_t frames;
  uint8_t data[FRAMES_PER_PACKET * FRAME_BYTES];
};

// Format du paquet radio :
//   en-tête (10 o) : type, id appareil, sel de démarrage (4 o), compteur (4 o)
//   voix chiffrée  : frames * 6 octets
//   tag GCM        : 8 octets (authentifie l'en-tête et la voix)
constexpr size_t HEADER_LEN = 10;
constexpr size_t TAG_LEN    = 8;
constexpr size_t MAX_PACKET = HEADER_LEN + FRAMES_PER_PACKET * FRAME_BYTES + TAG_LEN;

void   linkInit();
size_t linkEncode(const VoiceChunk& in, uint8_t* out);                  // retourne la longueur du paquet
bool   linkDecode(const uint8_t* pkt, size_t len, VoiceChunk& out);     // faux si invalide ou rejoué
