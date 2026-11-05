# Lire et maintenir ce fork

Ce firmware relie une console série/Telnet et MQTT à un unique NRF24. Le code radio conserve des séquences observées sur l'installation; une simplification qui semble équivalente au niveau C++ peut changer les échanges physiques.

## Parcours de lecture

| Fichier | Responsabilité et choix principal |
| --- | --- |
| `src/main.cpp` | Initialisation, réception MQTT et ordre des travaux de la boucle. |
| `include/reliability.h` | Validation partagée: nombres bornés, noms, messages MQTT et arguments de console. |
| `src/commands/callbacks.cpp` | Commandes série/Telnet et remplacement cohérent de la configuration active. |
| `src/RF/device.cpp` | Configuration d'un équipement, disponibilité et dernier état; aucune possession de la radio. |
| `src/RF/e2bp.cpp` | Construction des neuf octets de commande et échange radio synchrone. |
| `include/RF/shutterFeedback.h` | Interprétation des retours avec un historique propre à chaque volet. |
| `include/RF/stopVerification.h`, `src/postStopPolling.cpp` | Vérification différée et bornée après STOP, sans nouveau STOP automatique. |
| `src/RF/irqManager.cpp`, `src/RF/RF24_forked.cpp` | Routage de l'interruption et accès SPI bas niveau. |
| `src/RF/irqTransport.cpp`, `include/RF/radioCritical.h` | Transport IRQ en RAM et exclusion courte sauvegardant l'état des interruptions. |
| `src/RF/pairing.cpp`, `copy.cpp`, `scanner.cpp` | Modes spéciaux partageant le même NRF. |
| `src/net/mqtt.cpp` | Transport MQTT, reconnexion, abonnements et disponibilité de passerelle. |
| `src/net/mqttHass.cpp` | Decouverte Home Assistant et republication progressive des états mémorisés. |
| `src/net/wifi.cpp` | Conservation des identifiants station et reconnexion sans effacement implicite. |
| `src/net/webserver.cpp` | Validation HTTP puis mise en attente d'une configuration appliquée depuis la boucle. |
| `src/net/mqttConfig.cpp`, `src/storage/yokisLittleFS.cpp` | Persistance MQTT et montage LittleFS sans formatage automatique. |
| `src/serial/serialHelper.cpp` | Lecture de lignes bornée, traitement LF/CRLF et rejet de toute ligne débordante. |
| `src/serial/adminConsole.cpp` | Authentification TCP, assemblage séparé des lignes USB/TCP et limitation des entrées/sorties. |
| `tools/build_provenance.py` | Identité des sources publiques et reçus associant ces sources aux binaires produits. |

## Les informations à ne pas confondre

- **Disponibilité de la passerelle:** connexion MQTT de l'ESP, annonce Online et testament Offline.
- **Joignabilité radio d'un équipement:** présence de réponses, indépendamment de leur interprétation.
- **État connu ou estimé:** observation radio riche, estimation issue d'une commande, estimation contextuelle après STOP, ou inconnu.

Un volet peut être joignable sans fournir sa position intermédiaire. Les réponses simples `00 00` et `01 00` ne suffisent pas, sur toutes les variantes, à prouver une fin de course. Le contexte STOP appartient au `Device`, car le helper `E2bp` est partagé entre tous les volets.

`ShutterFeedback` conserve la provenance des informations. Les messages DETAIL indiquent notamment `state_source`, `state_estimated`, les octets reçus et le résultat du contrôle post-STOP. Une estimation ne devient pas une mesure parce qu'elle est répétée.

Les volets restent commandés en mode optimiste dans Home Assistant. La traduction `stopped -> None` évite qu'un arrêt intermédiaire soit présenté comme une fermeture ou ouverture certaine. Le firmware ne fournit pas de pourcentage mesuré de position intermédiaire.

## Pourquoi ces octets et ces temporisations radio

Les commandes individuelles de volet conservent `B9` pour ouvrir, `FA` pour fermer et `1A` pour arrêter. Le deuxième octet est `06`, et le quatrième reste `00`. Le nouveau mode bus explicite utilise `16` et `20` sur la route d'un volet d'entrée enregistré ; il ne remplace jamais une trame individuelle. Voir [la configuration et les limites du bus](bus-debug-diagnostics.md).

La bascule TX/RX, les écritures directes de registres et les délais dans `E2bp` proviennent des captures historiques. Remplacer une écriture par une méthode de plus haut niveau peut ajouter des lectures SPI, changer les interruptions effacées ou insérer un délai. Une refonte exige des mesures, pas uniquement une compilation.

Une interruption indique qu'il faut examiner le NRF. Seul un contenu réel dans la FIFO permet de déclarer une réponse reçue. Les octets anciens sont purgés avant un nouvel échange; la réutilisation TX est arrêtée à la fin.

