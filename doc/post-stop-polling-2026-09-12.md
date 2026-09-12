# Polling prioritaire après STOP — 2026.09-shutter-poststop-1

Base : `4425fef9e554dc5557b552663a3d25b11eb9e1e1`, version testée par François.
Branche : `fix/shutter-reliability-2026-09-12`. Pas de fusion dans master.

## Objectif et observation de départ

Le patch local mémorisait une PAUSE pendant 5000 ms pour distinguer un arrêt
intermédiaire estimé d'une fin de course estimée. Les traces de VrSalon montrent
`00 00` aussi bien après fermeture complète qu'après fermeture interrompue,
et `01 00` après ouverture interrompue. Le STOP connu reste donc du contexte,
jamais une preuve de position. Cette révision ne change ni les masques RF ni
les commandes individuelles ON/OFF/PAUSE ni leur traduction Home Assistant.

Défaut reproduit dans la base : si la réponse immédiate de PAUSE décrit encore
le mouvement, et si le premier polling périodique arrive six secondes plus tard,
le contexte d'arrêt peut expirer avant la première observation utile. La nouvelle
révision demande une interrogation prioritaire, sans modifier la cadence de fond.

## Politique retenue

Chaque volet possède une seule demande de contrôle, intégrée à son contexte en
RAM. Aucun pointeur vers un ancien Device n'est stocké dans une file persistante.
Une PAUSE nouvelle remplace la demande précédente pour le même volet.

Les valeurs ci-dessous sont des **paramètres d'ordonnancement expérimentaux**,
pas des temporisations constructeur déduites des captures :

| Paramètre | Valeur |
|---|---:|
| Premier contrôle éligible après la fin de la transaction PAUSE | 100 ms |
| Délai minimal entre la fin d'un contrôle et le suivant | 250 ms |
| Nombre maximal de contrôles rapprochés | 3 au total |
| Budget avant abandon, depuis la fin de la transaction PAUSE | 5000 ms |

Ces valeurs sont centralisées dans `include/RF/stopVerification.h`. Elles
n'introduisent pas de delay() supplémentaire ni d'émission dans un Ticker/ISR.
Le premier contrôle se fait à la première occasion après son échéance : 100 ms
n'est pas une garantie de réponse à 100 ms. Un échange RF déjà commencé n'est
pas préempté ; une connexion MQTT défaillante ou une commande bloquante peut
encore retarder la boucle. Aucun nouveau contrôle n'est lancé après le budget ;
un échange commencé avant cette limite peut finir après.

Ordre de service dans la boucle principale :

1. Expiration des demandes devenues trop anciennes (métadonnées seulement).
2. Traitement d'un paquet MQTT ; si des données viennent d'être traitées ou
   attendent encore dans le client TCP, aucun polling automatique à ce passage.
3. Priorité à la saisie console en attente, avant tout polling automatique.
4. Un contrôle post-STOP éligible, en rotation entre volets.
5. Polling périodique uniquement s'il ne reste aucun contrôle post-STOP en attente.

Cela permet `STOP A -> STOP B -> STOP C -> contrôles A/B/C` lorsque les messages
sont déjà présents dans le tampon de réception. Ce n'est pas une nouvelle file
d'exécution des commandes : les commandes restent synchrones et conservent
leur ordre de réception. Nous ne prétendons ni préempter une transaction en
cours ni donner priorité à un STOP qui n'est pas encore arrivé. Un flux entrant
continu peut retarder le polling ; la limite de 5 s empêche de prolonger la
vérification indéfiniment et produit alors un résultat non concluant.

Un polling réussi satisfait aussi l'ancienne demande périodique du volet.
Pendant l'attente de 100/250 ms, les autres services de boucle continuent ; un
polling ordinaire n'est pas lancé pour remplir cette attente. Après les contrôles,
les demandes périodiques des autres équipements restent disponibles.

## Interprétation des résultats

- Arrêt simple après PAUSE avec réponse à la commande : arrêt **estimé** mémorisé,
  `stop_check=stopped_observed`. La position intermédiaire n'est pas prouvée.
- Retour riche de fin de course : conservation du décodage existant,
  `stop_check=endpoint_observed`. Pas de nouvelle validation constructeur.
- Réponse indiquant encore le mouvement : publier le mouvement observé et
  programmer un nouveau contrôle, dans les limites ci-dessus.
