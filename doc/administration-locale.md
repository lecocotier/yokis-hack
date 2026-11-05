# Administration locale

`python tools/provision_local.py` crée quatre clés indépendantes dans
`include/local_secrets.h` et configure l'authentification OTA dans
`platformio.local.ini`. Ces deux fichiers sont ignorés par Git. Le script conserve
les clés existantes ; les consulter directement dans l'éditeur, sans les copier
dans un ticket ou une commande enregistrée. Les identifiants WiFi/MQTT déjà
enregistrés sur la carte ne sont pas remplacés par cette opération.

L'outil OTA du projet est activé. Les options d'authentification résident dans
`[ota_private:d1_mini_ota]`, séparées de `[env:d1_mini_ota]` qui conserve la
destination. Le script de préparation migre les anciens flags sans remplacer
la clé du firmware installé par une éventuelle nouvelle clé du header. Il
refuse deux jeux de flags contradictoires afin de préserver leur contenu.
Un port d'écoute 0 choisit un port libre ; un port explicite reste respecté.
La compilation configure l'outil sans lancer de téléversement.

La page HTTP demande l'utilisateur `admin` et la clé HTTP. Toute modification
utilise POST et le jeton du formulaire. Un champ mot de passe vide conserve
l'ancienne valeur ; la case d'effacement est nécessaire pour la supprimer.
La console TCP demande uniquement sa clé, avant de donner accès aux commandes
ou aux journaux. USB reste utilisable. L'OTA et le point d'accès ont chacun leur
propre clé. Sans clé valable, le service d'administration concerné reste fermé.
Les compilations publiques de CI n'incluent pas les clés de cette installation.

HTTP et la console TCP ne chiffrent pas les échanges. Réserver ces services au
réseau local de confiance ou à un tunnel ; ne pas les exposer directement sur
Internet. L'authentification OTA n'est pas une signature du firmware.

Avant publication : `python tools/check_private_config.py --staged` contrôle
l'index et les valeurs privées connues, sans les afficher. Ce contrôle protège
les prochains commits ; il n'efface pas les anciennes adresses déjà présentes
dans l'historique Git.