L'ISR utilise des appels qualifiés sans dispatch virtuel et un transport dédié
dans la RAM d'instructions. Ce transport conserve la bascule immédiate TX/RX et
les paquets d'appairage de trois puis cinq octets. L'appairage ne réécrit dans
l'ISR que la largeur du prochain paquet. Les transactions SPI principales
masquent les interruptions avant CSN bas et restaurent exactement leur état
après CSN haut ; les délais d'attente radio restent hors de cette protection.
Une broche IRQ encore basse est resservie hors interruption après un front
manqué ou un délai SPI dépassé. Un paquet lu en erreur n'est pas accepté comme
un appairage valide.

## Ordre des travaux dans la boucle

1. Reprendre une IRQ encore basse, puis traiter la console, une configuration HTTP en attente et OTA.
2. Expirer les contrôles STOP devenus trop anciens, sans transaction RF.
3. Consommer un paquet MQTT.
4. Autoriser une étape de découverte/republication seulement si aucune entrée ni vérification STOP prioritaire n'attend.
5. Si les conditions le permettent, effectuer un contrôle post-STOP, sinon une étape de `statusAll`, sinon au plus un polling ordinaire.
6. Servir le scanner et lire une quantité bornée de console.
7. Si aucune commande ou vérification STOP ne l'empêche, servir une étape de
   maintien de variateur, puis rendre la main au cœur ESP8266.

Ce sont des priorités coopératives. Un échange radio ou une reconnexion déjà
engagés ne sont pas interrompus par un nouveau STOP. `pressFor` accepte un
maintien et rend la main après le premier échange ; les impulsions suivantes et
la libération sont traitées une étape à la fois. Une nouvelle commande annule
le maintien et conserve la priorité du STOP et de sa vérification. Les échanges
RF individuels restent synchrones ; cela ne constitue pas une garantie de
latence maximale.

La campagne post-STOP attend au moins 100 ms après la commande, espace les essais suivants de 250 ms, limite les essais à trois et expire après 5000 ms. Ce sont des paramètres logiciels de vérification, pas des garanties de temps réel ni une mesure de course moteur.

## Durée de vie et remplacement de configuration

La liste active possède les `Device`. Les helpers radio empruntent leurs adresses. Les timers ne font que demander un polling; ils ne réalisent aucun échange radio ni aucune publication MQTT.

Un rechargement commence par parser un tableau temporaire complet. Les timers sont détachés et les pointeurs empruntés invalidés avant destruction des anciens objets. Une erreur de parsing conserve la liste active précédente.

Les sauvegardes passent par un fichier temporaire puis un renommage, avec contrôle des tailles écrites et erreurs signalées. Cela protège contre les échecs logiciels détectés. Les tests disponibles ne certifient pas la tenue à une coupure d'alimentation réelle pendant une écriture LittleFS.

LittleFS n'est jamais formaté automatiquement après un échec de montage: une configuration illisible reste une donnée à récupérer.

## MQTT et reprise réseau

Le transport gère son identité stable, le testament de passerelle et les copies des topics nécessaires à la reconnexion. Un succès de `publish()` ou `subscribe()` signifie que l'API a accepté/écrit le paquet localement; il ne prouve pas un acquittement applicatif du broker.

`Mqtt::loop()` peut appeler synchroniquement le callback de commande défini dans `main.cpp`, qui réalise alors l'échange radio. Le transport n'implémente pas cet échange, mais son appel peut en supporter toute la durée. Le rafraîchissement de découverte demande seulement des pollings ultérieurs.

Home Assistant reçoit une découverte et un rejeu progressifs. Le rejeu conserve les dates et la provenance des états, et ne lance pas de transaction RF. Les valeurs retenues sont des instantanés historiques; leur réception ultérieure n'est pas une nouvelle observation radio.

Le Wi-Fi enregistre en flash les identifiants volontairement changés. Une absence de point d'accès, un passage temporaire en AP ou une reconnexion ne doivent pas effacer les paramètres station. Seule la commande explicite `wifiReset` demande leur effacement.

## Vérification et points ouverts

Les tests natifs compilent les implémentations de production avec des interfaces simulées pour la radio, le temps, le stockage et le réseau. Ils vérifient des chemins logiciels, pas les chronogrammes SPI, les réceptions réelles ou tous les chemins IRAM.

Le contrôle du binaire lié suit les appels, sauts indirects résolus et littéraux
de l'ISR ; les pointeurs d'objets à l'exécution et les chronogrammes matériels
restent des hypothèses à vérifier sur carte. La configuration de l'administration
est décrite dans [administration locale](administration-locale.md), et les limites
de l'identification dans [provenance des builds](build-provenance.md). Les moyens de validation sont décrits dans [le guide des tests](../tests/README.md).
