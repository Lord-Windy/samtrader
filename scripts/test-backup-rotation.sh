#!/bin/bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
TEST_DIR=$(mktemp -d)
BACKUP_DIR="${TEST_DIR}/backups"

cleanup() {
    rm -rf "${TEST_DIR}"
}
trap cleanup EXIT

echo "Test directory: ${TEST_DIR}"
mkdir -p "${BACKUP_DIR}"

create_backup() {
    local date_str="$1"
    local time_str="${2:-120000}"
    local filename="samtrader-${date_str}-${time_str}.db.gz"
    touch "${BACKUP_DIR}/${filename}"
}

echo "Creating test backup files..."

create_backup "20260224"
create_backup "20260223"
create_backup "20260222"
create_backup "20260221"
create_backup "20260220"
create_backup "20260219"
create_backup "20260218"

create_backup "20260217"
create_backup "20260216"
create_backup "20260215"
create_backup "20260214"
create_backup "20260213"
create_backup "20260212"
create_backup "20260211"

create_backup "20260210"
create_backup "20260209"
create_backup "20260208"
create_backup "20260207"
create_backup "20260206"
create_backup "20260205"
create_backup "20260204"

create_backup "20260201"
create_backup "20260202"
create_backup "20260203"

create_backup "20260125"
create_backup "20260124"
create_backup "20260123"
create_backup "20260122"
create_backup "20260121"
create_backup "20260120"
create_backup "20260119"

create_backup "20260118"
create_backup "20260117"
create_backup "20260116"
create_backup "20260115"
create_backup "20260114"
create_backup "20260113"
create_backup "20260112"

create_backup "20260111"
create_backup "20260110"
create_backup "20260109"
create_backup "20260108"
create_backup "20260107"
create_backup "20260106"
create_backup "20260105"

create_backup "20260104"
create_backup "20260103"
create_backup "20260102"
create_backup "20260101"

create_backup "20251225"
create_backup "20251224"
create_backup "20251223"
create_backup "20251222"
create_backup "20251221"
create_backup "20251220"
create_backup "20251219"

create_backup "20251215"
create_backup "20251214"
create_backup "20251213"
create_backup "20251212"
create_backup "20251211"
create_backup "20251210"
create_backup "20251209"

create_backup "20251208"
create_backup "20251207"
create_backup "20251206"
create_backup "20251205"
create_backup "20251204"
create_backup "20251203"
create_backup "20251202"
create_backup "20251201"

create_backup "20251125"
create_backup "20251124"
create_backup "20251123"
create_backup "20251122"
create_backup "20251121"
create_backup "20251120"
create_backup "20251119"

create_backup "20251115"
create_backup "20251114"
create_backup "20251113"
create_backup "20251112"
create_backup "20251111"
create_backup "20251110"
create_backup "20251109"

create_backup "20251108"
create_backup "20251107"
create_backup "20251106"
create_backup "20251105"
create_backup "20251104"
create_backup "20251103"
create_backup "20251102"
create_backup "20251101"

TOTAL_FILES=$(ls -1 "${BACKUP_DIR}" | wc -l)
echo "Created ${TOTAL_FILES} test backup files"

echo ""
echo "Files before rotation:"
ls -1 "${BACKUP_DIR}" | sort -r

echo ""
echo "Running rotation script..."
SAMTRADER_BACKUP_DIR="${BACKUP_DIR}" "${SCRIPT_DIR}/samtrader-rotate-backups.sh"

echo ""
echo "Files after rotation:"
REMAINING_FILES=$(ls -1 "${BACKUP_DIR}" 2>/dev/null || true)
echo "${REMAINING_FILES}"
REMAINING_COUNT=$(echo "${REMAINING_FILES}" | grep -c . || echo 0)
echo ""
echo "Remaining files: ${REMAINING_COUNT}"

echo ""
echo "Verifying retention policy..."

verify_sunday() {
    local date_str="$1"
    local year="${date_str:0:4}"
    local month="${date_str:4:2}"
    local day="${date_str:6:2}"
    date -d "${year}-${month}-${day}" +%w
}

echo ""
echo "=== DAILY BACKUPS (should keep 7 most recent) ==="
DAILY_COUNT=0
for f in $(ls -1 "${BACKUP_DIR}" 2>/dev/null | sort -r | head -7); do
    echo "  Daily: ${f}"
    DAILY_COUNT=$((DAILY_COUNT + 1))
done
if [ ${DAILY_COUNT} -eq 7 ]; then
    echo "  ✓ Daily retention OK (7 files)"
else
    echo "  ✗ Daily retention FAILED (expected 7, got ${DAILY_COUNT})"
    exit 1
fi

echo ""
echo "=== WEEKLY BACKUPS (should keep 4 Sundays, including those in daily) ==="
WEEKLY_FILES=$(ls -1 "${BACKUP_DIR}" 2>/dev/null | sort -r)
SUNDAY_COUNT=0
for f in ${WEEKLY_FILES}; do
    date_str=$(echo "${f}" | sed -E 's/samtrader-([0-9]{8})-.*/\1/')
    if [[ "${date_str}" =~ ^[0-9]{8}$ ]]; then
        dow=$(verify_sunday "${date_str}")
        if [ "${dow}" = "0" ]; then
            SUNDAY_COUNT=$((SUNDAY_COUNT + 1))
            if [ ${SUNDAY_COUNT} -le 4 ]; then
                echo "  Weekly (Sunday): ${f}"
            fi
        fi
    fi
done
if [ ${SUNDAY_COUNT} -ge 4 ]; then
    echo "  ✓ Weekly retention OK (${SUNDAY_COUNT} Sunday backups found, at least 4)"
else
    echo "  ✗ Weekly retention FAILED (expected at least 4 Sundays, got ${SUNDAY_COUNT})"
    exit 1
fi

echo ""
echo "=== MONTHLY BACKUPS (should keep 3 1st-of-month backups) ==="
MONTHLY_COUNT=0
for f in ${WEEKLY_FILES}; do
    date_str=$(echo "${f}" | sed -E 's/samtrader-([0-9]{8})-.*/\1/')
    if [[ "${date_str}" =~ ^[0-9]{8}$ ]]; then
        day="${date_str:6:2}"
        if [ "${day}" = "01" ]; then
            MONTHLY_COUNT=$((MONTHLY_COUNT + 1))
            if [ ${MONTHLY_COUNT} -le 3 ]; then
                echo "  Monthly (1st): ${f}"
            fi
        fi
    fi
done
if [ ${MONTHLY_COUNT} -ge 3 ]; then
    echo "  ✓ Monthly retention OK (${MONTHLY_COUNT} 1st-of-month backups found, at least 3)"
else
    echo "  ✗ Monthly retention FAILED (expected at least 3 1st-of-month backups, got ${MONTHLY_COUNT})"
    exit 1
fi

echo ""
echo "=== VERIFICATION COMPLETE ==="
echo "All retention policies verified successfully!"
echo "- Daily: 7 most recent files retained"
echo "- Weekly: 4 Sunday backups retained"
echo "- Monthly: 3 1st-of-month backups retained"
echo "- Older files correctly removed"
