#!/bin/bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
CONFIG_FILE="${SAMTRADER_CONFIG:-/etc/samtrader/config.ini}"

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

RETENTION_DAILY=$(get_config_value "retention_daily" "7")
RETENTION_WEEKLY=$(get_config_value "retention_weekly" "4")
RETENTION_MONTHLY=$(get_config_value "retention_monthly" "3")

if [ ! -d "${BACKUP_DIR}" ]; then
    echo "Backup directory does not exist: ${BACKUP_DIR}"
    exit 0
fi

cd "${BACKUP_DIR}"

mapfile -t all_backups < <(find . -maxdepth 1 -name 'samtrader-*.db.gz' -type f | sed 's|^\./||' | sort -r)

if [ ${#all_backups[@]} -eq 0 ]; then
    echo "No backups found in ${BACKUP_DIR}"
    exit 0
fi

declare -A keep_files

daily_count=0
for backup in "${all_backups[@]}"; do
    if [ ${daily_count} -ge ${RETENTION_DAILY} ]; then
        break
    fi
    keep_files["${backup}"]=1
    daily_count=$((daily_count + 1))
done

weekly_count=0
for backup in "${all_backups[@]}"; do
    if [ ${weekly_count} -ge ${RETENTION_WEEKLY} ]; then
        break
    fi

    ts_str=$(echo "${backup}" | sed -E 's/samtrader-([0-9]{8})-[0-9]{6}\.db\.gz/\1/')
    if [[ ! "${ts_str}" =~ ^[0-9]{8}$ ]]; then
        continue
    fi

    year="${ts_str:0:4}"
    month="${ts_str:4:2}"
    day="${ts_str:6:2}"

    day_of_week=$(date -d "${year}-${month}-${day}" +%w 2>/dev/null || echo "")

    if [ "${day_of_week}" = "0" ]; then
        keep_files["${backup}"]=1
        weekly_count=$((weekly_count + 1))
    fi
done

monthly_count=0
for backup in "${all_backups[@]}"; do
    if [ ${monthly_count} -ge ${RETENTION_MONTHLY} ]; then
        break
    fi

    ts_str=$(echo "${backup}" | sed -E 's/samtrader-([0-9]{8})-[0-9]{6}\.db\.gz/\1/')
    if [[ ! "${ts_str}" =~ ^[0-9]{8}$ ]]; then
        continue
    fi

    day="${ts_str:6:2}"

    if [ "${day}" = "01" ]; then
        keep_files["${backup}"]=1
        monthly_count=$((monthly_count + 1))
    fi
done

deleted_count=0
for backup in "${all_backups[@]}"; do
    if [ -z "${keep_files[${backup}]:-}" ]; then
        rm -f "${backup}"
        deleted_count=$((deleted_count + 1))
        echo "Deleted: ${backup}"
    fi
done

echo "Rotation complete. Kept $((${#all_backups[@]} - deleted_count)) backups, deleted ${deleted_count}."
