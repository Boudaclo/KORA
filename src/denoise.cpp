#include "denoise.h"
#include "config.h"
#include <esp_timer.h>

// ESP-SR NSNet n'est utilisé que si ses en-têtes existent dans cette compilation
#if defined(__has_include)
#if __has_include("esp_nsn_iface.h")
#if NS_ENABLED
#define HAVE_NSNET 1
#endif
#endif
#endif

#ifdef HAVE_NSNET

#include "esp_nsn_iface.h"
#include "esp_nsn_models.h"
#include "model_path.h"

static const esp_nsn_iface_t* nsn = nullptr;
static esp_nsn_data_t* nsnData = nullptr;
static int chunk = 0;                       // 512 échantillons (32 ms à 16 kHz) d'après la doc ESP-SR

// Deux files d'attente entre les blocs du micro (640) et les blocs NSNet (512)
static int16_t inBuf[4096];
static int16_t outBuf[4096];
static int inCount = 0;
static int outCount = 0;

// Mesure du temps de calcul
static uint32_t nChunks = 0;
static int64_t sumUs = 0, maxUs = 0;

bool denoiseInit() {
  srmodel_list_t* models = esp_srmodel_init("model");
  if (!models) {
    Serial.println("NS : partition 'model' introuvable (voir README) - voix non filtree");
    return false;
  }
  char* name = esp_srmodel_filter(models, ESP_NSNET_PREFIX, NULL);
  if (!name) {
    Serial.println("NS : modele NSNet absent de la partition 'model' - voix non filtree");
    return false;
  }
  nsn = esp_nsnet_handle_from_name(name);
  if (!nsn) {
    Serial.println("NS : interface NSNet introuvable - voix non filtree");
    return false;
  }
  nsnData = nsn->create(name);
  if (!nsnData) {
    Serial.println("NS : creation du filtre impossible (memoire ?) - voix non filtree");
    return false;
  }
  chunk = nsn->get_samp_chunksize(nsnData);
  if (chunk <= 0 || chunk > 1024) {
    Serial.printf("NS : taille de bloc inattendue (%d) - voix non filtree\n", chunk);
    nsn->destroy(nsnData);
    nsnData = nullptr;
    return false;
  }
  denoiseReset();
  Serial.printf("NS : %s actif, blocs de %d echantillons\n", name, chunk);
  return true;
}

void denoiseReset() {
  inCount = 0;
  if (chunk > 0) {
    memset(outBuf, 0, chunk * sizeof(int16_t));   // silence de départ : garantit qu'on ne manque jamais de sortie
    outCount = chunk;
  } else {
    outCount = 0;
  }
}

bool denoiseActive() { return nsnData != nullptr; }

void denoiseProcess(const int16_t* in, int16_t* out) {
  if (!nsnData) {
    memcpy(out, in, MIC_BLOCK * sizeof(int16_t));
    return;
  }

  memcpy(inBuf + inCount, in, MIC_BLOCK * sizeof(int16_t));
  inCount += MIC_BLOCK;

  while (inCount >= chunk) {
    const int64_t t0 = esp_timer_get_time();
    nsn->process(nsnData, inBuf, outBuf + outCount);
    const int64_t dt = esp_timer_get_time() - t0;
    outCount += chunk;
    inCount -= chunk;
    memmove(inBuf, inBuf + chunk, inCount * sizeof(int16_t));

    sumUs += dt;
    if (dt > maxUs) maxUs = dt;
    if (++nChunks % 300 == 0) {                   // environ toutes les 10 s
      const int64_t budget = (int64_t)chunk * 1000000 / MIC_RATE;
      Serial.printf("NS : %lld us par bloc en moyenne, max %lld us (budget %lld us)%s\n",
                    (long long)(sumUs / 300), (long long)maxUs, (long long)budget,
                    (sumUs / 300 > budget * 8 / 10) ? "  ATTENTION : trop lent" : "");
      sumUs = 0;
      maxUs = 0;
    }
  }

  const int take = outCount < MIC_BLOCK ? outCount : MIC_BLOCK;
  memcpy(out, outBuf, take * sizeof(int16_t));
  if (take < MIC_BLOCK) memset(out + take, 0, (MIC_BLOCK - take) * sizeof(int16_t));
  outCount -= take;
  memmove(outBuf, outBuf + take, outCount * sizeof(int16_t));
}

#else  // ESP-SR NSNet indisponible : la voix passe telle quelle

bool denoiseInit() {
  Serial.println("NS : ESP-SR NSNet non disponible dans cette compilation - voix non filtree");
  return false;
}
void denoiseReset() {}
bool denoiseActive() { return false; }
void denoiseProcess(const int16_t* in, int16_t* out) {
  memcpy(out, in, MIC_BLOCK * sizeof(int16_t));
}

#endif
