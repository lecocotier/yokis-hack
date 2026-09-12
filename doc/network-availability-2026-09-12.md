# Fiabilité réseau et disponibilité — 2026.09-network-1

Base : `091721ae18898a78a4aa063814840722e269fd2f` (post-STOP, essai matériel
satisfaisant signalé par François). Cette base a été intégrée par avance rapide
dans `lecocotier/yokis-hack:master` et conservée dans
`reference/shutter-poststop-1`. Les changements décrits ici restent sur
`fix/network-availability-2026-09-12` pour les essais réseau sur installation.

## Périmètre

Aucune fusion générale de `nmaupu/master`. Les fichiers `src/RF/*`,
`include/RF/*`, l'ordonnanceur post-STOP et la persistance RF/MQTT ne sont pas
modifiés. Les payloads, temporisations, puissance, authentification, noms des
volets, identifiants Home Assistant et découpage de flash restent identiques.
Le mode optimiste des volets, la traduction `stopped -> None`, le contexte
propre à chaque volet et LittleFS sans autoformat sont conservés.

## Corrections

### Disponibilité radio et passerelle

`publishDevice()` ne remplace plus un volet déjà Offline par Online après une
simple publication de découverte : il republie son indicateur de disponibilité
connu, sans appeler une fonction qui change l'état radio. L'initialisation et
les critères de joignabilité radio sont ceux de la base ; il n'y a pas de
nouvelle preuve de réception au démarrage ni pour les équipements NO_RCPT.

Chaque ESP possède un identifiant client stable `YokisHack-<chip_id_hex>` et un
nouveau topic `yokis/YokisHack-<chip_id_hex>/availability`. La connexion enregistre
un testament MQTT `Offline`, QoS 1, conservé par le broker (retain). Le programme
publie `Online` conservé après connexion et réessaie une écriture échouée.
Une extinction/perte TCP doit être détectée par le broker avant publication du
testament : l'indisponibilité n'est pas instantanée. Le keepalive reste celui de
PubSubClient 2.8 ; aucune mesure réelle de ce délai n'est revendiquée.

Les déconnexions volontaires (nouvelle configuration MQTT/Wi-Fi, restart, OTA)
publient Offline avant MQTT DISCONNECT. Si cette publication échoue localement,
la socket est fermée sans DISCONNECT pour laisser agir le testament. Une écriture
réussie de PubSubClient ne constitue pas un acquittement applicatif du broker.
Une ACL de broker refusant le nouveau topic doit être adaptée par l'administrateur.

La découverte des entités utilise deux disponibilités avec `availability_mode=all` :
la passerelle ET le topic radio existant `<nom>/tele/LWT`. Une position inconnue
ne modifie pas ces disponibilités. Les commandes restent permises en mode
optimiste tant que l'entité n'est pas indisponible.

### Reconnexion et démarrage de Home Assistant

Le message exact `online` sur `homeassistant/status` déclenche un rafraîchissement
différé. Le topic et le payload sont ajustables à la compilation par
`HASS_BIRTH_TOPIC` et `HASS_BIRTH_PAYLOAD` pour une installation personnalisée.
Un autre payload sur ce topic est consommé, jamais transmis au parseur RF.
Les demandes répétées pendant un cycle sont regroupées sans le redémarrer.

La découverte traite au plus un équipement par étape, espacée de 100 ms minimum,
puis rejoue les états/attributs en mémoire progressivement. Un échec local de
publication/abonnement laisse le travail à réessayer. Cette tâche ne fait jamais
de transaction radio ; elle demande des pollings ordinaires pour les observations
suivantes. Commandes déjà reçues, saisie console et contrôles post-STOP passent
avant ce travail. Un échange ou une connexion en cours reste synchrone.

Les états/attributs/valeurs de luminosité sont désormais publiés avec retain afin
que Home Assistant reçoive aussi le dernier instantané s'il s'abonne après la
publication. Ce sont des valeurs historiques, pas une preuve d'observation récente.
La disponibilité de passerelle rend ces valeurs indisponibles lorsque le broker
constate sa disparition. Les topics de commandes ne deviennent PAS retenus.

Pour les volets, DETAIL ajoute `state_replayed` (republication effectuée par le
firmware) et `status_age_ms` (âge de la dernière mise à jour de l'état au moment
de cette publication, calcul non signé sur 32 bits). Ces champs ne sont pas une
horloge absolue et ne détectent pas un rejeu ultérieur du broker lui-même. La
republication ne modifie ni l'état, ni son horodatage, ni la provenance RF.
Les champs antérieurs, dont `stop_check`, restent présents.

Suppression/retypage d'une entité pendant une connexion active : ses messages
retenus STATE/DETAIL/BRIGHTNESS/LWT sont effacés avec sa découverte. Comme dans la
base, un effacement fait hors connexion n'est pas synchronisé par une file durable
de suppressions : le nettoyage du broker peut alors demander une action manuelle.

