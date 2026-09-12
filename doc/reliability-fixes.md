# Corrections de fiabilité — branche du 12 septembre 2026

Base : `lecocotier/yokis-hack`, commit `a271a54ea50e20db381e7ff187702bc7b6ff0c55`.
Cette branche ne fusionne pas le master de nmaupu. Elle corrige les défauts logiciels
identifiés dans la revue, en conservant l'interface de l'installation existante.

## Invariants conservés

- Volet individuel : ouverture `B9`, fermeture `FA`, arrêt `1A`, deuxième octet
  `06`, quatrième octet `00`. Aucun passage automatique en commande de bus `16`.
- Commandes MQTT : `<nom>/cmnd/POWER`, valeurs `ON`, `OFF`, `PAUSE`.
- Retour MQTT : `<nom>/tele/STATE`, objet JSON portant la clé `POWER`.
- Découverte Home Assistant sous `homeassistant/cover/<nom>/config` et identifiants
  existants. Les éclairages restent déclarés comme auparavant.
- Conservation du format des fichiers `/yokis.conf` et `/mqtt.conf`.
- Ni authentification réseau ajoutée, ni changement de puissance RF, ni délai de
  garde arbitraire de 60 ms, ni remplacement de la séquence TX/RX historique.

## Corrections

### Commandes, résultats et état du volet

Le polling n'alimente plus le filtre de doublons des commandes. Toutes les
commandes directes de volet, même identiques et rapprochées, sont admises.
L'arrêt n'est jamais filtré. Pour les autres modes, seul un doublon de la dernière
commande ayant obtenu un résultat positif entre dans la fenêtre de 100 ms.
La date de dernière observation de l'état reste indépendante.

Topics MQTT, périphériques, type de commande, longueur et valeur du payload sont
vérifiés avant toute émission. Une erreur de parsing ou un périphérique inconnu
n'accède pas à la radio. Les buffers dynamiques temporaires du callback ont été
remplacés par un parseur borné.

Une absence de réponse n'est plus confondue avec une réponse reçue dont l'état
n'est pas compris : le second cas maintient la joignabilité et signale l'état
inconnu. Une commande sans retour ne fabrique pas un état « ouvert » ou « fermé ».
L'absence de réponse ne prouve toutefois pas l'absence d'exécution physique.
Les journaux indiquent la commande, le nombre de cycles et la présence d'un retour.

Les deux réponses simples `00 00` et `01 00` ne prouvent pas à elles seules une
fin de course sur toutes les variantes. La révision `2026.09-shutter-context-1`
réintègre l'estimation contextuelle du patch local : STOP récent, arrêt mémorisé,
fin de course supposée sinon. L'estimation est distinguée du retour radio riche.
Voir [la note de réintégration et ses limites](shutter-context-2026-09-12.md).
Les masques historiques plus riches sont conservés ; aucun masquage arbitraire
des bits `40/80` n'est appliqué.

Le défaut d'affectation de `secondPayloadStatus` dans la branche non-volet est
corrigé. Le changement d'état répété dans `dimmerMem()` est supprimé. La consigne
maximale du variateur utilise réellement la séquence de luminosité maximale.
Le toggle volet ne répète plus une séquence appui/relâchement susceptible de
l'arrêter : le relâchement `53` est une opération distincte de l'appui `35` dans
les observations historiques de cette installation.

### Radio et durée de vie des objets

`E2bp` emprunte un `Device` : il ne l'alloue ni ne le détruit. Les périphériques
non configurés et les échecs d'initialisation du NRF sont rejetés. Les données et
compteurs sont initialisés. Les délais emploient une différence non signée sur
32 bits, y compris lors du débordement de `millis()`.

Les traces série/Telnet ont été retirées des callbacks d'interruption.
Le correctif de protection des transactions SPI n'a pas pu être publié :
l'outil de publication a bloqué cette écriture. Le pilote `RF24_forked.cpp`
reste donc celui de la base et cette protection reste un point ouvert.
Le passage TX vers RX dans l'ISR est conservé pour ne pas reconstruire sans
mesure une séquence temporelle spécifique. Une vérification des chronogrammes et
des chemins IRAM sur matériel reste nécessaire : les tests sur PC ne la remplacent pas.

La boucle vérifie aussi la FIFO en cas de front IRQ manqué ; elle exige un payload
réel pour annoncer une réponse. Les données RX anciennes sont purgées au début
de l'envoi et la réutilisation TX est arrêtée à la fin. Les tailles logicielles
TX neuf octets / RX deux octets sont distinguées.