- Absence de réponse ou réponse indécodable : état inconnu et nouvelle tentative.
- Trois contrôles non concluants ou budget expiré : `stop_check=inconclusive`.
  Aucun passage automatique à une fin de course supposée par simple expiration.
  Un mouvement réellement reçu au dernier contrôle reste visible ; un arrêt
  simple ultérieur reste inconnu jusqu'à un nouveau contexte exploitable.
- PAUSE sans réponse à la commande : un contrôle est quand même programmé, mais
  une réponse simple ne transforme pas la commande en commande acquittée et
  ne suffit pas à reconstruire une position. Les retours riches restent utilisables.

Les échecs des contrôles rapides ne sont pas comptés comme trois échecs de
polling périodique : ils ne déclarent pas le volet Offline en moins d'une seconde.
Les succès rétablissent normalement sa joignabilité ; les échecs périodiques
ultérieurs continuent à assurer le diagnostic d'indisponibilité.

Une nouvelle commande ON/OFF/TOGGLE annule le contrôle de l'ancien STOP, même si
l'initialisation radio échoue. Un appui manuel `press` est aussi identifié comme
commande, pas comme réponse au polling de l'ancien STOP. Rechargement/configuration
et redémarrage reconstruisent les objets sans anciennes demandes de contrôle.

`poll` désactivé suspend aussi les contrôles automatiques post-STOP. Les modes
scanner/appairage/copie ne sont pas interrompus par ces contrôles. La demande
peut expirer pendant cette suspension : elle ne produit alors pas de faux état
fermé/ouvert. Le service automatique est ajouté à la cible ESP8266 avec MQTT ;
la cible Mega compile mais ne reçoit pas un nouvel ordonnanceur autonome.

## Traces et attributs

Exemple de format (illustration, pas une capture de l'installation) :

```text
Post-STOP VrSalon attempt=1/3 age_ms=101
Received: VrSalon ... closing ...
Post-STOP VrSalon result=pending response=yes state=closing
Post-STOP VrSalon attempt=2/3 age_ms=360
Received: VrSalon ... stopped ...
Post-STOP VrSalon result=stopped_observed response=yes state=stopped
```

Le topic existant `<nom>/tele/DETAIL` reçoit deux champs additionnels :
`stop_check` et `stop_check_attempts`. Les autres champs sont conservés.
Le buffer JSON est agrandi et testé. Le topic POWER, les commandes, le mode
optimiste des volets et le template stopped -> None restent inchangés.

## Validation et limites

La base passe 219 contrôles natifs. Les premiers tests supplémentaires exécutés
avant correction ont donné 8 échecs (229 contrôles au total). Des scénarios
complémentaires ont ensuite révélé un contrôle non annulé après échec de toggle,
un appui console confondu avec du polling et un polling ordinaire intercalé pendant
l'attente prioritaire ; ces défauts ont été reproduits puis corrigés.

La suite finale passe **304 contrôles natifs** avec AddressSanitizer,
UndefinedBehaviorSanitizer et détection des fuites. `tests/check_mqtt_json.py`
vérifie les JSON produits et les six traductions Jinja existantes. Les substituts
matériels/réseau sont confinés à tests/host. Le substitut MQTT délivre maintenant
un paquet par loop(), selon le comportement de PubSubClient 2.8 ; il ne remplace
pas un essai avec un broker réel, fragmentation TCP et Wi-Fi chargés.

GitHub Actions compile d1_mini, d1_mini_ota et megaatmega2560, sans upload.
Consulter le résultat du run associé au commit livré. Cette note ne vaut pas
validation électrique, chronométrique ni RF sur l'installation.

Aucun changement de puissance radio, séquence TX/RX, pilote RF24, authentification,
format des fichiers RF/MQTT ou découpage de flash. LittleFS sans autoformat reste
activé. Les reprises Wi-Fi/LWT et le problème bas niveau SPI restent hors périmètre.

## Mise à jour et essai

Sauvegarder dConfigFS/mqttDiag en privé et conserver le firmware précédent.
Récupérer cette branche sans écraser les changements locaux non enregistrés.
Compiler `pio run -e d1_mini_ota`, puis firmware seul vers l'IP vérifiée :
`pio run -e d1_mini_ota -t upload --upload-port 192.168.0.112`.
Ne pas exécuter uploadfs, erase ni format.

Après démarrage, help doit afficher `2026.09-shutter-poststop-1`. Tester un arrêt
intermédiaire de VrSalon puis la reprise, et quelques STOP de volets distincts.
Comparer chronologie des commandes, traces post-STOP, états et mouvement réel,
en respectant le service intermittent des moteurs. Un résultat stopped_observed
n'est pas une preuve de fin de course et ne doit pas servir d'interverrouillage.
