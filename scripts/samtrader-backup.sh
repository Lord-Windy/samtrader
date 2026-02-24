#!/bin/bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
CONFIG_FILE="${SAMTRADER_CONFIG:-/etc/samtrader/config.ini}"

DB_PATH="${SAMTRADER_DB:-/var/lib/samtrader/samtrader.db}"

get_config_value() {
    local key="$1"
    local default="$2"
    
    if [ -f "${CONFIG_FILE}" ]; then
        local value
        value=$(grep -E "^${key}\s*=" "${CONFIG_FILE}" 2>/dev/null | head -1 | sed 's/[^=]*=\s*//' | tr -d ' ;')
        if [ -n "${value}" ]; then
            echo "${value}"
            return
        fi
    fi
    echo "${default}"
}

DEFAULT_BACKUP_DIR=$(get_config_value "destination" "/var/backups/samtrader")
BACKUP_DIR="${SAMTRADER_BACKUP_DIR:-${DEFAULT_BACKUP_DIR}}"

TIMESTAMP=$(date +%Y%m%d-%H%M%S)
BACKUP_FILE="${BACKUP_DIR}/samtrader-${TIMESTAMP}.db"

mkdir -p "${BACKUP_DIR}"

sqlite3 "${DB_PATH}" ".backup '${BACKUP_FILE}'"

gzip "${BACKUP_FILE}"

echo "Backup created: ${BACKUP_FILE}.gz"
