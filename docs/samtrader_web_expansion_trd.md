# Samtrader Web Expansion TRD

A technical requirements document for adding a web interface to samtrader.
Covers the SQLite data adapter, Axum web server, HTMX frontend, deployment
on Debian behind Caddy, Ansible provisioning, and SQLite backup strategy.
This is Phase 3 of the samtrader project, building on the core backtesting
engine (Phases 1-2) documented in `samtrader_reimplementation_trd.md`.

---

## 1. Overview

### 1.1 Purpose

Extend samtrader from a CLI-only backtester to include a web interface that
allows users to configure strategies, run backtests, and view reports through a
browser. The web interface provides the same analytical capabilities as the CLI
but with interactive HTML output instead of Typst/PDF reports.

### 1.2 Goals

- Serve backtest results as interactive HTML pages via a web interface
- Replace PostgreSQL with SQLite as the default data backend (simpler
  deployment, zero external dependencies)
- Deploy as a single binary on a Debian server behind a Caddy reverse proxy
- Provide Ansible playbooks for repeatable server provisioning
- Implement automated SQLite backup with configurable destination

### 1.3 Non-Goals

- Real-time data feeds or live trading
- Multi-user SaaS deployment (single-tenant, single-user)
- Client-side JavaScript frameworks (React, Vue, etc.)
- WebSocket push updates (polling or full page reload is acceptable)
- Mobile-specific responsive design (desktop-first)
- API-first design (no JSON REST API; HTML fragments served directly)

### 1.4 Relationship to Prior Phases

The hexagonal architecture established in Phases 1-2 makes this expansion
purely additive. The domain layer (`domain/`), port traits (`ports/`), and
existing adapters are unchanged. This phase adds:

- A new `SqliteAdapter` implementing the existing `DataPort` trait
- A new `HtmlReportAdapter` implementing the existing `ReportPort` trait
- A `serve` subcommand that starts an Axum web server
- Deployment infrastructure (Caddy, systemd, Ansible)

The existing CLI pipeline (`backtest`, `validate`, `list-symbols`, `info`) and
Typst report generation continue to work exactly as before.

---

## 2. Architecture Changes

### 2.1 What Stays

| Component | Status |
|---|---|
| `domain/` (all modules) | Unchanged |
| `ports/data_port.rs` | Unchanged (SqliteAdapter implements it) |
| `ports/report_port.rs` | Unchanged (HtmlReportAdapter implements it) |
| `ports/config_port.rs` | Unchanged |
| `adapters/postgres_adapter.rs` | Unchanged, still feature-gated |
| `adapters/csv_adapter.rs` | Unchanged |
| `adapters/typst_report/` | Unchanged |
| `adapters/file_config_adapter.rs` | Unchanged |
| `cli.rs` (existing commands) | Unchanged; `Serve` and `HashPassword` variants added to `Command` enum |

### 2.2 New Project Structure

```
samtrader/
├── Cargo.toml                        # Updated features and dependencies
├── src/
│   ├── lib.rs                        # Updated: re-exports new modules
│   ├── main.rs                       # Unchanged
│   ├── cli.rs                        # Updated: Serve + HashPassword commands
│   ├── domain/                       # Unchanged
│   ├── ports/                        # Unchanged
│   └── adapters/
│       ├── mod.rs                    # Updated: conditional module declarations
│       ├── postgres_adapter.rs       # Unchanged
│       ├── csv_adapter.rs            # Unchanged
│       ├── file_config_adapter.rs    # Unchanged
│       ├── typst_report/             # Unchanged
│       ├── sqlite_adapter.rs         # NEW: SQLite DataPort implementation
│       └── web/                      # NEW: Web server adapter
│           ├── mod.rs                # Axum app builder, AppState, router
│           ├── handlers.rs           # Request handlers (backtest, results, etc.)
│           ├── auth.rs               # axum-login integration, session management
│           ├── error.rs              # HTTP error responses, status mapping
│           └── templates/            # Askama HTML templates
│               ├── base.html         # Page layout (head, nav, footer)
│               ├── login.html        # Login form
│               ├── dashboard.html    # Strategy list / recent backtests
│               ├── backtest.html     # Backtest configuration form
│               ├── report.html       # Full backtest report (HTML equivalent)
│               ├── report_fragment.html  # HTMX partial for report sections
│               └── error.html        # Error display page
├── deploy/                           # NEW: Deployment infrastructure
│   ├── ansible/
│   │   ├── playbook.yml              # Main playbook
│   │   ├── inventory.ini             # Host configuration
│   │   └── roles/
│   │       ├── base/                 # SSH hardening, UFW, fail2ban, upgrades
│   │       ├── caddy/                # Caddy install + Caddyfile
│   │       ├── samtrader/            # Binary deploy, systemd unit, config
│   │       └── backup/               # SQLite backup cron + rotation
│   ├── samtrader.service             # systemd unit file
│   └── Caddyfile                     # Reverse proxy config
└── tests/                            # Updated: new integration tests
```

### 2.3 Dependency Flow

