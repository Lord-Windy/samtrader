# Deploying Samtrader

## Prerequisites

- A Debian/Ubuntu server with SSH access and sudo
- Ansible 2.9+ on your local machine
- A domain pointing at your server (e.g. `samtrader.samuelbrown.me`)

> **Feature Migration:** The `web` feature has been replaced with explicit `web-sqlite` and `web-postgres` features. Update your build commands accordingly:
> - `--features web` → `--features web-sqlite`
> - `--features sqlite,web` → `--features web-sqlite`

## 1. DNS

Add an A record for your subdomain in your DNS provider (e.g. Hover):

| Type | Hostname    | Value            |
|------|-------------|------------------|
| A    | samtrader   | YOUR_SERVER_IP   |

Wait for propagation (usually a few minutes).

## 2. Build the binary

```bash
cargo build --release --features web-sqlite
cp target/release/samtrader deploy/ansible/roles/samtrader/files/
```

The binary must match your server's architecture (x86_64 Linux in most cases).

## 3. Generate secrets

**Password hash** (for web login):

```bash
cargo run --features web-sqlite -- hash-password
```

Paste the output as `samtrader_auth_password` when deploying.

**Session secret** (for signing cookies):

```bash
python3 -c "import secrets; print(secrets.token_hex(64))"
```

## 4. Configure the inventory

Edit `deploy/ansible/inventory.ini`:

```ini
[samtrader]
my-server ansible_host=YOUR_SERVER_IP

[samtrader:vars]
samtrader_domain=samtrader.samuelbrown.me
samtrader_db_path=/var/lib/samtrader/samtrader.db
samtrader_backup_dir=/var/backups/samtrader
samtrader_web_port=3000
samtrader_auth_username=admin
```

## 5. Deploy

First deploy (bootstrap — creates the `sam` user and locks down root):

```bash
cd deploy/ansible
ansible-playbook -i inventory.ini playbook.yml \
  -u root \
  -e samtrader_auth_password='PASTE_PASSWORD_HASH' \
  -e samtrader_session_secret='PASTE_HEX_SECRET'
```

Subsequent deploys (uses the `sam` user created during bootstrap):

```bash
cd deploy/ansible
ansible-playbook -i inventory.ini playbook.yml \
  -e samtrader_auth_password='PASTE_PASSWORD_HASH' \
  -e samtrader_session_secret='PASTE_HEX_SECRET'
```

This will:

- Harden SSH (key-only auth, no root login)
- Set up UFW firewall (allow 22, 80, 443 only)
- Install fail2ban and unattended-upgrades
- Install Caddy as a reverse proxy with automatic HTTPS
- Deploy the samtrader binary and config
- Run database migration
- Start the systemd service
- Set up daily backup cron jobs

## 6. Verify

```bash
# Service running?
ssh my-server 'systemctl status samtrader'

# HTTPS working?
curl https://samtrader.samuelbrown.me

# Logs
ssh my-server 'journalctl -u samtrader -f'
```

Or use the included script:

```bash
cd deploy
./verify-deployment.sh
```

## Local testing with Vagrant

To test the full deployment locally before hitting a real server:

```bash
cd deploy
vagrant up
cd ansible
ansible-playbook -i inventory.vagrant.ini playbook.yml
```

The VM is at `192.168.56.10`.

## Redeploying

After code changes, rebuild and redeploy:

```bash
cargo build --release --features web-sqlite
cd deploy/ansible
ansible-playbook -i inventory.ini playbook.yml
```

Ansible only restarts the service when the binary or config changes.

## Troubleshooting

**Service won't start:**
```bash
ssh my-server 'journalctl -u samtrader -n 50'
```

**Caddy/TLS issues:**
```bash
ssh my-server 'journalctl -u caddy -n 50'
```

Make sure your DNS A record is pointing at the right IP -- Caddy needs to reach Let's Encrypt to provision the certificate.

**Backup issues:**
```bash
ssh my-server 'ls -la /var/backups/samtrader'
ssh my-server '/usr/local/bin/samtrader-backup.sh'
```
