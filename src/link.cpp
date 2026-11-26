#include "link.h"
#include <esp_system.h>
#include "mbedtls/gcm.h"

static mbedtls_gcm_context gcm;
static uint32_t bootSalt = 0;
static uint32_t txCounter = 0;

// Dernier état connu de l'appareil distant (protection contre le rejeu)
static bool     peerKnown = false;
static uint32_t peerSalt = 0;
static uint32_t peerCounter = 0;

static constexpr uint8_t PKT_VOICE = 0xA1;

void linkInit() {
  mbedtls_gcm_init(&gcm);
  mbedtls_gcm_setkey(&gcm, MBEDTLS_CIPHER_ID_AES, AES_KEY, 256);
  bootSalt = esp_random();   // change à chaque démarrage : évite de réutiliser un nonce
  txCounter = 0;
}

// Nonce GCM de 12 octets : id | sel | compteur | 0 0 0
static void makeNonce(uint8_t id, uint32_t salt, uint32_t ctr, uint8_t nonce[12]) {
  memset(nonce, 0, 12);
  nonce[0] = id;
  memcpy(nonce + 1, &salt, 4);
  memcpy(nonce + 5, &ctr, 4);
}

size_t linkEncode(const VoiceChunk& in, uint8_t* out) {
  const size_t n = (size_t)in.frames * FRAME_BYTES;
  out[0] = PKT_VOICE;
  out[1] = DEVICE_ID;
  memcpy(out + 2, &bootSalt, 4);
  memcpy(out + 6, &txCounter, 4);

  uint8_t nonce[12];
  makeNonce(DEVICE_ID, bootSalt, txCounter, nonce);

  int rc = mbedtls_gcm_crypt_and_tag(&gcm, MBEDTLS_GCM_ENCRYPT, n,
                                     nonce, sizeof(nonce),
                                     out, HEADER_LEN,               // données authentifiées : l'en-tête
                                     in.data, out + HEADER_LEN,     // voix -> voix chiffrée
                                     TAG_LEN, out + HEADER_LEN + n);
  if (rc != 0) {
    Serial.printf("GCM encode erreur %d\n", rc);
    return 0;
  }
  txCounter++;
  return HEADER_LEN + n + TAG_LEN;
}

bool linkDecode(const uint8_t* pkt, size_t len, VoiceChunk& out) {
  if (len < HEADER_LEN + FRAME_BYTES + TAG_LEN) return false;
  const size_t n = len - HEADER_LEN - TAG_LEN;
  if (n % FRAME_BYTES != 0 || n / FRAME_BYTES > FRAMES_PER_PACKET) return false;
  if (pkt[0] != PKT_VOICE) return false;
  const uint8_t id = pkt[1];
  if (id == DEVICE_ID) return false;                      // ignorer ses propres paquets

  uint32_t salt, ctr;
  memcpy(&salt, pkt + 2, 4);
  memcpy(&ctr, pkt + 6, 4);

  // Rejeu : même session mais compteur déjà vu ou plus ancien
  if (peerKnown && salt == peerSalt && ctr <= peerCounter) return false;

  uint8_t nonce[12];
  makeNonce(id, salt, ctr, nonce);

  int rc = mbedtls_gcm_auth_decrypt(&gcm, n,
                                    nonce, sizeof(nonce),
                                    pkt, HEADER_LEN,
                                    pkt + HEADER_LEN + n, TAG_LEN,   // tag reçu
                                    pkt + HEADER_LEN, out.data);
  if (rc != 0) return false;                              // mauvaise clé ou paquet altéré

  peerKnown = true;
  peerSalt = salt;
  peerCounter = ctr;
  out.frames = (uint8_t)(n / FRAME_BYTES);
  return true;
}
