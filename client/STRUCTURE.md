# Structure du dossier `client/`

Ce fichier résume l'organisation du dossier `client/` du projet IOTSHADOW.

## Arborescence principale

```
client/
├── CMakeLists.txt           # Configuration CMake pour le client unifié
├── main.cpp                 # Point d'entrée principal du client
├── build/                   # Répertoire de build (généré)
├── config/                  # Fichiers de configuration client
│   └── config.txt
├── logs/                    # Logs générés par le client
├── monitoring-service/      # Code du service de monitoring côté client
│   ├── include/
│   │   ├── metrics_collector.h
│   │   └── rabbitmq_sender.h
│   ├── src/
│   │   ├── client.cpp
│   │   ├── metrics_collector.cpp
│   │   └── rabbitmq_sender.cpp
├── provision-service/       # Code du service de provisioning côté client
│   ├── include/
│   └── src/
├── ota-service/             # Code du service OTA côté client
│   ├── include/
│   └── src/
├── scripts/                 # Scripts utilitaires (ex: collecte de métriques)
│   └── collect_metrics.sh
```

## Description des dossiers
- **main.cpp** : Point d'entrée du client unifié, intègre tous les services.
- **monitoring-service/** : Code source et headers pour la collecte et l'envoi des métriques.
- **provision-service/** : Code source et headers pour la gestion du provisioning.
- **ota-service/** : Code source et headers pour la gestion des mises à jour OTA.
- **config/** : Fichiers de configuration spécifiques au client.
- **logs/** : Logs générés par le client.
- **scripts/** : Scripts shell pour automatiser certaines tâches (ex: collecte de métriques).

## Notes
- Tous les services partagent les fichiers proto situés dans `../proto/`.
- Les dépendances sont gérées via le CMake principal du dossier `client/`.
- Les exécutables et fichiers temporaires sont générés dans `build/`.
