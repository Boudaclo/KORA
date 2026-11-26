# Talkie-walkie LoRa vocal – firmware

ESP32-S3 + SX1276 (433 MHz) + micro INMP441 + ampli MAX98357A. Voix compressée avec Codec2 (1200 bit/s), chiffrée en AES-256-GCM, envoyée en LoRa. Fonctionnement en alternat (bouton PTT).

> **État** : code écrit d'après les bibliothèques et le câblage du projet, **non compilé ni testé sur le matériel**. Prévoir des petits ajustements à la première compilation.

## Fichiers

| Fichier | Rôle |
|---|---|
| `platformio.ini` | Cartes, bibliothèques, 3 environnements : `talkie_a`, `talkie_b`, `loopback` |
| `include/config.h` | **Toutes les broches et les réglages** (radio, audio, clé AES) |
| `src/main.cpp` | Démarrage, bouton PTT (antirebond), LED |
| `src/audio.cpp` | I2S micro et haut-parleur, Codec2, lecture avec tampon |
| `src/link.cpp` | Format de paquet, AES-GCM, protection contre le rejeu |
| `src/radio.cpp` | SX1276 avec RadioLib : émission et réception |
| `src/denoise.cpp` | Filtre anti-bruit ESP-SR (NSNet), avec repli sans filtre |
| `partitions_sr.csv` | Table de partitions avec la partition `model` d'ESP-SR |

## Mise en route

1. Installer PlatformIO (extension VS Code).
2. **Changer la clé AES** dans `config.h` (32 octets, identique sur les deux appareils).
3. Vérifier `board_build.arduino.memory_type` dans `platformio.ini` selon ton ESP32-S3 (N8R8 / N16R8 : `qio_opi`, N8R2 : `qio_qspi`). Un mauvais réglage provoque un redémarrage en boucle.
4. Téléverser : appareil A `pio run -e talkie_a -t upload`, appareil B `pio run -e talkie_b -t upload`.
5. Moniteur série : `pio device monitor` (115200 bauds, port USB « UART » de la carte).

## Ordre de test conseillé

1. `loopback` (sans radio) : appuyer sur PTT, parler, relâcher. La voix est rejouée. Cela valide micro, Codec2 et haut-parleur.
2. `talkie_a` et `talkie_b` à quelques mètres, antennes branchées : parler sur A, écouter sur B. Le moniteur de B affiche `RX ... RSSI ... SNR`.
3. Augmenter la distance, puis la puissance (`LORA_POWER_DBM`) dans les limites légales.

## Réglages utiles (`config.h`)

- `MIC_SHIFT` : 14 par défaut. Voix trop faible : 13 ou 12. Saturation : 15 ou 16.
- `SPK_GAIN_PERCENT` : volume de lecture.
- `PREFILL_MS` : tampon avant lecture. Voix hachée : augmenter (300 à 400). Latence trop grande : diminuer.
- `VAD_ENABLED` : n'envoie pas les paquets silencieux. Régler `VAD_RMS_THRESHOLD` après avoir observé le niveau.
- `LORA_FREQ_MHZ`, `LORA_POWER_DBM` : à valider selon la réglementation locale.

## Chiffres de fonctionnement (estimations)

- 10 trames Codec2 = 400 ms de voix = 60 octets. Paquet complet : 10 (en-tête) + 60 + 8 (tag) = 78 octets, environ 0,14 s en l'air à SF7 / 125 kHz.
- Latence de bout en bout : environ 0,8 s.

## Limites connues

- Alternat uniquement, pas d'écoute avant d'émettre : deux appuis simultanés se brouillent.
- Protection contre le rejeu limitée : un paquet d'une ancienne session (autre sel de démarrage) est accepté.
- Tag GCM tronqué à 8 octets pour gagner du temps d'antenne. Si `mbedtls` refuse, passer `TAG_LEN` à 16 dans `link.h`.
- Bibliothèque Codec2 : `arduino-libcodec2` (pschatzmann). Si le nom du fichier d'en-tête ou l'API diffère, ajuster l'`#include` dans `audio.cpp`.
- Détection de mot-clé : pas encore faite. Point d'extension : dans `audioTask`, après `decimateTo8k()`, les échantillons `pcm` (8 kHz, 16 bits).

## Filtre anti-bruit ESP-SR (NSNet)

Le filtre se place **avant Codec2**, à 16 kHz : micro → filtre → passage à 8 kHz → Codec2. Il ajoute 32 ms de retard (un bloc NSNet de 512 échantillons).

**État de l'intégration**
- Le module `denoise.cpp` suit l'API décrite dans la documentation ESP-SR actuelle (`esp_nsn_iface.h`, `create`, `process`, `get_samp_chunksize`).
- La logique de tampon (blocs micro de 640 échantillons contre blocs NSNet de 512) est testée sur PC avec un faux filtre : 0 erreur.
- **Non testé avec le vrai ESP-SR ni sur la carte.** NSNet est annoncé pour l'ESP32-S3.
- Si ESP-SR ou son modèle manque, le programme démarre quand même : le moniteur série affiche `NS : ... voix non filtree` et la voix passe sans filtre.

**Pour l'activer**
1. Compiler avec `talkie_a_ns` / `talkie_b_ns` (pioarduino, Arduino-ESP32 3.x). Les environnements `talkie_a` / `talkie_b` d'origine restent sans filtre.
2. Au premier démarrage, lire le moniteur série. Si `NS : ESP-SR NSNet non disponible dans cette compilation` apparaît, les en-têtes d'ESP-SR ne sont pas dans le framework utilisé. Vérifier avec : `find ~/.platformio -name esp_nsn_iface.h`.
3. Le **modèle** doit être dans la flash, partition `model` (fournie dans `partitions_sr.csv`, 4 Mo à l'adresse `0x310000`). Choisir `nsnet2` dans `idf.py menuconfig` → *ESP Speech Recognition* → modèle de suppression de bruit, puis générer `srmodels.bin` avec le script `movemodel.py` d'ESP-SR (voir la page « Model Selection and Loading » de la documentation ESP-SR).
4. Téléverser le modèle : `esptool.py --chip esp32s3 write_flash 0x310000 srmodels.bin`. Si `srmodels.bin` dépasse 4 Mo, agrandir la partition `model`.
5. Surveiller le temps de calcul affiché toutes les 10 s : `NS : ... us par bloc`. Il doit rester nettement sous 32 000 µs (budget d'un bloc), sinon le filtre est trop lent avec Codec2 sur le même coeur. Désactivation : `-DNS_ENABLED=0`.

**Si les en-têtes ESP-SR manquent dans pioarduino** : la voie de secours est de passer le projet en composant ESP-IDF avec `espressif/esp-sr` (gestionnaire de composants), ce qui demande de réorganiser le projet.

**Si le démarrage échoue avec une erreur I2S** (conflit entre ancien et nouveau pilote sous Arduino-ESP32 3.x) : il faudra migrer `audio.cpp` vers l'API `i2s_std`.
