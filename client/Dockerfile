# Multi-stage build pour optimiser la taille de l'image finale
FROM ubuntu:22.04 AS builder

# Éviter les prompts interactifs pendant l'installation
ENV DEBIAN_FRONTEND=noninteractive

# Mettre à jour et installer les dépendances de build
RUN apt-get update && apt-get install -y \
    build-essential \
    cmake \
    git \
    pkg-config \
    libssl-dev \
    libboost-all-dev \
    libjsoncpp-dev \
    librabbitmq-dev \
    libprotobuf-dev \
    protobuf-compiler \
    libgrpc-dev \
    libgrpc++-dev \
    protobuf-compiler-grpc \
    libabsl-dev \
    wget \
    ca-certificates \
    && rm -rf /var/lib/apt/lists/*

# Installer jwt-cpp depuis les sources (car pas disponible dans les repos Ubuntu 22.04)
WORKDIR /tmp
RUN git clone https://github.com/Thalhammer/jwt-cpp.git && \
    cd jwt-cpp && \
    mkdir build && cd build && \
    cmake -DCMAKE_BUILD_TYPE=Release .. && \
    make -j$(nproc) && \
    make install

# Installer SimpleAmqpClient
RUN git clone https://github.com/alanxz/SimpleAmqpClient.git && \
    cd SimpleAmqpClient && \
    mkdir build && cd build && \
    cmake -DCMAKE_BUILD_TYPE=Release .. && \
    make -j$(nproc) && \
    make install

# Créer le répertoire de travail pour l'application
WORKDIR /app

# Copier les fichiers proto d'abord (pour optimiser le cache Docker)
COPY proto/ ./proto/

# Copier les sources du client
COPY client/ ./client/

# Construire l'application
WORKDIR /app/client
RUN mkdir -p build && cd build && \
    cmake .. && \
    make -j$(nproc)

# Stage final - image de production plus légère
FROM ubuntu:22.04

# Éviter les prompts interactifs
ENV DEBIAN_FRONTEND=noninteractive

# Installer seulement les dépendances runtime
RUN apt-get update && apt-get install -y \
    libssl3 \
    libboost-system1.74.0 \
    libboost-filesystem1.74.0 \
    libboost-thread1.74.0 \
    libjsoncpp25 \
    librabbitmq4 \
    libprotobuf23 \
    libgrpc++1 \
    libabsl20210324 \
    ca-certificates \
    && rm -rf /var/lib/apt/lists/* \
    && apt-get clean

# Copier les librairies installées manuellement depuis le stage builder
COPY --from=builder /usr/local/lib/libSimpleAmqpClient* /usr/local/lib/
COPY --from=builder /usr/local/include/jwt-cpp /usr/local/include/jwt-cpp

# Créer un utilisateur non-root pour la sécurité
RUN groupadd -r iotuser && useradd -r -g iotuser iotuser

# Créer les répertoires nécessaires
RUN mkdir -p /app/config /app/logs /app/scripts && \
    chown -R iotuser:iotuser /app

# Copier l'exécutable depuis le stage builder
COPY --from=builder /app/client/build/iotshadow_client /app/
COPY --from=builder /app/client/config/ /app/config/
COPY --from=builder /app/client/scripts/ /app/scripts/

# Rendre l'exécutable exécutable et appartenir à iotuser
RUN chmod +x /app/iotshadow_client && \
    chown iotuser:iotuser /app/iotshadow_client

# Mettre à jour le cache des librairies dynamiques
RUN ldconfig

# Définir le répertoire de travail
WORKDIR /app

# Changer vers l'utilisateur non-root
USER iotuser

# Exposer les ports si nécessaire (à ajuster selon vos besoins)
# EXPOSE 8080 5672

# Variables d'environnement par défaut
ENV LOG_LEVEL=INFO
ENV CONFIG_PATH=/app/config/config.txt

# Point d'entrée
ENTRYPOINT ["./iotshadow_client"]

# Commande par défaut (peut être surchargée)
CMD []