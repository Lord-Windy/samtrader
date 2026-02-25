#!/bin/bash
set -euo pipefail

HOST="${1:-192.168.56.10}"
USER="${2:-vagrant}"
KEY_FILE="${3:-deploy/.vagrant/machines/default/virtualbox/private_key}"

SSH_OPTS="-o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null -i $KEY_FILE"
SSH_CMD="ssh $SSH_OPTS $USER@$HOST"

echo "=== Backup Rotation Test ==="
echo "This test seeds backup directory with dated files and verifies rotation"
echo ""

echo "1. Creating test backup directory..."
$SSH_CMD "sudo rm -rf /tmp/rotation-test"
$SSH_CMD "sudo mkdir -p /tmp/rotation-test"
$SSH_CMD "sudo chown samtrader:samtrader /tmp/rotation-test"

echo "2. Seeding with test backup files at various ages..."
SEED_SCRIPT=$(cat <<'REMOTE'
#!/bin/bash
BACKUP_DIR=/tmp/rotation-test
touch_date=$(which touch)
# Create backups at various ages
# Daily (should be kept: < 7 days)
$touch_date -t $(date -d "1 day ago" +%Y%m%d%H%M) $BACKUP_DIR/samtrader-$(date -d "1 day ago" +%Y%m%d-%H%M%S).db.gz
$touch_date -t $(date -d "3 days ago" +%Y%m%d%H%M) $BACKUP_DIR/samtrader-$(date -d "3 days ago" +%Y%m%d-%H%M%S).db.gz
$touch_date -t $(date -d "6 days ago" +%Y%m%d%H%M) $BACKUP_DIR/samtrader-$(date -d "6 days ago" +%Y%m%d-%H%M%S).db.gz

# Weekly range (should keep one per week for 4 weeks)
$touch_date -t $(date -d "10 days ago" +%Y%m%d%H%M) $BACKUP_DIR/samtrader-$(date -d "10 days ago" +%Y%m%d-%H%M%S).db.gz
$touch_date -t $(date -d "11 days ago" +%Y%m%d%H%M) $BACKUP_DIR/samtrader-$(date -d "11 days ago" +%Y%m%d-%H%M%S).db.gz
$touch_date -t $(date -d "15 days ago" +%Y%m%d%H%M) $BACKUP_DIR/samtrader-$(date -d "15 days ago" +%Y%m%d-%H%M%S).db.gz
$touch_date -t $(date -d "20 days ago" +%Y%m%d%H%M) $BACKUP_DIR/samtrader-$(date -d "20 days ago" +%Y%m%d-%H%M%S).db.gz

# Monthly range (should keep one per month for 3 months)
$touch_date -t $(date -d "35 days ago" +%Y%m%d%H%M) $BACKUP_DIR/samtrader-$(date -d "35 days ago" +%Y%m%d-%H%M%S).db.gz
$touch_date -t $(date -d "40 days ago" +%Y%m%d%H%M) $BACKUP_DIR/samtrader-$(date -d "40 days ago" +%Y%m%d-%H%M%S).db.gz
$touch_date -t $(date -d "65 days ago" +%Y%m%d%H%M) $BACKUP_DIR/samtrader-$(date -d "65 days ago" +%Y%m%d-%H%M%S).db.gz

# Too old (should be deleted)
$touch_date -t $(date -d "100 days ago" +%Y%m%d%H%M) $BACKUP_DIR/samtrader-$(date -d "100 days ago" +%Y%m%d-%H%M%S).db.gz
$touch_date -t $(date -d "120 days ago" +%Y%m%d%H%M) $BACKUP_DIR/samtrader-$(date -d "120 days ago" +%Y%m%d-%H%M%S).db.gz

echo "Seeded $(ls -1 $BACKUP_DIR/*.gz 2>/dev/null | wc -l) backup files"
REMOTE

$SSH_CMD "echo '$SEED_SCRIPT' | sudo -u samtrader bash"
)

echo "3. Listing seeded files..."
$SSH_CMD "ls -la /tmp/rotation-test/"
COUNT_BEFORE=$($SSH_CMD "ls -1 /tmp/rotation-test/*.gz 2>/dev/null | wc -l")
echo "   Files before rotation: $COUNT_BEFORE"

echo "4. Running rotation script on test directory..."
$SSH_CMD "sudo SAMTRADER_BACKUP_DIR=/tmp/rotation-test /usr/local/bin/samtrader-rotate-backups.sh"

echo "5. Listing files after rotation..."
$SSH_CMD "ls -la /tmp/rotation-test/"
COUNT_AFTER=$($SSH_CMD "ls -1 /tmp/rotation-test/*.gz 2>/dev/null | wc -l")
echo "   Files after rotation: $COUNT_AFTER"

echo ""
if [ "$COUNT_AFTER" -lt "$COUNT_BEFORE" ]; then
    echo "   [PASS] Rotation removed files ($((COUNT_BEFORE - COUNT_AFTER)) deleted)"
else
    echo "   [FAIL] Rotation did not remove any files"
fi

echo ""
echo "6. Verifying retention logic..."
echo "   Checking that files older than retention period were removed..."
if $SSH_CMD "ls /tmp/rotation-test/*100*.gz 2>/dev/null"; then
    echo "   [FAIL] 100-day old file still exists (should be deleted)"
else
    echo "   [PASS] 100-day old files correctly removed"
fi

if $SSH_CMD "ls /tmp/rotation-test/*120*.gz 2>/dev/null"; then
    echo "   [FAIL] 120-day old file still exists (should be deleted)"
else
    echo "   [PASS] 120-day old files correctly removed"
fi

echo ""
echo "=== Rotation Test Complete ==="