```
              ┌─────────────────┐     ┌─────────────────┐
              │  CLI (main.rs)  │     │  serve command   │
              │  backtest, etc. │     │  (Axum server)   │
              └────────┬────────┘     └────────┬────────┘
                       │                       │
                       │   both depend on      │
                       ▼                       ▼
              ┌────────────────────────────────────────┐
              │           Library Core                 │
              │  domain + ports + adapters              │
              │                                        │
              │  ┌──────────┐  ┌──────────────────┐    │
              │  │ Postgres │  │ SQLite            │    │
              │  │ Adapter  │  │ Adapter           │    │
              │  │(feature) │  │ (feature)         │    │
              │  └──────────┘  └──────────────────┘    │
              │  ┌──────────┐  ┌──────────────────┐    │
              │  │ Typst    │  │ HTML Report       │    │
              │  │ Report   │  │ Adapter           │    │
              │  │          │  │ (feature)         │    │
              │  └──────────┘  └──────────────────┘    │
              └────────────────────────────────────────┘
```

**Key principle**: The `serve` command and the `backtest` CLI command are
sibling consumers of the same library. Web handlers call the same domain
functions (`run_backtest_pipeline`, `build_strategy`, `build_backtest_config`,
etc.) that the CLI uses. No domain logic is duplicated.

---

## 3. Database Migration

### 3.1 SQLite Schema

The SQLite schema mirrors the PostgreSQL `ohlcv` table but uses SQLite-native
types:

```sql
CREATE TABLE ohlcv (
    code     TEXT    NOT NULL,
    exchange TEXT    NOT NULL,
    date     TEXT    NOT NULL,   -- ISO 8601: 'YYYY-MM-DD'
    open     REAL    NOT NULL,
    high     REAL    NOT NULL,
    low      REAL    NOT NULL,
    close    REAL    NOT NULL,
    volume   INTEGER NOT NULL
);

CREATE INDEX idx_ohlcv_code_exchange_date
    ON ohlcv(code, exchange, date);

CREATE UNIQUE INDEX idx_ohlcv_unique
    ON ohlcv(code, exchange, date);
```

Dates are stored as ISO 8601 text (`YYYY-MM-DD`) rather than Unix timestamps
for human readability and direct string comparison in queries.

### 3.2 SqliteAdapter

```rust
use rusqlite::Connection;
use r2d2::Pool;
use r2d2_sqlite::SqliteConnectionManager;

pub struct SqliteAdapter {
    pool: Pool<SqliteConnectionManager>,
}
```

The adapter uses `r2d2` connection pooling for web concurrency. Each web
request checks out a connection from the pool; the CLI path uses a pool of
size 1 (effectively single-connection).

#### 3.2.1 Construction

```rust
impl SqliteAdapter {
    pub fn from_config(config: &dyn ConfigPort) -> Result<Self, SamtraderError> {
        let db_path = config
            .get_string("sqlite", "path")
            .ok_or_else(|| SamtraderError::ConfigMissing {
                section: "sqlite".into(),
                key: "path".into(),
            })?;

        let pool_size = config.get_int("sqlite", "pool_size", 4) as u32;

        let manager = SqliteConnectionManager::file(&db_path);
        let pool = Pool::builder()
            .max_size(pool_size)
            .build(manager)
            .map_err(|e| SamtraderError::Database {
                reason: e.to_string(),
            })?;

        // Enable WAL mode on first connection
        let conn = pool.get().map_err(|e| SamtraderError::Database {
            reason: e.to_string(),
        })?;
        conn.execute_batch("PRAGMA journal_mode=WAL; PRAGMA busy_timeout=5000;")
            .map_err(|e| SamtraderError::Database {
                reason: e.to_string(),
            })?;

        Ok(Self { pool })
    }
}
```

#### 3.2.2 WAL Mode

SQLite is configured with Write-Ahead Logging (WAL) mode:
- Allows concurrent reads while a write is in progress
- Required for web concurrency (multiple requests reading simultaneously)
- `busy_timeout=5000` prevents immediate `SQLITE_BUSY` errors under contention

#### 3.2.3 DataPort Implementation

