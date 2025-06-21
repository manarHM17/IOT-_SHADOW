#!/bin/bash

# === Configurable Variables ===
BASE_DIR="$(dirname "$(realpath "$0")")/.."   # remonte depuis scripts vers client
LOG_DIR="$BASE_DIR/logs"

READABLE_DATE=$(date +"%Y-%m-%d_%H-%M-%S")   # Date lisible avec tirets pour compatibilité fichier
HARDWARE_FILE="hardware_metrics_$READABLE_DATE.json"
SOFTWARE_FILE="software_metrics_$READABLE_DATE.json"
CHECK_SERVICES=("ssh" "cron" "mosquitto")    # Services à surveiller
PING_TARGET="8.8.8.8"                        # IP à tester pour la connectivité réseau

# === Créer le dossier de logs s'il n'existe pas ===
mkdir -p "$LOG_DIR"

# === Récupération des informations système ===
CPU_USAGE=$(top -bn1 | grep "Cpu(s)" | awk '{print $2 + $4}') # Utilisation du CPU
CPU_USAGE="${CPU_USAGE}%"                                    # Ajouter le symbole %

MEMORY_TOTAL=$(free -m | awk '/Mem:/ {print $2}')             # Mémoire totale en Mo
MEMORY_USED=$(free -m | awk '/Mem:/ {print $3}')              # Mémoire utilisée en Mo
MEMORY_USAGE_PERCENT=$(awk "BEGIN {printf \"%.2f\", ($MEMORY_USED/$MEMORY_TOTAL)*100}") # Calculer le pourcentage
MEMORY_USAGE_PERCENT="${MEMORY_USAGE_PERCENT}%"              # Ajouter le symbole %

UPTIME=$(uptime -p)                                          # Temps de fonctionnement

DISK_USAGE=$(df -h / | awk 'NR==2 {print $5}')               # Utilisation du disque racine

# === USB Devices Detection ===
USB_DEVICES=$(lsusb)

if [ -z "$USB_DEVICES" ]; then
    USB_DATA="\"usb_state\": \"none\""
else
    # Escape newlines and quotes for JSON
    USB_DEVICES_CLEANED=$(echo "$USB_DEVICES" | sed ':a;N;$!ba;s/\n/ | /g' | sed 's/\"/\\"/g')
    USB_DATA="\"usb_state\": \"$USB_DEVICES_CLEANED\""
fi

# === GPIO State Count ===
GPIO_STATE=$(if command -v gpio >/dev/null 2>&1; then gpio readall | grep -c "ON"; else echo "0"; fi)

IP_ADDRESS=$(hostname -I | awk '{print $1}')                 # Adresse IP

PING_STATUS=$(ping -c 1 $PING_TARGET > /dev/null 2>&1 && echo "reachable" || echo "unreachable")

# === Vérifier l'état des services ===
declare -A SERVICE_STATUS
for service in "${CHECK_SERVICES[@]}"; do
    STATUS=$(systemctl is-active "$service" 2>/dev/null | tr -d '\n' || echo "unknown")
    SERVICE_STATUS["$service"]="$STATUS"
done

# === Détection des applications sous /opt au format app_name_version ===
declare -A APPLICATION_BINARIES

