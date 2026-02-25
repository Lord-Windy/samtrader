# Samtrader

An algorithmic trading backtester with a Rust backend and optional web interface.

## Overview

Samtrader enables users to:
- Define trading rules using text-based function composition
- Backtest strategies against historical OHLCV data
- Run strategies across multiple instruments with a shared portfolio
- Generate reports (Typst/PDF via CLI, HTML via web interface)

## Installation

### From Source

```bash
# Default build (SQLite backend)
cargo build --release

# With PostgreSQL backend
cargo build --release --features postgres

# With web interface (includes SQLite)
cargo build --release --features web
```

### Feature Flags

| Flag | Description | Dependencies |
|------|-------------|--------------|
| `sqlite` (default) | SQLite data adapter | rusqlite, r2d2, r2d2_sqlite |
| `postgres` | PostgreSQL data adapter | postgres |
| `web` | Web server with HTML interface | axum, tokio, askama, axum-login, argon2, tower-sessions |

The `web` feature implies `sqlite` (web server requires SQLite for session storage).

## CLI Usage

### Backtest

```bash
# Run a backtest
samtrader backtest -c config.ini

# Dry-run mode (validate config without DB connection)
samtrader backtest -c config.ini --dry-run

# Override code from config
samtrader backtest -c config.ini --code BHP

# Specify output file
samtrader backtest -c config.ini -o report.typ
```

### Info Commands

```bash
# List symbols on an exchange
samtrader list-symbols --exchange ASX -c config.ini

# Show data range for symbol(s)
samtrader info --code BHP --exchange ASX -c config.ini

# Validate strategy configuration
samtrader validate -s strategy.ini
```

### Web Server

```bash
# Start the web server
samtrader serve -c config.ini
```

The web interface is available at `http://127.0.0.1:3000` by default. Configure the listen address in the `[web]` section.

### Password Hashing

For web deployments, generate a password hash:

```bash
samtrader hash-password
```

This prompts for a password and outputs an Argon2id hash suitable for use in deployment configuration (e.g. Ansible `[auth]` section).

### Database Migration

For SQLite deployments, initialize the database schema:

```bash
samtrader migrate --sqlite /path/to/samtrader.db
```

## Configuration

Configuration uses INI format. Copy `config.ini.example` and customize.

### Sections

#### [database] (PostgreSQL)

```ini
[database]
conninfo = postgresql://user:password@127.0.0.1:5432/samtrader
```

Used when built with `--features postgres`.

#### [sqlite]

```ini
[sqlite]
path = /var/lib/samtrader/samtrader.db
pool_size = 4
```

| Key | Description | Default |
|-----|-------------|---------|
| `path` | Path to SQLite database file | Required |
| `pool_size` | Connection pool size (for web) | 4 |

#### [backtest]

```ini
[backtest]
start_date = 2020-01-01
end_date = 2024-12-31
exchange = AU
codes = CBA, BHP, WBC, NAB
initial_capital = 100000.0
commission_per_trade = 9.95
commission_pct = 0.0
slippage_pct = 0.001
risk_free_rate = 0.05
allow_shorting = false
```

#### [strategy]

```ini
[strategy]
name = SMA Crossover
description = Buy when 20-day SMA crosses above 50-day SMA
entry_long = CROSS_ABOVE(SMA(20), SMA(50))
exit_long = CROSS_BELOW(SMA(20), SMA(50))
position_size = 0.25
stop_loss = 5.0
take_profit = 0.0
max_positions = 4
```

#### [report]

```ini
[report]
; template_path = /path/to/custom_template.typ
```

| Key | Description | Default |
|-----|-------------|---------|
| `template_path` | Path to custom Typst report template | Built-in default |

#### [web]

```ini
[web]
listen = 127.0.0.1:3000
```

| Key | Description | Default |
|-----|-------------|---------|
| `listen` | Socket address to bind | 127.0.0.1:3000 |

#### [backup]

> **Note:** This section is read by the backup shell scripts, not by the Rust binary.

```ini
[backup]
destination = /var/backups/samtrader
retention_daily = 7
retention_weekly = 4
retention_monthly = 3
```

| Key | Description | Default |
|-----|-------------|---------|
| `destination` | Backup output directory | /var/backups/samtrader |
| `retention_daily` | Days of daily backups | 7 |
| `retention_weekly` | Weeks of weekly backups | 4 |
| `retention_monthly` | Months of monthly backups | 3 |

## Deployment (Web)

### Ansible Provisioning

Deploy to a Debian server using the included Ansible playbooks:

```bash
cd deploy/ansible

# Edit inventory.ini with your server details
vim inventory.ini

# Run the playbook
ansible-playbook -i inventory.ini playbook.yml
```

### Inventory Variables

```ini
[samtrader]
my-server ansible_host=203.0.113.10

[samtrader:vars]
samtrader_domain=samtrader.example.com
samtrader_db_path=/var/lib/samtrader/samtrader.db
samtrader_backup_dir=/var/backups/samtrader
samtrader_web_port=3000
samtrader_auth_username=admin
```

### Ansible Roles