```rust
impl DataPort for SqliteAdapter {
    fn fetch_ohlcv(
        &self,
        code: &str,
        exchange: &str,
        start_date: NaiveDate,
        end_date: NaiveDate,
    ) -> Result<Vec<OhlcvBar>, SamtraderError> {
        let conn = self.pool.get().map_err(|e| SamtraderError::Database {
            reason: e.to_string(),
        })?;

        let mut stmt = conn.prepare(
            "SELECT code, exchange, date, open, high, low, close, volume \
             FROM ohlcv \
             WHERE code = ?1 AND exchange = ?2 AND date >= ?3 AND date <= ?4 \
             ORDER BY date ASC"
        ).map_err(|e| SamtraderError::DatabaseQuery {
            reason: e.to_string(),
        })?;

        let start_str = start_date.format("%Y-%m-%d").to_string();
        let end_str = end_date.format("%Y-%m-%d").to_string();

        let bars = stmt.query_map(
            rusqlite::params![code, exchange, start_str, end_str],
            |row| {
                let date_str: String = row.get(2)?;
                Ok(OhlcvBar {
                    code: row.get(0)?,
                    exchange: row.get(1)?,
                    date: NaiveDate::parse_from_str(&date_str, "%Y-%m-%d")
                        .unwrap_or_default(),
                    open: row.get(3)?,
                    high: row.get(4)?,
                    low: row.get(5)?,
                    close: row.get(6)?,
                    volume: row.get(7)?,
                })
            },
        ).map_err(|e| SamtraderError::DatabaseQuery {
            reason: e.to_string(),
        })?;

        bars.collect::<Result<Vec<_>, _>>()
            .map_err(|e| SamtraderError::DatabaseQuery {
                reason: e.to_string(),
            })
    }

    fn list_symbols(&self, exchange: &str) -> Result<Vec<String>, SamtraderError> {
        let conn = self.pool.get().map_err(|e| SamtraderError::Database {
            reason: e.to_string(),
        })?;
        let mut stmt = conn.prepare(
            "SELECT DISTINCT code FROM ohlcv WHERE exchange = ?1 ORDER BY code"
        ).map_err(|e| SamtraderError::DatabaseQuery {
            reason: e.to_string(),
        })?;

        let symbols = stmt.query_map([exchange], |row| row.get(0))
            .map_err(|e| SamtraderError::DatabaseQuery {
                reason: e.to_string(),
            })?;

        symbols.collect::<Result<Vec<String>, _>>()
            .map_err(|e| SamtraderError::DatabaseQuery {
                reason: e.to_string(),
            })
    }

    fn get_data_range(
        &self,
        code: &str,
        exchange: &str,
    ) -> Result<Option<(NaiveDate, NaiveDate, usize)>, SamtraderError> {
        let conn = self.pool.get().map_err(|e| SamtraderError::Database {
            reason: e.to_string(),
        })?;
        let mut stmt = conn.prepare(
            "SELECT MIN(date), MAX(date), COUNT(*) FROM ohlcv \
             WHERE code = ?1 AND exchange = ?2"
        ).map_err(|e| SamtraderError::DatabaseQuery {
            reason: e.to_string(),
        })?;

        let result = stmt.query_row([code, exchange], |row| {
            let min_date: Option<String> = row.get(0)?;
            let max_date: Option<String> = row.get(1)?;
            let count: i64 = row.get(2)?;
            Ok((min_date, max_date, count))
        }).map_err(|e| SamtraderError::DatabaseQuery {
            reason: e.to_string(),
        })?;

        match result {
            (Some(min_str), Some(max_str), count) if count > 0 => {
                let min = NaiveDate::parse_from_str(&min_str, "%Y-%m-%d")
                    .map_err(|e| SamtraderError::Database {
                        reason: format!("invalid date in database: {e}"),
                    })?;
                let max = NaiveDate::parse_from_str(&max_str, "%Y-%m-%d")
                    .map_err(|e| SamtraderError::Database {
                        reason: format!("invalid date in database: {e}"),
                    })?;
                Ok(Some((min, max, count as usize)))
            }
            _ => Ok(None),
        }
    }
}
```

### 3.3 Migration Utility

A `migrate` subcommand creates the SQLite schema:

```bash
samtrader migrate --sqlite /path/to/samtrader.db
```

This runs the `CREATE TABLE` and `CREATE INDEX` statements from Section 3.1.
It is idempotent (`CREATE TABLE IF NOT EXISTS`, `CREATE INDEX IF NOT EXISTS`).

---

## 4. Web Server

### 4.1 Serve Subcommand

The web server is started via a new CLI subcommand:

```bash
samtrader serve -c config.ini
```

Added to the `Command` enum:

```rust
#[derive(Subcommand, Debug)]
pub enum Command {
    // ... existing variants unchanged ...

    /// Start the web server
    Serve {
        #[arg(short, long)]
        config: PathBuf,
    },

    /// Hash a password for the [auth] config section
    HashPassword,
}
```

### 4.2 AppState

Shared application state passed to all handlers via Axum's state extraction:

```rust
use std::sync::Arc;

pub struct AppState {
    pub data_port: Arc<dyn DataPort + Send + Sync>,
    pub config: Arc<dyn ConfigPort + Send + Sync>,
}
```

The `SqliteAdapter` must implement `Send + Sync` (it does, since `r2d2::Pool`
is `Send + Sync`). The `PostgresAdapter` uses `RefCell` internally and is NOT
`Send + Sync`, so it cannot be used with the web server. This is acceptable:
the web server is designed for SQLite deployments.

### 4.3 Router Structure

```rust
use axum::{Router, routing::get, routing::post};

pub fn build_router(state: AppState) -> Router {
    Router::new()
        .route("/", get(handlers::dashboard))
        .route("/login", get(handlers::login_form).post(handlers::login))
        .route("/logout", post(handlers::logout))
        .route("/backtest", get(handlers::backtest_form))
        .route("/backtest/run", post(handlers::run_backtest))
        .route("/report/{id}", get(handlers::view_report))
        .route("/report/{id}/equity-chart", get(handlers::equity_chart_svg))
        .route("/report/{id}/drawdown-chart", get(handlers::drawdown_chart_svg))
        .layer(auth_layer)
        .with_state(Arc::new(state))
}
```

### 4.4 HTMX Fragment Pattern

