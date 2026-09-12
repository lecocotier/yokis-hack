# Réintégration du retour d'état local des volets

Révision : `2026.09-shutter-context-1`. Base : `760e5ca` de la branche
`fix/shutter-reliability-2026-09-12`. Aucun changement du master.

## Origine et observations

Cette correction reprend le patch local fourni par François le 12 septembre
2026, notamment `LastShutterCmdSent`, `LastCmdTime`, la condition de PAUSE
récente et le maintien de l'état arrêté. Le patch exécutait **5000 ms** malgré
son commentaire « 500 ms » : la valeur réellement exécutée est conservée et
nommée `StopWindowMs`. Aucun temps de parcours moteur n'est inventé.

Les observations physiques et traces transmises le même jour établissent :

| VrSalon | Mouvement | Arrêt final |
|---|---|---|
| Fermeture interrompue par PAUSE | `00 01` | `00 00` |
| Fermeture complète | `00 01` répété | `00 00` |
| Ouverture interrompue par PAUSE | `01 01` | `01 00` |

Une ouverture complète n'a pas été fournie sous forme de trace brute distincte.
Ces captures n'établissent pas que le protocole est incapable de fournir une
position par une autre méthode ; elles établissent l'ambiguïté des réponses
actuellement exploitées. Les autres modules montrent des réponses plus riches
(`10 02`, `18 02`, `04 02`...), dont les masques existants sont conservés.

## Comportement réintégré et corrections du patch ancien

L'historique est initialisé et appartient à **chaque Device**, jamais à la radio
partagée. Il ne modifie ni `/yokis.conf` ni `/mqtt.conf` et n'est pas restauré
après redémarrage/rechargement : un vieil arrêt ne doit pas devenir persistant.

- Une réponse simple d'arrêt dans les 5 secondes suivant une PAUSE ayant obtenu
  une réponse RF conduit à « arrêté estimé ». Cette fenêtre est calculée par
  soustraction non signée, y compris autour du débordement de `millis()`.
- Une fois cet arrêt observé, il reste mémorisé après les 5 secondes. Cela
  conserve l'intention de l'ancien `status == SHUTTER_STOPPED`.
- Une nouvelle commande UP/DOWN/TOGGLE, ou un mouvement réellement observé,
  invalide l'arrêt ancien. Il ne reste donc pas bloqué à cause d'un polling
  ayant manqué toute une course. Un retour riche de fin de course a priorité.
- Sans contexte STOP, `00 00` est une fermeture **estimée** et `01 00` une
  ouverture **estimée**, comme dans le patch local. Au premier polling après
  redémarrage, cette estimation ne prouve pas davantage une fin de course.
- Une commande sans réponse est notée non confirmée. Les réponses simples qui
  suivent ne fabriquent pas une position certaine ; un mouvement observé, un
  retour riche ou une nouvelle commande avec réponse peut rétablir le contexte.
- Les réponses aux commandes pouvant décrire l'état AVANT exécution, elles sont
  conservées comme traces brutes mais pas décodées comme une nouvelle fin de
  course. Après une commande avec réponse, le mouvement/STOP attendu reste
  explicitement une estimation jusqu'aux observations suivantes.

**Limites :** un STOP en fin de course peut être pris pour un arrêt intermédiaire,
et un arrêt par bouton mural ou autre télécommande peut échapper à l'historique.
Un polling périodique peut manquer un mouvement entier. L'estimation ne convient
pas comme preuve de fermeture ni comme interverrouillage. `command_response`
signifie qu'une réponse a été reçue, pas que l'exécution mécanique est prouvée.

## Home Assistant : conserver les commandes sans faux « fermé » après STOP

`optimistic: true` est rétabli pour les seuls volets, avec les identifiants et
commandes existants : `<nom>/cmnd/POWER`, `ON`/`OFF`/`PAUSE`.
Le message `<nom>/tele/STATE` conserve la clé `POWER` et la valeur `stopped`,
pour les consommateurs existants. La découverte utilise maintenant :

```jinja2
{{ 'None' if value_json.POWER == 'stopped' else value_json.POWER }}
```

Ainsi, l'entité cover native présente **Inconnu** après un arrêt sans position
certaine, et non « fermé » uniquement parce qu'elle descendait. Ce choix est
volontaire : MQTT Cover ne fournit pas d'état natif « intermédiaire » distinct.
Il ne s'agit pas de `unavailable`, la disponibilité RF reste séparée.

Le nouveau topic `<nom>/tele/DETAIL`, également déclaré comme source d'attributs
JSON de l'entité, précise : `yokis_state`, `state_source`, `state_estimated`,
`last_command`, `last_command_ms`, `command_response`, `raw_response`, `raw_origin`.
Pour un arrêt détecté après PAUSE : `yokis_state=stopped`,
`state_source=command_stop_estimate`, `state_estimated=true`.
`simple_rf_estimate` identifie les fins de course supposées ; `rf_status`
identifie l'application du décodeur historique aux réponses plus riches, sans
certifier à nouveau le protocole constructeur. Aucun pourcentage n'est inventé.

Référence externe pour la conversion native de stopped et la valeur None :
https://www.home-assistant.io/integrations/cover.mqtt/

Les traces RF affichent maintenant l'action (UP/DOWN/PAUSE), son horodatage,
l'âge, l'état précédent et la source. L'ancien intitulé Last cmd qui affichait
un horodatage plutôt qu'une commande n'est pas repris tel quel.

## Commandes groupées et conservation des paramètres

Aucune hypothèse de panne radio n'est présentée comme corrigée. La passerelle
conserve des transactions successives et le traitement d'un seul polling par
itération. Le correctif de suppression du filtre de 100 ms pour les volets reste
actif. Les tests envoient des commandes rapprochées à des destinations distinctes
et vérifient que l'état STOP d'un volet ne contamine pas un autre, et qu'un
échec de réponse n'empêche pas de traiter la commande du volet suivant.
Ce n'est ni un test de rafale réelle du broker, ni une mesure radio sur place.

Le réglage LittleFS `setAutoFormat(false)` précédemment proposé dans le chat est
intégré. Les configurations existantes et leur découpage flash ne sont pas
modifiés. Ne pas utiliser uploadfs ni erase pour une mise à jour firmware seule.
Les réglages radio, payloads, temporisations, authentification et le pilote
RF24 bas niveau restent inchangés dans cette révision.

## Validation reproductible

- `bash tests/run_native.sh` : 219 contrôles sur les fonctions de production,
  radio/réseau/fichiers simulés, AddressSanitizer, UndefinedBehaviorSanitizer et
  détection des fuites. Les ajouts échouaient avant correction (19 échecs lors
  de la première campagne étendue).
- `python3 tests/check_mqtt_json.py` : JSON produit par le firmware testé et
  six traductions du template Jinja ; nécessite Jinja2==3.1.6.
- GitHub Actions compile les cibles ESP8266 d1_mini et Arduino Mega. Vérifier
  le résultat du run associé au commit retenu. Aucun téléversement matériel.

Essai terrain : sauvegarder dConfigFS/mqttDiag et l'ancien firmware ; tester sur
un volet une course complète, une PAUSE dans chaque sens, puis une reprise dans
le même sens. Contrôler l'attribut `state_source`, pas uniquement l'icône cover.
Vérifier ensuite les commandes groupées usuelles sans cyclage intensif du moteur.
Les modifications locales de optimistic et LittleFS sont désormais intégrées :
ne pas réappliquer aveuglément un ancien stash après récupération de la branche.
