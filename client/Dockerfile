FROM balenalib/raspberrypi3-debian:latest

# Installation des dépendances
RUN apt-get update && apt-get install -y \
    build-essential \
    cmake \
    pkg-config \
    libprotobuf-dev \
    protobuf-compiler \
    libgrpc++-dev \
    libgrpc-dev \
    libssl-dev \
    libboost-all-dev \
    libjsoncpp-dev \
    librabbitmq-dev \
    git \
    && rm -rf /var/lib/apt/lists/*

# Création du répertoire de travail
WORKDIR /app

# Copie des fichiers
COPY . .

# Création des répertoires nécessaires
RUN mkdir -p build proto

# Compilation
RUN cd build && \
    cmake -DCMAKE_BUILD_TYPE=Release .. && \
    make -j$(nproc) || make

# Exposition du port si nécessaire
EXPOSE 8080

CMD ["./build/iotshadow_client"]