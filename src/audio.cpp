#include <driver/i2s.h>
#include "app.h"
#include "config.h"
#include "link.h"
#include "codec2.h"
#include "denoise.h"

static const i2s_port_t MIC_PORT = I2S_NUM_0;
static const i2s_port_t SPK_PORT = I2S_NUM_1;

// ---------------- Initialisation I2S ----------------
static void micInit() {
  i2s_config_t c = {};
  c.mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_RX);
  c.sample_rate = MIC_RATE;
  c.bits_per_sample = I2S_BITS_PER_SAMPLE_32BIT;      // l'INMP441 sort 24 bits dans 32
  c.channel_format = I2S_CHANNEL_FMT_ONLY_LEFT;       // broche L/R de l'INMP441 à GND
  c.communication_format = I2S_COMM_FORMAT_STAND_I2S;
  c.intr_alloc_flags = ESP_INTR_FLAG_LEVEL1;
  c.dma_buf_count = 8;
  c.dma_buf_len = 512;
  c.use_apll = false;

  i2s_pin_config_t p = {};
  p.mck_io_num = I2S_PIN_NO_CHANGE;
  p.bck_io_num = PIN_MIC_SCK;
  p.ws_io_num = PIN_MIC_WS;
  p.data_out_num = I2S_PIN_NO_CHANGE;
  p.data_in_num = PIN_MIC_SD;

  ESP_ERROR_CHECK(i2s_driver_install(MIC_PORT, &c, 0, nullptr));
  ESP_ERROR_CHECK(i2s_set_pin(MIC_PORT, &p));
  i2s_stop(MIC_PORT);                                 // le micro ne tourne qu'en émission
}

static void spkInit() {
  i2s_config_t c = {};
  c.mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX);
  c.sample_rate = SPK_RATE;
  c.bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT;
  c.channel_format = I2S_CHANNEL_FMT_RIGHT_LEFT;      // on envoie G+D identiques (SD du MAX98357A libre = moyenne G/D)
  c.communication_format = I2S_COMM_FORMAT_STAND_I2S;
  c.intr_alloc_flags = ESP_INTR_FLAG_LEVEL1;
  c.dma_buf_count = 8;
  c.dma_buf_len = 256;
  c.use_apll = false;
  c.tx_desc_auto_clear = true;                        // silence en cas de manque de données

  i2s_pin_config_t p = {};
  p.mck_io_num = I2S_PIN_NO_CHANGE;
  p.bck_io_num = PIN_SPK_BCLK;
  p.ws_io_num = PIN_SPK_LRC;
  p.data_out_num = PIN_SPK_DIN;
  p.data_in_num = I2S_PIN_NO_CHANGE;

  ESP_ERROR_CHECK(i2s_driver_install(SPK_PORT, &c, 0, nullptr));
  ESP_ERROR_CHECK(i2s_set_pin(SPK_PORT, &p));
}

// ---------------- Capture micro ----------------
static bool micReadBlock(int32_t* buf, size_t samples) {
  const size_t need = samples * sizeof(int32_t);
  size_t got = 0;
  while (got < need) {
    size_t r = 0;
    esp_err_t e = i2s_read(MIC_PORT, (uint8_t*)buf + got, need - got, &r, pdMS_TO_TICKS(200));
    if (e != ESP_OK || r == 0) return false;
    got += r;
  }
  return true;
}

static float hpX = 0, hpY = 0;   // filtre passe-haut (retire la composante continue du micro)

// 640 échantillons 32 bits -> 640 échantillons 16 bits (gain, passe-haut, écrêtage), toujours à 16 kHz
static void micToPcm16k(const int32_t* raw, int16_t* out) {
  for (int i = 0; i < MIC_BLOCK; i++) {
    float x = (float)(raw[i] >> MIC_SHIFT);
    float y = x - hpX + 0.995f * hpY;
    hpX = x;
    hpY = y;
    int32_t v = (int32_t)y;
    if (v > 32767) v = 32767;
    if (v < -32768) v = -32768;
    out[i] = (int16_t)v;
  }
}

// 16 kHz -> 8 kHz (moyenne de 2 échantillons). Retourne le niveau RMS de la trame.
static float decimateTo8k(const int16_t* in, int16_t* out) {
  float sum = 0;
  for (int i = 0; i < FRAME_SAMPLES; i++) {
    int32_t v = ((int32_t)in[2 * i] + (int32_t)in[2 * i + 1]) / 2;
    out[i] = (int16_t)v;
    sum += (float)v * (float)v;
  }
  return sqrtf(sum / FRAME_SAMPLES);
}