| Role | Purpose |
|------|---------|
| `base` | SSH hardening, UFW firewall, fail2ban, unattended-upgrades |
| `caddy` | Caddy reverse proxy with automatic HTTPS |
| `samtrader` | Binary deployment, systemd service, config |
| `backup` | SQLite backup scripts and cron jobs |

### Manual Deployment

1. Build the binary with web support:
   ```bash
   cargo build --release --features web
   ```

2. Create system user:
   ```bash
   sudo useradd -r -s /usr/sbin/nologin -d /var/lib/samtrader samtrader
   ```

3. Create directories:
   ```bash
   sudo mkdir -p /var/lib/samtrader /etc/samtrader /var/backups/samtrader
   sudo chown samtrader:samtrader /var/lib/samtrader /var/backups/samtrader
   ```

4. Install binary and config:
   ```bash
   sudo cp target/release/samtrader /usr/local/bin/
   sudo cp config.ini /etc/samtrader/
   sudo chmod 640 /etc/samtrader/config.ini
   sudo chown samtrader:samtrader /etc/samtrader/config.ini
   ```

5. Initialize database:
   ```bash
   sudo -u samtrader /usr/local/bin/samtrader migrate --sqlite /var/lib/samtrader/samtrader.db
   ```

6. Install systemd unit (see `deploy/samtrader.service`)

7. Install Caddy reverse proxy with a Caddyfile:
   ```
   samtrader.example.com {
       reverse_proxy 127.0.0.1:3000
   }
   ```

## Backup Configuration

Backups use SQLite's online backup mechanism and run via cron.

### Backup Script

The backup script (`scripts/samtrader-backup.sh`) performs:
1. Consistent online backup via `sqlite3 .backup`
2. Gzip compression
3. Timestamped output

### Rotation

The rotation script (`scripts/samtrader-rotate-backups.sh`) implements tiered retention:
- **Daily**: Keep all backups from the last N days
- **Weekly**: Keep one backup per week for N weeks
- **Monthly**: Keep one backup per month for N months

### Cron Schedule

```cron
# Daily backup at 03:00
0 3 * * * samtrader /usr/local/bin/samtrader-backup.sh

# Rotation at 03:05
5 3 * * * samtrader /usr/local/bin/samtrader-rotate-backups.sh
```

### Environment Variables

| Variable | Description |
|----------|-------------|
| `SAMTRADER_DB` | Database path (overrides config) |
| `SAMTRADER_BACKUP_DIR` | Backup directory (overrides config) |

## Strategy Rules

Rules are composable predicates defined in configuration:

### Comparison Rules

| Rule | Semantics |
|------|-----------|
| `CROSS_ABOVE(left, right)` | left crosses above right |
| `CROSS_BELOW(left, right)` | left crosses below right |
| `ABOVE(left, right)` | left > right |
| `BELOW(left, right)` | left < right |
| `BETWEEN(val, low, high)` | low <= val <= high |
| `EQUALS(left, right)` | left == right |

### Composite Rules

| Rule | Semantics |
|------|-----------|
| `AND(rule1, rule2, ...)` | All rules true |
| `OR(rule1, rule2, ...)` | Any rule true |
| `NOT(rule)` | Negation |

### Temporal Rules

| Rule | Semantics |
|------|-----------|
| `CONSECUTIVE(rule, n)` | Rule true for n consecutive bars |
| `ANY_OF(rule, n)` | Rule true at least once in n bars |

### Indicators

| Indicator | Syntax |
|-----------|--------|
| SMA | `SMA(n)` |
| EMA | `EMA(n)` |
| WMA | `WMA(n)` |
| RSI | `RSI(n)` |
| MACD | `MACD_LINE(f,s,sig)`, `MACD_SIGNAL(f,s,sig)`, `MACD_HISTOGRAM(f,s,sig)` |
| Bollinger | `BOLLINGER_UPPER(n,mult)`, `BOLLINGER_MIDDLE(n,mult)`, `BOLLINGER_LOWER(n,mult)` |
| Stochastic | `STOCHASTIC_K(k,d)`, `STOCHASTIC_D(k,d)` |
| ATR | `ATR(n)` |
| ROC | `ROC(n)` |
| STDDEV | `STDDEV(n)` |
| OBV | `OBV` |
| VWAP | `VWAP` |
| Pivot | `PIVOT`, `PIVOT_R1..R3`, `PIVOT_S1..S3` |

## Development

### Running Tests

```bash
cargo test

# With web feature
cargo test --features web
```

### Project Structure

```
samtrader/
├── src/
│   ├── lib.rs              # Library root
│   ├── main.rs             # CLI entry point
│   ├── domain/             # Core business logic
│   ├── ports/              # Trait definitions
│   └── adapters/           # Trait implementations
│       ├── sqlite_adapter.rs
│       ├── postgres_adapter.rs
│       ├── csv_adapter.rs
│       ├── typst_report/
│       └── web/            # (web feature)
├── deploy/
│   ├── ansible/            # Ansible playbooks
│   ├── Caddyfile
│   └── samtrader.service
├── scripts/                # Backup scripts
├── templates/              # Askama HTML templates
└── tests/                  # Integration tests
```

## License

MIT
