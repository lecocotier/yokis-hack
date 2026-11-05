# Bus, traces et diagnostic

## Traces et diagnostic

`debug 0` supprime les trames et résultats périodiques. `debug 1` les affiche.
`debug 2` ajoute le debug détaillé antérieur. `debug` parcourt les trois niveaux.
Le démarrage reste au niveau 0. `raw` choisit un format de maintenance sans
activer les traces de polling. Les transitions Offline et erreurs explicites
restent visibles ; une panne déjà signalée ne réimprime pas Offline à chaque poll.

La racine HTTP affiche le diagnostic et `/config` conserve le formulaire
POST/CSRF. `/api/status?page=0` fournit huit équipements par page, jusqu'à 64.
Les deux pages et l'API demandent la clé HTTP existante. Elles ne déclenchent
aucun échange RF et ne renvoient aucune clé. Le navigateur actualise les données
toutes les cinq secondes. Un snapshot dépassant le buffer borné retourne 503,
jamais un JSON partiel. `dConfig` affiche toujours la mémoire ; `statusAll`
demande des mesures progressives, y compris lorsque le polling périodique est OFF.
Une commande reçue ou une vérification STOP passe avant ce rafraîchissement.

La LED produit deux flashes courts sans WiFi, un clignotement lent lorsque MQTT
est configuré mais déconnecté, et reste éteinte lorsque la connexion est saine
ou que MQTT n'est pas configuré. Aucun `delay` supplémentaire n'est introduit.

## Configuration et limites du bus

La [notice Yokis MVR500MRP](https://www.yokis.fr/notices/NOTICE-MVR500MRP-040319.pdf)
décrit le relais des commandes de groupe par un récepteur appairé. Il faut
choisir un volet individuel déjà enregistré comme point d'entrée :

```text
busConfig tous volet_entree
```

Cette commande sauvegarde la route adresse/canal et recharge la configuration,
sans émettre de trame. Elle refuse d'écraser un équipement individuel. Le groupe
représente tous les équipements `SHUTTER` configurés ; les variateurs et autres
groupes sont exclus. Cette liste logicielle suppose que le bus physique a été
appairé pour ces mêmes volets. La route est revérifiée à chaque commande : un
point d'entrée supprimé, retypé ou dont la route a changé empêche l'envoi.

Les commandes du cover Home Assistant restent `ON`, `OFF`, `PAUSE`, comme pour
les volets individuels. La console accepte `on tous`, `off tous`, `pause tous`.
Le groupe ne reçoit aucun polling radio automatique : la réponse d'un seul
récepteur ne peut pas prouver l'état de l'ensemble.

Le NRF est configuré avec `EN_AA=0`. `TX_DS` confirme seulement une émission,
pas un acquittement du récepteur ou des moteurs. Un ordre bus est donc émis
une seule fois et crée un contexte **estimé** distinct pour chaque membre,
sans pourcentage inventé ni `command_response=true`. Un échec laisse l'état
inconnu. Les publications des membres sont étalées par le replay MQTT.

Après fermeture bus puis STOP individuel, seul le contexte du volet ciblé est
remplacé. Un poll du point d'entrée ne réécrit pas les autres volets. Un groupe
dont les états divergent devient inconnu. Après STOP, les réponses anciennes
ambiguës ne deviennent pas une certitude de fermeture. Une nouvelle direction
bus remplace ensuite les contextes des membres concernés.

Les tests natifs vérifient ces scénarios sur banc logiciel. Ils ne valident
pas les mouvements réels ni la portée radio sur une installation.