// ---------------- Lecture ----------------
static void playSilence(int ms) {
  static int16_t zeros[256 * 2] = {0};
  int frames = SPK_RATE * ms / 1000;                  // échantillons (stéréo = x2)
  while (frames > 0) {
    int n = frames > 256 ? 256 : frames;
    size_t w = 0;
    i2s_write(SPK_PORT, zeros, n * 2 * sizeof(int16_t), &w, portMAX_DELAY);
    frames -= n;
  }
}

static void playFrame(const int16_t* pcm) {
  static int16_t st[FRAME_SAMPLES * 2];
  for (int i = 0; i < FRAME_SAMPLES; i++) {
    int32_t v = (int32_t)pcm[i] * SPK_GAIN_PERCENT / 100;
    if (v > 32767) v = 32767;
    if (v < -32768) v = -32768;
    st[2 * i] = st[2 * i + 1] = (int16_t)v;
  }
  size_t w = 0;
  i2s_write(SPK_PORT, st, sizeof(st), &w, portMAX_DELAY);
}

// ---------------- Tâche audio ----------------
void audioTask(void*) {
  micInit();
  spkInit();

  struct CODEC2* c2 = codec2_create(CODEC2_MODE_1200);
  if (!c2 || codec2_samples_per_frame(c2) != FRAME_SAMPLES || codec2_bytes_per_frame(c2) != FRAME_BYTES) {
    Serial.println("Codec2 : initialisation impossible ou mode inattendu");
    vTaskDelete(nullptr);
    return;
  }

  denoiseInit();                                      // filtre anti-bruit ESP-SR si disponible

  static int32_t raw[MIC_BLOCK];
  static int16_t pcm16[MIC_BLOCK];
  static int16_t clean16[MIC_BLOCK];
  static int16_t pcm[FRAME_SAMPLES];
  VoiceChunk chunk;
  chunk.frames = 0;
  bool anyVoice = false;
  bool wasPtt = false;
  bool playing = false;
  uint32_t lastRx = 0;
  uint32_t dropped = 0;

#ifdef LOOPBACK_TEST
  QueueHandle_t outQueue = rxQueue;                   // test : ce qui est enregistré est rejoué
#else
  QueueHandle_t outQueue = txQueue;
#endif

  for (;;) {
    const bool ptt = pttPressed;

    // ----- Début d'émission -----
    if (ptt && !wasPtt) {
      playing = false;
      xQueueReset(rxQueue);
      i2s_zero_dma_buffer(SPK_PORT);                  // coupe immédiatement ce qui jouait
      hpX = hpY = 0;
      denoiseReset();
      chunk.frames = 0;
      anyVoice = false;
      i2s_start(MIC_PORT);
      txBusy = true;
    }
    // ----- Fin d'émission : on envoie le paquet partiel restant -----
    if (!ptt && wasPtt) {
      i2s_stop(MIC_PORT);
      if (chunk.frames > 0 && (!VAD_ENABLED || anyVoice)) {
        xQueueSend(outQueue, &chunk, 0);
      }
      chunk.frames = 0;
      txBusy = false;                                 // après l'envoi dans la file : pas de trou côté radio
    }
    wasPtt = ptt;

    // ----- Émission : micro -> Codec2 -> paquet -----
    if (ptt) {
      if (!micReadBlock(raw, MIC_BLOCK)) continue;
      micToPcm16k(raw, pcm16);
      denoiseProcess(pcm16, clean16);                 // anti-bruit à 16 kHz
      float rms = decimateTo8k(clean16, pcm);         // puis 8 kHz pour Codec2
      if (rms > VAD_RMS_THRESHOLD) anyVoice = true;
      codec2_encode(c2, chunk.data + chunk.frames * FRAME_BYTES, pcm);
      if (++chunk.frames == FRAMES_PER_PACKET) {
        if (!VAD_ENABLED || anyVoice) {
          if (xQueueSend(outQueue, &chunk, 0) != pdTRUE) {
            if (++dropped % 5 == 1) Serial.printf("File pleine, paquets perdus : %u\n", (unsigned)dropped);
          }
        }
        chunk.frames = 0;
        anyVoice = false;
      }
      continue;
    }

    // ----- Réception : paquet -> Codec2 -> haut-parleur -----
    VoiceChunk rx;
    if (xQueueReceive(rxQueue, &rx, pdMS_TO_TICKS(20)) == pdTRUE) {
      uint32_t now = millis();
      if (!playing || now - lastRx > RX_TIMEOUT_MS) {
        playSilence(PREFILL_MS);                      // petit tampon pour absorber la gigue
        playing = true;
      }
      lastRx = now;
      for (uint8_t f = 0; f < rx.frames; f++) {
        if (pttPressed) break;                        // PTT pressé : on arrête de parler à l'autre
        codec2_decode(c2, pcm, rx.data + f * FRAME_BYTES);
        playFrame(pcm);
      }
    } else if (playing && millis() - lastRx > RX_TIMEOUT_MS) {
      playing = false;
    }
  }
}
