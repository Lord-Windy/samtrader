#!/bin/bash
set -euo pipefail

HOST="${1:-192.168.56.10}"
USER="${2:-vagrant}"
KEY_FILE="${3:-.vagrant/machines/default/virtualbox/private_key}"

SSH_OPTS="-o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null -i $KEY_FILE"
SSH_CMD="ssh $SSH_OPTS $USER@$HOST"

echo "=== Samtrader Deployment Verification ==="
echo "Host: $HOST"
echo ""

echo "1. Checking samtrader service..."
$SSH_CMD "systemctl is-active samtrader" && echo "   [PASS] samtrader service is active" || echo "   [FAIL] samtrader service not running"
$SSH_CMD "systemctl is-enabled samtrader" && echo "   [PASS] samtrader service is enabled" || echo "   [FAIL] samtrader service not enabled"

echo ""
echo "2. Checking Caddy service..."
$SSH_CMD "systemctl is-active caddy" && echo "   [PASS] caddy service is active" || echo "   [FAIL] caddy service not running"
$SSH_CMD "systemctl is-enabled caddy" && echo "   [PASS] caddy service is enabled" || echo "   [FAIL] caddy service not enabled"

echo ""
echo "3. Checking HTTPS (port 443)..."
if $SSH_CMD "curl -sk https://localhost:443 -o /dev/null -w '%{http_code}'" | grep -q "200\|401"; then
    echo "   [PASS] Caddy responds on 443"
else
    echo "   [FAIL] Caddy not responding on 443"
fi

echo ""
echo "4. Checking backup script..."
if $SSH_CMD "test -x /usr/local/bin/samtrader-backup.sh"; then
    echo "   [PASS] Backup script exists and is executable"
else
    echo "   [FAIL] Backup script missing or not executable"
fi

echo ""
echo "5. Running backup test..."
$SSH_CMD "sudo -u samtrader /usr/local/bin/samtrader-backup.sh"
if $SSH_CMD "ls /var/backups/samtrader/*.db.gz 2>/dev/null"; then
    echo "   [PASS] Backup created successfully"
    $SSH_CMD "ls -lh /var/backups/samtrader/*.db.gz"
else
    echo "   [FAIL] No backup files found"
fi

echo ""
echo "6. Testing backup file integrity..."
BACKUP_FILE=$($SSH_CMD "ls -t /var/backups/samtrader/*.db.gz | head -1")
if [ -n "$BACKUP_FILE" ]; then
    if $SSH_CMD "zcat $BACKUP_FILE | head -c 16 | grep -q 'SQLite format 3'"; then
        echo "   [PASS] Backup file is valid SQLite database"
    else
        echo "   [FAIL] Backup file is not a valid SQLite database"
    fi
else
    echo "   [SKIP] No backup file to verify"
fi

echo ""
echo "7. Checking rotation script..."
if $SSH_CMD "test -x /usr/local/bin/samtrader-rotate-backups.sh"; then
    echo "   [PASS] Rotation script exists and is executable"
else
    echo "   [FAIL] Rotation script missing or not executable"
fi

echo ""
echo "8. Checking cron configuration..."
if $SSH_CMD "test -f /etc/cron.d/samtrader-backup"; then
    echo "   [PASS] Cron configuration exists"
else
    echo "   [FAIL] Cron configuration missing"
fi

echo ""
echo "=== Verification Complete ==="