for entry in /opt/*; do
  if [[ -d "$entry" ]]; then
    bin_dir="$entry/bin"
    if [[ -d "$bin_dir" && -r "$bin_dir" && -x "$bin_dir" ]]; then
      echo "DEBUG: Scanning binaries in $bin_dir" >&2
      while IFS= read -r -d '' exe_file; do
        exe_name=$(basename "$exe_file")
        
        # Ignorer les fichiers contenant '.backup'
        if [[ "$exe_name" == *.backup* ]]; then
          echo "DEBUG: Skipping backup file $exe_name" >&2
          continue
        fi
        
        # Ne prendre que les fichiers au format nom_version (ex: print_hello_003)
        if [[ "$exe_name" =~ ^(.+)_([0-9]+)$ ]]; then
          app_base="${BASH_REMATCH[1]}"    # nom sans la version
          app_version="${BASH_REMATCH[2]}" # version extraite
          APPLICATION_BINARIES["$app_base"]="$app_version"
          echo "DEBUG: Added executable $exe_name as $app_base with version $app_version" >&2
        else
          echo "DEBUG: Skipping file $exe_name (does not match name_version pattern)" >&2
        fi
      done < <(find "$bin_dir" -maxdepth 1 -type f -executable -print0 2>/dev/null)
    else
      echo "DEBUG: No accessible bin directory in $entry" >&2
    fi

  elif [[ -f "$entry" && -x "$entry" ]]; then
    exe_name=$(basename "$entry")
    
    # Ignorer les fichiers contenant '.backup'
    if [[ "$exe_name" == *.backup* ]]; then
      echo "DEBUG: Skipping backup file $exe_name" >&2
      continue
    fi
    
    # Ne prendre que les fichiers au format nom_version
    if [[ "$exe_name" =~ ^(.+)_([0-9]+)$ ]]; then
      app_base="${BASH_REMATCH[1]}"
      app_version="${BASH_REMATCH[2]}"
      APPLICATION_BINARIES["$app_base"]="$app_version"
      echo "DEBUG: Added executable $exe_name as $app_base with version $app_version" >&2
    else
      echo "DEBUG: Skipping file $exe_name (does not match name_version pattern)" >&2
    fi

  else
    echo "DEBUG: $entry is not executable or directory, skipping" >&2
  fi
done


# === Récupération des versions kernel, matériel et firmware ===
KERNEL_VERSION=$(uname -r)
# RPi specific hardware info from cpuinfo
HARDWARE_MODEL=$(grep "Model" /proc/cpuinfo | cut -d: -f2- | sed 's/^ *//' || echo "unknown")
# RPi firmware version using vcgencmd (raspberry pi specific)
FIRMWARE_VERSION=$(vcgencmd version 2>/dev/null || echo "unknown")
# === Récupération de la version du système d'exploitation ===
OS_VERSION=$(grep '^PRETTY_NAME=' /etc/os-release | cut -d= -f2- | tr -d '"' || echo "unknown")

# === Création du fichier JSON pour les métriques matérielles ===
{
  echo "{"
  echo "  \"device_id\": \"$(head -n 1 "$BASE_DIR/config/config.txt")\","
  echo "  \"readable_date\": \"$READABLE_DATE\","
  echo "  \"cpu_usage\": \"$CPU_USAGE\","
  echo "  \"memory_usage\": \"$MEMORY_USAGE_PERCENT\","
  echo "  \"disk_usage\": \"$DISK_USAGE\","
  echo "  $USB_DATA,"
  echo "  \"gpio_state\": $GPIO_STATE,"
  echo "  \"kernel_version\": \"$KERNEL_VERSION\","
  echo "  \"hardware_model\": \"$HARDWARE_MODEL\","
  echo "  \"firmware_version\": \"$FIRMWARE_VERSION\""
  echo "}"
} > "$LOG_DIR/$HARDWARE_FILE"

# === Création du fichier JSON pour les métriques logicielles/réseau ===
{
  echo "{"
  echo "  \"device_id\": \"$(head -n 1 "$BASE_DIR/config/config.txt")\","
  echo "  \"readable_date\": \"$READABLE_DATE\","
  echo "  \"ip_address\": \"$IP_ADDRESS\","
  echo "  \"uptime\": \"$UPTIME\","
  echo "  \"os_version\": \"$OS_VERSION\","
  echo "  \"network_status\": \"$PING_STATUS\","
  echo -n "  \"application_binaries\": "
    if [ ${#APPLICATION_BINARIES[@]} -eq 0 ]; then
    echo "{},"
    else
    echo "{"
    count=${#APPLICATION_BINARIES[@]}
    i=0
    for name in "${!APPLICATION_BINARIES[@]}"; do
        i=$((i+1))
        if [ $i -lt $count ]; then
        echo "    \"$name\": \"${APPLICATION_BINARIES[$name]}\","
        else
        echo "    \"$name\": \"${APPLICATION_BINARIES[$name]}\""
        fi
    done
    echo "  },"
    fi

  echo "  \"services\": {"
  service_count=${#SERVICE_STATUS[@]}
  i=0
  for s in "${!SERVICE_STATUS[@]}"; do
    i=$((i+1))
    if [ $i -lt $service_count ]; then
      echo "    \"$s\": \"${SERVICE_STATUS[$s]}\","
    else
      echo "    \"$s\": \"${SERVICE_STATUS[$s]}\""
    fi
  done
  echo "  }"
  echo "}"
} > "$LOG_DIR/$SOFTWARE_FILE"

# === Supprimer les fichiers JSON plus vieux de 10 minutes ===
find "$LOG_DIR" -name "*.json" -type f -mmin +10 -exec rm -f {} \;