The web interface uses HTMX for dynamic updates without full page reloads.
Handlers detect HTMX requests via the `HX-Request` header and return either:

- **Full page**: Complete HTML document (for direct navigation / non-HTMX)
- **Fragment**: Just the content div (for HTMX `hx-get` / `hx-post`)

```rust
fn is_htmx_request(headers: &HeaderMap) -> bool {
    headers.get("HX-Request").is_some()
}
```

### 4.5 Askama Templates

Templates use Askama (compile-time Jinja2-style templates for Rust):

```rust
#[derive(askama::Template)]
#[template(path = "report.html")]
struct ReportTemplate<'a> {
    strategy: &'a Strategy,
    metrics: &'a Metrics,
    code_results: Option<&'a [CodeResult]>,
    equity_svg: &'a str,
    drawdown_svg: &'a str,
    trades: &'a [ClosedTrade],
}
```

Templates are compiled into the binary at build time (no runtime file I/O for
template loading).

---

## 5. New Ports & Adapters

### 5.1 SqliteAdapter (DataPort)

See Section 3 for the full implementation. Summary:

| Trait Method | SQLite Implementation |
|---|---|
| `fetch_ohlcv()` | Parameterised query on `ohlcv` table, date as TEXT comparison |
| `list_symbols()` | `SELECT DISTINCT code` with exchange filter |
| `get_data_range()` | `MIN(date)`, `MAX(date)`, `COUNT(*)` aggregate query |

### 5.2 HtmlReportAdapter (ReportPort)

The `HtmlReportAdapter` generates an HTML report file instead of Typst markup.
It implements the existing `ReportPort` trait:

```rust
pub struct HtmlReportAdapter;

impl ReportPort for HtmlReportAdapter {
    fn write(
        &self,
        result: &BacktestResult,
        strategy: &Strategy,
        output_path: &str,
    ) -> Result<(), SamtraderError> {
        let metrics = Metrics::compute(
            &result.portfolio,
            0.05, // default risk-free rate
        );
        let equity_svg = chart_svg::generate_equity_svg(
            &result.portfolio.equity_curve,
        );
        let drawdown_svg = chart_svg::generate_drawdown_svg(
            &result.portfolio.equity_curve,
        );

        let template = ReportTemplate {
            strategy,
            metrics: &metrics,
            code_results: None,
            equity_svg: &equity_svg,
            drawdown_svg: &drawdown_svg,
            trades: &result.portfolio.closed_trades,
        };

        let html = template.render().map_err(|e| SamtraderError::Io(
            std::io::Error::new(std::io::ErrorKind::Other, e.to_string()),
        ))?;

        std::fs::write(output_path, html)?;
        Ok(())
    }
}
```

The SVG chart functions from `adapters/typst_report/chart_svg.rs` are reused
directly. The same `generate_equity_svg()` and `generate_drawdown_svg()`
functions produce inline SVG that works in both Typst (`#image.decode(...)`)
and HTML (`<img src="data:image/svg+xml;...">` or inline `<svg>`).

### 5.3 Handler-to-Domain Call Flow

```
Browser POST /backtest/run
    │
    ▼
handlers::run_backtest()
    │
    ├── cli::build_strategy()        ← reused from CLI
    ├── cli::build_backtest_config() ← reused from CLI
    ├── cli::resolve_codes()         ← reused from CLI
    │
    ▼
cli::run_backtest_pipeline()         ← the SAME function the CLI calls
    │
    ├── validate_universe()
    ├── fetch OHLCV + compute indicators
    ├── build timeline
    ├── backtest_engine::run_backtest()
    ├── Metrics::compute()
    └── return results to handler
    │
    ▼
handler renders HTML template with results
```

No domain logic is duplicated between CLI and web paths.

---

## 6. Authentication

### 6.1 Stack

- **axum-login**: Session-based authentication middleware for Axum
- **Argon2**: Password hashing (via the `argon2` crate)
- **tower-sessions**: Session storage (SQLite-backed)

### 6.2 User Model

Single-user authentication. The username and password hash are stored in the
INI configuration file, not in a database table:

```ini
[auth]
username = admin
password_hash = $argon2id$v=19$m=19456,t=2,p=1$...
session_secret = <random-64-hex-chars>
```

### 6.3 hash-password Subcommand

```bash
$ samtrader hash-password
Enter password: ********
Confirm password: ********
$argon2id$v=19$m=19456,t=2,p=1$base64salt$base64hash
```

The user pastes the output into their config file's `[auth]` section. The
password is never stored in plaintext.

### 6.4 Session Management

- Sessions are stored in a SQLite table (managed by `tower-sessions-sqlx` or
  equivalent)
- Session cookie: `HttpOnly`, `Secure`, `SameSite=Strict`
- Session lifetime: configurable, default 24 hours
- On login: verify password with `argon2::verify_encoded()`, create session
- On logout: destroy session, clear cookie
- All routes except `/login` require authentication

### 6.5 Login Flow

```
GET /login          → render login form
POST /login         → verify credentials
  success           → set session cookie, redirect to /
  failure           → re-render login form with error message
POST /logout        → destroy session, redirect to /login
```

---

## 7. Frontend

### 7.1 HTMX Patterns