L'appairage limite les deux réceptions à la taille du buffer et publie son compteur
après écriture. Le type n'est plus deviné depuis un octet d'état ambigu : utiliser
`save <nom> SHUTTER` pour un volet. Le scanner reporte l'affichage hors ISR.
La copie vérifie les retours d'émission et ne laisse pas l'ISR effacer les bits
attendus par l'émission bloquante.

### MQTT, configuration et console

La découverte n'est déclarée terminée qu'après publication et abonnement réussis
pour tous les équipements. Les abonnements sont uniques, libérés sans double-free,
et leur capacité compte les topics : deux par variateur, jusqu'à 64 équipements.
Une reconnexion redemande la découverte. Un rechargement retire les anciennes
entités supprimées ou retypées lorsque le broker est connecté.

Un seul polling est traité par passage dans la boucle, en rotation, au lieu de
bloquer le traitement MQTT pendant la visite de tous les équipements indisponibles.
Les équipements `NO_RCPT` ne sont pas interrogés inutilement. Le compteur d'échecs
est initialisé et saturé plutôt que de reboucler après 255.

Les changements de configuration HTTP sont validés intégralement puis appliqués
depuis la boucle principale, pas depuis le callback AsyncTCP. Les champs omis
conservent leur valeur. Les ports sont numériques et compris entre 1 et 65535.
Une réponse HTTP 202 signifie « mise en attente », pas « sauvegarde déjà réussie ».
Le résultat d'application est indiqué sur la console. Le formulaire n'ajoute pas
d'authentification dans cette branche. Le formatage automatique au montage
LittleFS est maintenant désactivé pour protéger une configuration illisible.

Les chaînes MQTT sont toujours terminées et les valeurs trop longues sont
refusées sans écraser l'ancienne valeur : hôte 63 octets maximum, utilisateur et
mot de passe 31 octets chacun. Les noms d'équipement sont limités à 48 octets,
sans espace, délimiteur de configuration, caractère JSON spécial ou séparateur /
joker MQTT. Ces restrictions doivent être vérifiées avant de migrer un ancien
fichier contenant des noms inhabituellement longs.

Les écritures de configuration passent par un fichier temporaire puis un
renommage. Une erreur détectée d'écriture ou de renommage conserve l'ancien fichier.
Le parsing d'un fichier incomplet, invalide, dupliqué ou trop grand échoue sans
remplacer la configuration active. Il n'y a pas de test de coupure électrique
réelle de LittleFS dans la validation native.

Lors d'un rechargement, les timers sont détachés avant destruction de leurs
arguments, les pointeurs empruntés sont invalidés et les abonnements reconstruits.
La suppression de la configuration MQTT ne recrée plus immédiatement le fichier.

La console accepte LF et CRLF sans double exécution. Un dépassement de longueur
rejette la ligne entière, sans exécuter sa fin comme une seconde commande. Les
arguments manquants, durées invalides et erreurs de commande sont traités. Les
arguments WiFi/MQTT contenant des espaces peuvent être entre guillemets.

## Validation

`bash tests/run_native.sh` compile avec g++ les fonctions de production et des
substituts de leurs dépendances matérielles, puis active AddressSanitizer,
UndefinedBehaviorSanitizer et la détection des fuites. Il ne s'agit pas d'un test
radio. Les fonctions `loop`, `pollForStatus` et `mqttCallback` sont extraites
verbatim depuis `src/main.cpp`, sans copie réécrite de leur logique.

Les premiers tests ont reproduit quatre défauts dans la base : filtre MQTT lié
au polling, port MQTT indéterminé, terminaison de chaîne et accumulation des
abonnements. Les tests étendus vérifient aussi les erreurs de fichiers, les
requêtes HTTP partielles, les réponses RF simulées, les limites de la console,
les rechargements, les pointeurs empruntés et le débordement temporel.

La CI compile également les cibles `d1_mini` et `megaatmega2560`, sans aucune
commande de téléversement. Son résultat et les artefacts sont attachés au run
GitHub Actions correspondant. Un binaire compilé n'est pas un firmware validé
sur l'installation. Le changement de chaîne ESP8266 doit être testé sur un
appareil disponible avant tout remplacement généralisé.

## Essai sur l'installation

Sauvegarder `dConfigFS` avant mise à jour et conserver le firmware précédent.
Commencer par un seul volet en gardant un accès série et la commande locale.
Vérifier ouverture, fermeture et arrêt, puis les commandes autour du polling.
Comparer action physique, ordre MQTT et journal RF. Respecter le service
intermittent du moteur ; ne pas imposer des dizaines de cycles rapprochés.

Les hypothèses de portée, alimentation NRF, délai de garde, signification de
l'octet aléatoire et exactitude des chronogrammes restent des sujets de mesure.
Cette branche corrige des défauts logiciels démontrés ; elle ne promet pas que
chaque raté matériel éventuel a la même cause.
