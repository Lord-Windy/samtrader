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

FAILURES=0

assert_kept() {
    local date_str="$1"
    local reason="$2"
    local filename="samtrader-${date_str}-120000.db.gz"
    if [ -f "${BACKUP_DIR}/${filename}" ]; then
        echo "  ✓ Kept ${filename} (${reason})"
    else
        echo "  ✗ MISSING ${filename} - should have been kept (${reason})"
        FAILURES=$((FAILURES + 1))
    fi
}

assert_deleted() {
    local date_str="$1"
    local reason="$2"
    local filename="samtrader-${date_str}-120000.db.gz"
    if [ ! -f "${BACKUP_DIR}/${filename}" ]; then
        echo "  ✓ Deleted ${filename} (${reason})"
    else
        echo "  ✗ STILL EXISTS ${filename} - should have been deleted (${reason})"
        FAILURES=$((FAILURES + 1))
    fi
}

echo "Creating test backup files..."

# Most recent 7 days (daily retention window)
create_backup "20260224"  # Tue - daily #1
create_backup "20260223"  # Mon - daily #2
create_backup "20260222"  # Sun - daily #3 + weekly Sunday #1
create_backup "20260221"  # Sat - daily #4
create_backup "20260220"  # Fri - daily #5
create_backup "20260219"  # Thu - daily #6
create_backup "20260218"  # Wed - daily #7

# Week of Feb 16 (outside daily window)
create_backup "20260217"  # Tue
create_backup "20260216"  # Mon
create_backup "20260215"  # Sun - weekly Sunday #2
create_backup "20260214"  # Sat
create_backup "20260213"  # Fri
create_backup "20260212"  # Thu
create_backup "20260211"  # Wed

# Week of Feb 9
create_backup "20260210"  # Tue
create_backup "20260209"  # Mon
create_backup "20260208"  # Sun - weekly Sunday #3
create_backup "20260207"  # Sat
create_backup "20260206"  # Fri
create_backup "20260205"  # Thu
create_backup "20260204"  # Wed

# Early Feb
create_backup "20260203"  # Tue
create_backup "20260202"  # Mon
create_backup "20260201"  # Sun - weekly Sunday #4 + monthly 1st #1

# Late January (5th+ Sundays - beyond weekly retention)
create_backup "20260125"  # Sun - 5th Sunday, should be deleted
create_backup "20260124"  # Sat
create_backup "20260123"  # Fri
create_backup "20260122"  # Thu
create_backup "20260121"  # Wed
create_backup "20260120"  # Tue
create_backup "20260119"  # Mon

create_backup "20260118"  # Sun - 6th Sunday, should be deleted
create_backup "20260117"  # Sat
create_backup "20260116"  # Fri
create_backup "20260115"  # Thu
create_backup "20260114"  # Wed
create_backup "20260113"  # Tue
create_backup "20260112"  # Mon

create_backup "20260111"  # Sun - 7th Sunday
create_backup "20260110"  # Sat
create_backup "20260109"  # Fri
create_backup "20260108"  # Thu
create_backup "20260107"  # Wed
create_backup "20260106"  # Tue
create_backup "20260105"  # Mon

create_backup "20260104"  # Sun - 8th Sunday
create_backup "20260103"  # Sat
create_backup "20260102"  # Fri
create_backup "20260101"  # Thu - monthly 1st #2

# December 2025
create_backup "20251225"  # Thu
create_backup "20251224"  # Wed
create_backup "20251223"  # Tue
create_backup "20251222"  # Mon
create_backup "20251221"  # Sun
create_backup "20251220"  # Sat
create_backup "20251219"  # Fri

create_backup "20251215"  # Mon
create_backup "20251214"  # Sun
create_backup "20251213"  # Sat
create_backup "20251212"  # Fri
create_backup "20251211"  # Thu
create_backup "20251210"  # Wed
create_backup "20251209"  # Tue

create_backup "20251208"  # Mon
create_backup "20251207"  # Sun
create_backup "20251206"  # Sat
create_backup "20251205"  # Fri
create_backup "20251204"  # Thu
create_backup "20251203"  # Wed
create_backup "20251202"  # Tue
create_backup "20251201"  # Mon - monthly 1st #3