The frontend uses server-rendered HTML enhanced with HTMX attributes. No
JavaScript build step, no node_modules, no bundler.

HTMX is loaded from a vendored copy in the binary (embedded via
`include_str!` or served from a static handler) to avoid CDN dependencies.

Common patterns:

```html
<!-- Trigger backtest and swap results into #report-container -->
<form hx-post="/backtest/run"
      hx-target="#report-container"
      hx-swap="innerHTML"
      hx-indicator="#spinner">
    ...
</form>

<!-- Lazy-load equity chart SVG -->
<div hx-get="/report/42/equity-chart"
     hx-trigger="load"
     hx-swap="innerHTML">
    Loading chart...
</div>
```

### 7.2 Page Layout

The base template (`base.html`) provides:

- `<head>` with HTMX script, minimal CSS (classless CSS framework or hand-
  written)
- Navigation bar (Dashboard, New Backtest, Logout)
- Content area (`{% block content %}`)
- Footer

### 7.3 Report Sections (HTML Equivalents of Typst)

Each section from the Typst report (TRD Phase 1, Section 12.1) maps to an
HTML equivalent:

| Typst Report Section | HTML Equivalent |
|---|---|
| Strategy Summary | `<table>` with strategy parameters |
| Aggregate Metrics | `<table>` with metric rows |
| Equity Curve | Inline `<svg>` (reuse `generate_equity_svg()`) |
| Drawdown Chart | Inline `<svg>` (reuse `generate_drawdown_svg()`) |
| Universe Summary | `<table>` with per-code summary rows |
| Per-Code Details | Collapsible `<details>` sections per code |
| Trade Log | Sortable `<table>` with all trades |
| Monthly Returns | `<table>` with colour-coded cells (CSS classes for positive/negative) |

### 7.4 Chart Rendering

SVG charts are generated server-side by the existing `chart_svg` module. The
same functions that produce SVG for Typst embedding produce valid SVG for
direct browser display. Charts are served as:

- Inline SVG in the report page, OR
- Separate endpoints (`/report/{id}/equity-chart`) returning
  `Content-Type: image/svg+xml` for lazy loading via HTMX

---

## 8. Configuration

### 8.1 New INI Sections

The following sections are added to the configuration file. Existing sections
(`[database]`, `[backtest]`, `[strategy]`, `[report]`) are unchanged.

#### [sqlite]

```ini
[sqlite]
path = /var/lib/samtrader/samtrader.db    ; Path to SQLite database file
pool_size = 4                              ; r2d2 pool size (default: 4)
```

#### [web]

```ini
[web]
bind = 127.0.0.1                          ; Listen address (default: 127.0.0.1)
port = 3000                                ; Listen port (default: 3000)
```

#### [auth]

```ini
[auth]
username = admin                           ; Login username
password_hash = $argon2id$...              ; Argon2 hash from `samtrader hash-password`
session_secret = <64-hex-chars>            ; HMAC key for session cookies
session_lifetime = 86400                   ; Session duration in seconds (default: 24h)
```

#### [backup]

```ini
[backup]
destination = /var/backups/samtrader       ; Backup output directory
retention_daily = 7                        ; Days of daily backups to keep
retention_weekly = 4                       ; Weeks of weekly backups to keep
retention_monthly = 3                      ; Months of monthly backups to keep
```

### 8.2 Feature Flag Interaction

The `[sqlite]` section is only read when the `sqlite` feature is enabled. The
`[web]` and `[auth]` sections are only read when the `web` feature is enabled.
The `[backup]` section is read by the backup script (not by the Rust binary).

---

## 9. Backup System

### 9.1 Strategy

SQLite backups use the `sqlite3 .backup` command, which performs a consistent
online backup even while the database is being read by the web server (WAL
mode ensures readers are not blocked).

### 9.2 Backup Script

```bash
#!/bin/bash
# /usr/local/bin/samtrader-backup.sh
set -euo pipefail

DB_PATH="${SAMTRADER_DB:-/var/lib/samtrader/samtrader.db}"
BACKUP_DIR="${SAMTRADER_BACKUP_DIR:-/var/backups/samtrader}"
TIMESTAMP=$(date +%Y%m%d-%H%M%S)
BACKUP_FILE="${BACKUP_DIR}/samtrader-${TIMESTAMP}.db"

mkdir -p "${BACKUP_DIR}"
sqlite3 "${DB_PATH}" ".backup '${BACKUP_FILE}'"
gzip "${BACKUP_FILE}"

echo "Backup created: ${BACKUP_FILE}.gz"
```

### 9.3 Rotation

A companion rotation script removes old backups according to the retention
policy:

| Tier | Retention | Schedule |
|---|---|---|
| Daily | 7 most recent | Runs daily via cron |
| Weekly | 4 most recent | Sunday backups kept as weekly |
| Monthly | 3 most recent | 1st-of-month backups kept as monthly |

Rotation logic: after creating a new backup, the script identifies backups
older than the retention window and removes them. Weekly and monthly backups
are simply daily backups that fall on the retention boundary dates; no
separate backup runs are needed.

### 9.4 Cron Configuration

