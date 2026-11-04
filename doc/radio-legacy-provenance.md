# Définitions du pilote radio

Le projet utilisait déjà `src/RF/RF24_forked.cpp`, dérivé de RF24. La dépendance
externe nRF24/RF24 compilait en plus une seconde implémentation inutilisée et son
manifeste JSON était invalide. Elle est retirée de PlatformIO.

Les définitions nécessaires restent celles du commit
`2af44fcac5b0022780a18fddde593359194c634d` de nRF24/RF24 :

- `include/nRF24L01.h` et `include/printf.h` : copies conservant les notices originales ;
- `include/RF/legacyTypes.h` : valeurs des trois énumérations publiques ;
- `include/RF24_config.h` : adaptation minimale aux deux cibles réellement prises
  en charge ici, AVR Mega et ESP8266 avec SPI matériel. Délais, débit SPI et
  gestion des délais d'erreur conservent les valeurs de cette révision.

Le pilote dérivé, la configuration et `printf.h` portent la licence GPL version 2
de leurs auteurs, reproduite dans `doc/GPL-2.0.txt`. `nRF24L01.h` conserve sa
licence permissive propre. La licence Apache du dépôt ne remplace pas ces notices.
Les versions TCP sont également unifiées sur la dépendance ESP8266 déclarée par
ESPAsyncWebServer 3.3.7 : ESPAsyncTCP-esphome 2.0.0.
