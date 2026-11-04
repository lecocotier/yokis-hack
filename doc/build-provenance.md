# Retrouver les sources d'un firmware

PlatformIO génère `include/build_provenance.h` avant la compilation. La commande
console `version` affiche le commit Git, l'identifiant de build et l'empreinte
SHA-256 des sources publiques. `help` affiche aussi l'identifiant court.

Chaque environnement produit `.pio/build/<environnement>/build-receipt.json`
avec le commit complet, l'état modifié ou propre, l'empreinte des sources et
celles des ELF/BIN/HEX effectivement générés. Le reçu après édition de liens
référence seulement l'ELF ; il n'attribue jamais un ancien BIN à ce nouvel ELF.
Une modification des sources pendant le build fait refuser le reçu.

L'empreinte des sources couvre les noms et octets exacts des fichiers publics
de `src`, `include`, `lib`, `platformio.ini` et des scripts de build. Les fins de
ligne comptent : des checkouts Windows et Linux différents peuvent avoir des
empreintes distinctes au même commit. Les clés, fichiers privés/locaux,
en-têtes générés, caches et dépendances installées sont exclus. Les versions
disponibles des outils et bibliothèques sont enregistrées séparément.

Une clé locale différente peut donc conserver l'identité des sources publiques,
mais change le binaire et son empreinte dans le reçu privé. Les binaires locaux
contenant les clés ne doivent pas être publiés. La CI produit ses propres
binaires sans les fichiers privés de cette installation.

Ce mécanisme permet de comparer un futur firmware installé à son reçu. Il ne
prouve pas quel ancien firmware se trouve actuellement sur la carte : aucun
dump, upload ou essai matériel n'a été effectué pendant ces corrections.