```cron
# /etc/cron.d/samtrader-backup
0 3 * * * samtrader /usr/local/bin/samtrader-backup.sh
5 3 * * * samtrader /usr/local/bin/samtrader-rotate-backups.sh
```

Backups run at 03:00 daily as the `samtrader` system user. Rotation runs at
03:05.

### 9.5 Configurable Destination

The backup destination is configurable via:

1. The `[backup] destination` config key
2. The `SAMTRADER_BACKUP_DIR` environment variable (overrides config)

Default: `/var/backups/samtrader` (local server directory).

Future extensibility: the backup script can be extended to upload to cloud
storage (S3, GCS, B2) after the local backup completes. This is out of scope
for the initial implementation but the script structure accommodates it via
a post-backup hook.

---

## 10. Deployment

### 10.1 Target Environment

- **OS**: Debian 12 (Bookworm) or later
- **Reverse Proxy**: Caddy 2 (automatic HTTPS via Let's Encrypt)
- **Init System**: systemd
- **Provisioning**: Ansible

### 10.2 Caddyfile

```
samtrader.example.com {
    reverse_proxy 127.0.0.1:3000
}
```

Caddy handles:
- TLS certificate provisioning and renewal (automatic via ACME)
- HTTP → HTTPS redirect
- Reverse proxy to the samtrader Axum server on localhost

### 10.3 systemd Unit

```ini
[Unit]
Description=Samtrader Web Server
After=network.target

[Service]
Type=simple
User=samtrader
Group=samtrader
WorkingDirectory=/var/lib/samtrader
ExecStart=/usr/local/bin/samtrader serve -c /etc/samtrader/config.ini
Restart=on-failure
RestartSec=5

# Sandboxing
NoNewPrivileges=yes
ProtectSystem=strict
ProtectHome=yes
ReadWritePaths=/var/lib/samtrader /var/backups/samtrader
PrivateTmp=yes
ProtectKernelTunables=yes
ProtectKernelModules=yes
ProtectControlGroups=yes
RestrictAddressFamilies=AF_INET AF_INET6 AF_UNIX
RestrictNamespaces=yes
MemoryDenyWriteExecute=yes

[Install]
WantedBy=multi-user.target
```

### 10.4 Ansible Playbook Structure

```yaml
# deploy/ansible/playbook.yml
---
- hosts: samtrader
  become: yes
  roles:
    - base
    - caddy
    - samtrader
    - backup
```

#### Role: base

- Configure SSH (disable password auth, disable root login)
- Install and configure UFW (allow 22, 80, 443 only)
- Install and configure fail2ban
- Enable unattended-upgrades for security patches

#### Role: caddy

- Install Caddy from official APT repository
- Deploy Caddyfile from template (domain configurable via inventory vars)
- Enable and start Caddy service

#### Role: samtrader

- Create `samtrader` system user (no login shell)
- Create directories: `/var/lib/samtrader`, `/etc/samtrader`
- Copy compiled binary to `/usr/local/bin/samtrader`
- Deploy config.ini from template
- Deploy systemd unit file
- Run `samtrader migrate --sqlite /var/lib/samtrader/samtrader.db`
- Enable and start samtrader service

#### Role: backup

- Deploy backup and rotation scripts to `/usr/local/bin/`
- Create backup directory with correct ownership
- Deploy cron configuration

### 10.5 Inventory Variables

```ini
# deploy/ansible/inventory.ini
[samtrader]
my-server ansible_host=203.0.113.10

[samtrader:vars]
samtrader_domain=samtrader.example.com
samtrader_db_path=/var/lib/samtrader/samtrader.db
samtrader_backup_dir=/var/backups/samtrader
samtrader_web_port=3000
samtrader_auth_username=admin
```

---

## 11. Security

### 11.1 Server Hardening (Ansible base role)

| Control | Implementation |
|---|---|
| SSH | Key-only auth, no root login, non-standard port (optional) |
| Firewall | UFW: allow 22 (SSH), 80, 443 only; deny all other inbound |
| Brute-force protection | fail2ban monitoring SSH and Caddy access logs |
| Automatic patching | unattended-upgrades for security updates |

### 11.2 systemd Sandboxing

The systemd unit (Section 10.3) applies:

| Directive | Effect |
|---|---|
| `NoNewPrivileges` | Process cannot gain new privileges via setuid/setgid |
| `ProtectSystem=strict` | Filesystem is read-only except `ReadWritePaths` |
| `ProtectHome` | `/home` is inaccessible |
| `PrivateTmp` | Isolated `/tmp` namespace |
| `MemoryDenyWriteExecute` | No W+X memory pages (prevents code injection) |
| `RestrictAddressFamilies` | Only IPv4, IPv6, and Unix sockets |
| `RestrictNamespaces` | No new namespaces (prevents container escape) |

### 11.3 Application Security

| Threat | Mitigation |
|---|---|
| SQL injection | Parameterised queries (`?1`, `?2`) in all SQLite queries (same pattern as PostgresAdapter) |
| XSS | Askama auto-escapes all template variables by default; no `|safe` filter used without explicit review |
| CSRF | `SameSite=Strict` cookies; HTMX includes the session cookie automatically; form submissions validated via session |
| Session hijacking | `HttpOnly`, `Secure`, `SameSite=Strict` cookie flags |
| Password storage | Argon2id hashing; plaintext never stored or logged |
| Directory traversal | All file paths come from config, not user input; no file upload functionality |

---

## 12. Dependencies

### 12.1 Updated Feature Flags

```toml
[features]
default = ["sqlite"]
postgres = ["dep:postgres"]
sqlite = ["dep:rusqlite", "dep:r2d2", "dep:r2d2_sqlite"]
web = ["sqlite", "dep:axum", "dep:tokio", "dep:askama", "dep:askama_axum",
       "dep:axum-login", "dep:tower-sessions", "dep:tower-sessions-sqlite-store",
       "dep:tower", "dep:argon2"]
```

The default feature changes from `postgres` to `sqlite`. Users who need
PostgreSQL can still build with `--features postgres`. The `web` feature
implies `sqlite` (the web server requires SQLite for session storage and data
access).

### 12.2 New Dependencies

| Crate | Purpose | Feature Gate | Notes |
|---|---|---|---|
| `rusqlite` | SQLite database driver | `sqlite` | Sync API, bundled SQLite |
| `r2d2` | Connection pool | `sqlite` | Generic pool; used with `r2d2_sqlite` |
| `r2d2_sqlite` | SQLite pool manager for r2d2 | `sqlite` | |
| `axum` | HTTP framework | `web` | Tower-based, async |
| `tokio` | Async runtime | `web` | `features = ["full"]` |
| `askama` | Compile-time HTML templates | `web` | Jinja2-like syntax |
| `askama_axum` | Askama integration for Axum | `web` | `impl IntoResponse` for templates |
| `axum-login` | Session-based auth middleware | `web` | |
| `tower-sessions` | Session management | `web` | Cookie + backend store |
| `tower-sessions-sqlite-store` | SQLite session backend | `web` | |
| `tower` | Middleware framework | `web` | Required by axum-login |
| `argon2` | Password hashing | `web` | Argon2id variant |

### 12.3 Updated Cargo.toml

```toml
[package]
name = "samtrader"
version = "0.2.0"
edition = "2024"

[lib]
name = "samtrader"
path = "src/lib.rs"

[[bin]]
name = "samtrader"
path = "src/main.rs"

[features]
default = ["sqlite"]
postgres = ["dep:postgres"]
sqlite = ["dep:rusqlite", "dep:r2d2", "dep:r2d2_sqlite"]
web = ["sqlite", "dep:axum", "dep:tokio", "dep:askama", "dep:askama_axum",
       "dep:axum-login", "dep:tower-sessions", "dep:tower-sessions-sqlite-store",
       "dep:tower", "dep:argon2"]

[dependencies]
chrono = { version = "0.4", default-features = false, features = ["std"] }
clap = { version = "4", features = ["derive"] }
configparser = "3"
csv = "1"
thiserror = "2"

# PostgreSQL adapter (optional)
postgres = { version = "0.19", optional = true, features = ["with-chrono-0_4"] }

# SQLite adapter (optional)
rusqlite = { version = "0.31", optional = true, features = ["bundled"] }
r2d2 = { version = "0.8", optional = true }
r2d2_sqlite = { version = "0.24", optional = true }

# Web server (optional)
axum = { version = "0.7", optional = true }
tokio = { version = "1", optional = true, features = ["full"] }
askama = { version = "0.12", optional = true }
askama_axum = { version = "0.4", optional = true }
axum-login = { version = "0.15", optional = true }
tower-sessions = { version = "0.12", optional = true }
tower-sessions-sqlite-store = { version = "0.8", optional = true }
tower = { version = "0.4", optional = true }
argon2 = { version = "0.5", optional = true }

[dev-dependencies]
approx = "0.5"
proptest = "1"
tempfile = "3"
```

Note: version numbers are approximate and should be verified against
crates.io at implementation time.

---

## 13. Error Handling

### 13.1 New Error Variants

```rust
#[derive(Debug, thiserror::Error)]
pub enum SamtraderError {
    // ... existing variants unchanged ...

    #[error("web server error: {reason}")]
    WebServer { reason: String },

    #[error("authentication failed")]
    AuthFailed,

    #[error("template rendering error: {reason}")]
    TemplateRender { reason: String },
}
```

### 13.2 HTTP Status Mapping

| SamtraderError Variant | HTTP Status | Response |
|---|---|---|
| `ConfigMissing`, `ConfigInvalid`, `ConfigParse` | 400 Bad Request | Error page with config issue details |
| `AuthFailed` | 401 Unauthorized | Redirect to `/login` with error message |
| `Database`, `DatabaseQuery` | 500 Internal Server Error | Generic error page (no DB details leaked) |
| `NoData`, `InsufficientData` | 422 Unprocessable Entity | Error page explaining data issue |
| `RuleParse`, `RuleInvalid` | 400 Bad Request | Error page with parse error context |
| `TemplateRender` | 500 Internal Server Error | Fallback plain-text error |
| `WebServer` | 500 Internal Server Error | Generic error page |

### 13.3 HTML Error Responses

Error pages use the same base template for consistent styling. HTMX requests
receive error fragments (just the content div); non-HTMX requests receive full
pages.

```rust
pub async fn handle_error(err: SamtraderError) -> impl IntoResponse {
    let status = status_from_error(&err);
    let template = ErrorTemplate {
        message: err.to_string(),
        status: status.as_u16(),
    };
    (status, template)
}
```

---

## 14. Testing Strategy

### 14.1 Unit Tests

| Module | Test Focus |
|---|---|
| `sqlite_adapter.rs` | `from_config()` with missing/invalid paths; queries against in-memory SQLite (`":memory:"`) |
| `web/handlers.rs` | Handler functions with mock state; verify template rendering |
| `web/auth.rs` | Password verification; session creation/destruction |
| `web/error.rs` | Error-to-status-code mapping for all variants |

SQLite unit tests use in-memory databases (`:memory:`) seeded with known OHLCV
data. This avoids filesystem dependencies and runs fast.

```rust
#[test]
fn sqlite_fetch_ohlcv_returns_bars() {
    let conn = Connection::open_in_memory().unwrap();
    conn.execute_batch(SCHEMA_SQL).unwrap();
    // Insert test data...
    // Wrap in SqliteAdapter...
    // Assert fetch_ohlcv returns expected bars
}
```

### 14.2 Integration Tests

- Full backtest pipeline via `SqliteAdapter` with seeded in-memory database
- Verify that `SqliteAdapter` and `PostgresAdapter` return identical results
  for the same data
- Web handler integration: use `axum::test` utilities to send HTTP requests
  and verify response status codes and body content
- Authentication flow: login → access protected route → logout → 401

### 14.3 End-to-End Tests

- **Ansible**: Run playbook against a Vagrant/Docker test VM; verify service
  starts, Caddy responds on 443, backup script produces valid `.db.gz` files
- **Backup rotation**: Seed backup directory with dated files; run rotation
  script; verify correct files are retained/removed
- **Full web flow**: Start server, log in, submit backtest form, verify report
  page renders with expected sections (equity chart, metrics table, trade log)

---

## 15. Implementation Phases

### Phase 3.1: SQLite Adapter

1. Add `rusqlite`, `r2d2`, `r2d2_sqlite` dependencies with `sqlite` feature
2. Change default feature from `postgres` to `sqlite`
3. Implement `SqliteAdapter` with `DataPort` trait
4. Add `migrate` subcommand for schema creation
5. Unit tests with in-memory SQLite
6. Update CLI dispatch to use `SqliteAdapter` when `sqlite` feature is active

### Phase 3.2: Web Foundation

1. Add `axum`, `tokio`, `askama` dependencies with `web` feature
2. Add `Serve` variant to `Command` enum
3. Implement `AppState`, router, and basic handlers (dashboard, 404)
4. Implement base template with HTMX
5. Add backtest form and `run_backtest` handler (reusing CLI pipeline
   functions)
6. Add `HtmlReportAdapter` implementing `ReportPort`
7. Serve report as HTML page

### Phase 3.3: Authentication

1. Add `axum-login`, `tower-sessions`, `argon2` dependencies
2. Implement `hash-password` subcommand
3. Implement login/logout handlers and session management
4. Add auth middleware layer to router
5. Test authentication flow

### Phase 3.4: Templates & HTMX Polish

1. Implement all report section templates (equity chart, drawdown, metrics,
   trade log, monthly returns, universe summary)
2. Add HTMX lazy-loading for charts
3. Collapsible per-code detail sections
4. Error page template
5. Visual polish (CSS, layout)

### Phase 3.5: Deployment Infrastructure

1. Write systemd unit file with sandboxing directives
2. Write Caddyfile template
3. Write Ansible playbook with roles (base, caddy, samtrader, backup)
4. Write backup and rotation scripts
5. Test full deployment on a Debian VM

### Phase 3.6: Polish & Documentation

1. Verify all existing CLI tests still pass
2. Add integration tests for SQLite adapter
3. Add web handler tests
4. Verify backup rotation logic
5. Update README with web deployment instructions

---

## Appendix A: Existing Functions Reused by Web Handlers

The following public functions from `cli.rs` are called by web handlers with
no modification:

| Function | Purpose |
|---|---|
| `build_strategy()` | Parse strategy rules from config into `Strategy` struct |
| `build_backtest_config()` | Parse backtest parameters from config into `BacktestConfig` |
| `resolve_codes()` | Resolve code list from config (with optional override) |
| `collect_all_indicators()` | Extract required indicators from strategy rules |
| `run_backtest_pipeline()` | Full pipeline: validate → fetch → compute → backtest → metrics |
| `load_config()` | Load and parse INI config file |

## Appendix B: Existing SVG Functions Reused for Web

| Function | Module | Output |
|---|---|---|
| `generate_equity_svg()` | `adapters/typst_report/chart_svg.rs` | Inline SVG line chart |
| `generate_drawdown_svg()` | `adapters/typst_report/chart_svg.rs` | Inline SVG area chart |

These functions return `String` containing valid SVG markup. In the Typst
pipeline they are wrapped in `#image.decode(...)`. In the web pipeline they
are embedded directly as inline `<svg>` elements or served with
`Content-Type: image/svg+xml`.