# November 2025 (all beyond retention)
create_backup "20251125"  # Tue
create_backup "20251124"  # Mon
create_backup "20251123"  # Sun
create_backup "20251122"  # Sat
create_backup "20251121"  # Fri
create_backup "20251120"  # Thu
create_backup "20251119"  # Wed

create_backup "20251115"  # Sat
create_backup "20251114"  # Fri
create_backup "20251113"  # Thu
create_backup "20251112"  # Wed
create_backup "20251111"  # Tue
create_backup "20251110"  # Mon
create_backup "20251109"  # Sun

create_backup "20251108"  # Sat
create_backup "20251107"  # Fri
create_backup "20251106"  # Thu
create_backup "20251105"  # Wed
create_backup "20251104"  # Tue
create_backup "20251103"  # Mon
create_backup "20251102"  # Sun
create_backup "20251101"  # Sat - 4th monthly 1st, beyond retention

TOTAL_FILES=$(ls -1 "${BACKUP_DIR}" | wc -l)
echo "Created ${TOTAL_FILES} test backup files"

echo ""
echo "Running rotation script..."
SAMTRADER_BACKUP_DIR="${BACKUP_DIR}" "${SCRIPT_DIR}/samtrader-rotate-backups.sh"

echo ""
echo "=== VERIFYING RETAINED FILES ==="

# Daily: 7 most recent
assert_kept "20260224" "daily #1"
assert_kept "20260223" "daily #2"
assert_kept "20260222" "daily #3 + weekly Sunday #1"
assert_kept "20260221" "daily #4"
assert_kept "20260220" "daily #5"
assert_kept "20260219" "daily #6"
assert_kept "20260218" "daily #7"

# Weekly: 4 most recent Sundays (#1 already counted in daily)
assert_kept "20260215" "weekly Sunday #2"
assert_kept "20260208" "weekly Sunday #3"
assert_kept "20260201" "weekly Sunday #4 + monthly 1st #1"

# Monthly: 3 most recent 1st-of-month (#1 already counted in weekly)
assert_kept "20260101" "monthly 1st #2"
assert_kept "20251201" "monthly 1st #3"

echo ""
echo "=== VERIFYING DELETED FILES ==="

# Files just outside daily window (not Sunday, not 1st)
assert_deleted "20260217" "outside daily, not Sunday or 1st"
assert_deleted "20260216" "outside daily, not Sunday or 1st"
assert_deleted "20260214" "outside daily, not Sunday or 1st"
assert_deleted "20260210" "outside daily, not Sunday or 1st"
assert_deleted "20260207" "outside daily, not Sunday or 1st"

# Sundays beyond 4-week weekly retention
assert_deleted "20260125" "5th Sunday, beyond weekly retention"
assert_deleted "20260118" "6th Sunday, beyond weekly retention"
assert_deleted "20260111" "7th Sunday, beyond weekly retention"
assert_deleted "20260104" "8th Sunday, beyond weekly retention"

# 1st-of-month beyond 3-month monthly retention
assert_deleted "20251101" "4th monthly 1st, beyond monthly retention"

# Old files with no retention match
assert_deleted "20251225" "old, no retention match"
assert_deleted "20251115" "old, no retention match"
assert_deleted "20251105" "old, no retention match"

echo ""
echo "=== VERIFYING EXACT FILE COUNT ==="
REMAINING_COUNT=$(ls -1 "${BACKUP_DIR}" | wc -l)
if [ "${REMAINING_COUNT}" -eq 12 ]; then
    echo "  ✓ Exactly 12 files retained (7 daily + 2 weekly + 1 weekly/monthly + 2 monthly)"
else
    echo "  ✗ Expected exactly 12 files, got ${REMAINING_COUNT}"
    echo "  Remaining files:"
    ls -1 "${BACKUP_DIR}" | sort -r | while read -r f; do echo "    ${f}"; done
    FAILURES=$((FAILURES + 1))
fi

echo ""
if [ ${FAILURES} -gt 0 ]; then
    echo "=== FAILED: ${FAILURES} assertion(s) failed ==="
    exit 1
fi

echo "=== ALL ASSERTIONS PASSED ==="
echo "Retention policy verified (per TRD section 9.3):"
echo "  - Daily: 7 most recent files retained"
echo "  - Weekly: 4 most recent Sunday backups retained"
echo "  - Monthly: 3 most recent 1st-of-month backups retained"
echo "  - All other files correctly removed"