Les JSON de découverte utilisent les abréviations officielles pour rester dans
les 1024 octets MQTT avec les noms de 48 octets. Les troncatures sont détectées
avant envoi. Les valeurs des unique_id et device identifiers ne changent pas.

### Wi-Fi : reprise sélective de l'amont

Références : `nmaupu/yokis-hack`, état `4a5bf0a`, notamment l'intention de
`f99e422` et `c2aeb56` (ordre de persistance et reconnexion avec mêmes identifiants).
La nouvelle version est adaptée à notre ESP8266, pas copiée avec les branches ESP32.

Même SSID/mot de passe + station déconnectée ou radio éteinte : une reconnexion
est réellement demandée. La persistance n'est activée que pour enregistrer des
identifiants volontairement changés, puis désactivée. La reconnexion n'efface
pas les paramètres enregistrés, et l'absence temporaire du point d'accès n'active
pas une remise à zéro. Le mode AP conserve aussi les paramètres station ; seul
le `wifiReset` explicite les efface. Aucun `ESP.eraseConfig()` général n'est ajouté.

`wifiReconnect` est ajouté à la console. `mqttDiag` indique l'identifiant client,
le topic de disponibilité passerelle et le rafraîchissement restant ; il affiche
également les identifiants MQTT comme auparavant et ne doit pas être partagé
sans masquer le mot de passe.

MQTT ne tente pas de connexion au broker lorsque le Wi-Fi est déconnecté. Son
attente de réponse MQTT est ramenée à 2 secondes ; cela ne borne pas à deux
secondes l'ensemble DNS/TCP/MQTT. La procédure Wi-Fi explicite peut attendre
jusqu'à environ 5 secondes comme auparavant ; les retries automatiques Wi-Fi
restent gérés par le coeur ESP8266, sans nouvel ordonnanceur de reconnexion.

## Validation

La base a repassé ses 304 contrôles et les six traductions Jinja avant promotion.
Les premiers tests ajoutés ont reproduit 21 échecs (350 contrôles) avant les
changements de production. La version présente passe 411 contrôles natifs,
AddressSanitizer, UndefinedBehaviorSanitizer et détection des fuites.
Les tests compilent maintenant le vrai `src/net/wifi.cpp`, plutôt que des fonctions
vides, avec une interface Wi-Fi simulant les effets persistants.
Le mode hôte utilise -O0 par défaut (ajustable par HOST_OPTIMIZATION), sans changer
le mode release des compilations ESP8266/Mega.

`tests/check_mqtt_json.py` vérifie les six traductions Jinja et cinq JSON de
découverte, dont les quatre modes avec des noms maximaux. Les tests couvrent
Offline en redécouverte, identité stable par ESP, configuration du testament,
déconnexion volontaire, échec d'annonce Online, payloads de démarrage invalides,
regroupement des événements, priorité des commandes/STOP, erreurs et rollover.

Le réseau, la radio et les fichiers des tests natifs sont simulés : ce n'est pas
un essai avec un broker, un point d'accès ou Home Assistant réels. Les compilations
PlatformIO des trois cibles sont contrôlées par GitHub Actions ; le résultat du
run correspondant au commit livré fait foi. Aucun téléversement automatique.

## Essai sur installation et retour arrière

Conserver le binaire/source de 091721a et les sauvegardes privées dConfigFS/mqttDiag.
Compiler puis téléverser le firmware seul vers l'IP confirmée, sans uploadfs,
effacement ni format. `help` doit annoncer `2026.09-network-1`.

Vérifier d'abord les commandes individuelles, un STOP intermédiaire et les
commandes de plusieurs volets. Ensuite : redémarrer Home Assistant sans redémarrer
l'ESP ; vérifier retour des états et attributs. Couper momentanément la passerelle :
son topic doit passer Offline après détection broker, puis Online à sa reconnexion.
Tester séparément une coupure du Wi-Fi et du broker, sans modifier les identifiants.
Un volet réellement hors ligne ne doit pas redevenir Online par redécouverte seule.

Un broker à ACL restrictive doit autoriser le nouveau topic de passerelle et
l'abonnement au topic de démarrage Home Assistant. En revenant au firmware 091721a,
la découverte antérieure remplacera la double disponibilité et les états reprendront
leurs anciennes publications. Les caches retenus ajoutés sur le broker peuvent être
nettoyés séparément ; ils ne font pas partie de la flash du module.

## Sources externes

- https://www.home-assistant.io/integrations/cover.mqtt/ (availability/all, None)
- https://www.home-assistant.io/integrations/mqtt/ (birth, discovery/state replay)
- https://pubsubclient.knolleary.net/api (API 2.8 : will, disconnect, timeout)
- https://arduino-esp8266.readthedocs.io/en/3.1.2/esp8266wifi/generic-class.html
- https://github.com/esp8266/Arduino/blob/3.1.2/libraries/ESP8266WiFi/src/ESP8266WiFiSTA.cpp
- https://github.com/nmaupu/yokis-hack/blob/4a5bf0a1de7906b6ea511e1cd88dbaa65fe16378/src/net/wifi.cpp
