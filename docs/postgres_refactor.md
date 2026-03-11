# PostgreSQL Migration Plan

## Context

SQLite is the current production database but importing a PostgreSQL dump is
painful due to syntax/index incompatibilities. The project already has a
`postgres_adapter.rs` with working query logic. With more historical data
coming from a paid API, PostgreSQL is the natural choice — it handles
concurrent reads/writes better and eliminates the conversion step entirely.

The goal: make PostgreSQL the primary database for both OHLCV data and web
sessions, self-hosted on the same VPS via Ansible.

---

## Phase 1 — Upgrade `postgres_adapter.rs`

**File:** `src/adapters/postgres_adapter.rs`

### 1a. Replace `RefCell<Client>` with `r2d2` connection pool

The current adapter uses `RefCell<Client>` which is `!Sync` — it cannot be
shared via `Arc<dyn DataPort + Send + Sync>` as the web server requires.

- Add `r2d2_postgres` dependency (Cargo.toml)
- Replace `RefCell<Client>` with `Pool<PostgresConnectionManager<NoTls>>`
- Add `pool_size` config (default 4, read from `[postgres] pool_size`)
- All methods call `self.pool.get()` instead of `self.client.borrow_mut()`

**DataPort trait bound verification:**

After the pool change, verify `PostgresAdapter` compiles under
`Arc<dyn DataPort + Send + Sync>`. The pool type is `Arc` internally and
fully `Send + Sync`, so this should work. Add a compile-time assertion:

```rust
fn _assert_sync() {
    fn is_sync<T: Sync>() {}
    is_sync::<PostgresAdapter>();
}
```

**Connection pool error handling:**

`pool.get()` returns `Result<PooledConnection, r2d2::Error>`. Handle via:

```rust
fn get_conn(&self) -> Result<PooledConnection<...>, DataError> {
    self.pool.get().map_err(|e| DataError::Connection(e.to_string()))
}
```

All adapter methods propagate `DataError::Connection` upward — the web layer
should convert this to HTTP 503 (Service Unavailable). Do not panic on pool
exhaustion; let the caller decide.

### 1b. Add `initialize_schema()` method

Mirror `SqliteAdapter::initialize_schema()` but with PostgreSQL DDL. The
schema **must match the existing dump** (`samtrader.sql`, pg_dump v16.9,
~13 GB / 228M rows) so the dump can be loaded directly via `psql <`.

**Actual dump schema:**

```sql
CREATE TABLE IF NOT EXISTS public.ohlcv (
    code CHARACTER VARYING NOT NULL,
    exchange CHARACTER VARYING NOT NULL,
    date TIMESTAMP WITH TIME ZONE NOT NULL,
    open NUMERIC NOT NULL,
    high NUMERIC NOT NULL,
    low NUMERIC NOT NULL,
    close NUMERIC NOT NULL,
    volume INTEGER NOT NULL
);

-- Primary key added separately (matches pg_dump output)
ALTER TABLE ONLY public.ohlcv
    ADD CONSTRAINT ohlcv_pkey PRIMARY KEY (code, exchange, date);

-- Index names match the dump exactly
CREATE INDEX IF NOT EXISTS ohlcv_code_exchange_idx ON public.ohlcv USING btree (code, exchange);
CREATE INDEX IF NOT EXISTS ohlcv_date_idx ON public.ohlcv USING btree (date);
```

**Why `NUMERIC`/`INTEGER` not `DOUBLE PRECISION`/`BIGINT`:** The dump
uses `NUMERIC` for prices and `INTEGER` for volume. The existing adapter
already casts at query time (`open::double precision`, `volume::bigint`)
so Rust gets the types it expects. Keep the dump-native types to avoid
any conversion during the 228M-row import.

**`TIMESTAMPTZ` handling:** Keep the current `DateTime<Utc>` conversion
logic (NaiveDate -> DateTime<Utc> for queries, DateTime<Utc> -> NaiveDate
for results). The dump timestamps use `+10` offset (AEST) but PostgreSQL
normalizes to UTC on storage, which the adapter handles correctly.

### 1c. Add `insert_bars()` method

Match SQLite adapter's batch insert with transactions. Use
`INSERT ... ON CONFLICT (code, exchange, date) DO UPDATE SET ...` (upsert).

### 1d. Add unit tests

- `from_config_missing_connection_string` (already exists)
- Integration tests can use a real pg via `#[ignore]` attribute

### 1e. Verify `FromSql`/`ToSql` implementations

The existing adapter uses `postgres` crate's `FromSql`/`ToSql` traits for
type conversion. The pool change doesn't affect this — `PooledConnection`
deref's to `Client`, so query execution is unchanged.

**Sanity check before merge:**

```bash
cargo check --features postgres
```

Ensure no type mismatch errors. The existing casts in SQL
(`::double precision`, `::bigint`) handle the `NUMERIC`/`INTEGER` → Rust
`f64`/`i64` conversion at the database level, so Rust sees the types it
expects.

---

**Cargo.toml changes:**
```toml
r2d2_postgres = { version = "0.14", optional = true }

[features]
postgres = ["dep:postgres", "dep:r2d2", "dep:r2d2_postgres"]
```

(`r2d2` is already a dep under `sqlite` -- just share it.)

---

## Phase 2 — Web session store

**File:** `src/adapters/web/mod.rs`

Use `tower-sessions-memory-store` for sessions since sessions are ephemeral
and don't need persistence across restarts. This avoids adding sqlx
entirely. Sessions just expire on server restart, which is acceptable for a
single-user backtesting app.

Upgrading to pg-backed sessions later is a one-file change (swap
`MemoryStore` for `SqlxStore<Postgres>` in `build_router()` + add sqlx
dep).

**Feature flag naming:**

Rename `web` → `web-sqlite` and add `web-postgres`. Deprecate bare `web`
(with a comment in Cargo.toml pointing to `web-sqlite`).

**Cargo.toml changes (memory store approach):**
```toml
# Under [features]
web-sqlite = ["sqlite", "dep:axum", "dep:tokio", "dep:askama", "dep:askama_axum",
    "dep:axum-login", "dep:tower-sessions", "dep:tower",
    "dep:tower-http", "dep:argon2", "dep:serde", "dep:rand",
    "dep:time", "dep:hex", "dep:tower-sessions-rusqlite-store", "dep:tokio-rusqlite"]
web-postgres = ["postgres", "dep:axum", "dep:tokio", "dep:askama", "dep:askama_axum",
    "dep:axum-login", "dep:tower-sessions", "dep:tower",
    "dep:tower-http", "dep:argon2", "dep:serde", "dep:rand",
    "dep:time", "dep:hex"]
# web = ["web-sqlite"]  # deprecated alias (optional)
```

---

## Phase 3 — CLI changes

**File:** `src/cli.rs`

### 3a. Update `run_serve()` to support postgres

Add `#[cfg(feature = "web-postgres")]` and `#[cfg(feature = "web-sqlite")]`
blocks that construct `PostgresAdapter::from_config()` or
`SqliteAdapter::from_config()` respectively.

### 3b. Update `run_migrate()` to support postgres

Extend the `Migrate` command to accept `--postgres <connstring>` as an
alternative to `--sqlite <path>`. The pg path calls
`PostgresAdapter::initialize_schema()`.

```rust
Migrate {
    #[arg(long, group = "db")]
    sqlite: Option<PathBuf>,
    #[arg(long, group = "db")]
    postgres: Option<String>,
},
```

---

## Phase 4 — Ansible: PostgreSQL role

**New directory:** `deploy/ansible/roles/postgresql/`

### Prerequisite: Ansible collection

The role uses `community.postgresql` modules. Install before running:

```bash
ansible-galaxy collection install community.postgresql
```

Or add to `deploy/ansible/requirements.yml`:

```yaml
collections:
  - name: community.postgresql
    version: ">=3.0.0"
```

### `tasks/main.yml`

```yaml
- name: Install PostgreSQL
  apt:
    name:
      - postgresql
      - postgresql-contrib
    state: present
    update_cache: yes

- name: Ensure PostgreSQL is running
  systemd:
    name: postgresql
    state: started
    enabled: yes

- name: Create samtrader database user
  become_user: postgres
  community.postgresql.postgresql_user:
    name: "{{ samtrader_pg_user }}"
    password: "{{ samtrader_pg_password }}"
    state: present

- name: Create samtrader database
  become_user: postgres
  community.postgresql.postgresql_db:
    name: "{{ samtrader_pg_database }}"
    owner: "{{ samtrader_pg_user }}"
    state: present

- name: Configure pg_hba for local connections
  become_user: postgres
  community.postgresql.postgresql_pg_hba:
    dest: /etc/postgresql/{{ pg_version }}/main/pg_hba.conf
    contype: local
    databases: "{{ samtrader_pg_database }}"
    users: "{{ samtrader_pg_user }}"
    method: scram-sha-256
  notify: Restart PostgreSQL
```

### `handlers/main.yml`

```yaml
- name: Restart PostgreSQL
  systemd:
    name: postgresql
    state: restarted
```

### `defaults/main.yml`

```yaml
samtrader_pg_user: samtrader
samtrader_pg_database: samtrader
pg_version: "16"
```

---

## Phase 5 — Update config template and deployment

### 5a. Config template

**File:** `deploy/ansible/roles/samtrader/templates/config.ini.j2`

```ini
[postgres]
connection_string = postgresql://{{ samtrader_pg_user }}:{{ samtrader_pg_password }}@localhost:5432/{{ samtrader_pg_database }}
pool_size = 4

[server]
host = 127.0.0.1
port = {{ samtrader_web_port }}

[auth]
username = {{ samtrader_auth_username }}
{% if samtrader_auth_password is defined %}
password = {{ samtrader_auth_password }}
{% endif %}
session_secret = {{ samtrader_session_secret }}
```

### 5b. Playbook

**File:** `deploy/ansible/playbook.yml`

```yaml
- hosts: samtrader
  become: yes
  roles:
    - base
    - postgresql    # NEW -- before samtrader
    - caddy
    - samtrader
    - backup
```

### 5c. Samtrader role

Update the binary build/deploy to use `--features "web-postgres"` instead
of `--features web`.

### 5d. Migrate task

Update the migration task in the samtrader role to use:
```
/usr/local/bin/samtrader migrate --postgres "postgresql://..."
```

---

## Phase 6 — Update backup role

**File:** `deploy/ansible/roles/backup/tasks/main.yml`

Replace `sqlite3 .backup` with `pg_dump`:

```bash
PGPASSWORD="${PG_PASSWORD}" pg_dump -h localhost -U samtrader -Fc samtrader \
    > "${BACKUP_DIR}/samtrader-${TIMESTAMP}.dump"
gzip "${BACKUP_FILE}"
```

The rotation script stays the same (just change the glob pattern from
`samtrader-*.db.gz` to `samtrader-*.dump.gz`).

---

## Phase 7 — Data migration (one-time)

### About the dump

The source file is `samtrader.sql` — a plain-text `pg_dump` (v16.9) of
the `ohlcv` table. It's ~13 GB / 228M rows covering US and AU exchanges
from 1999–present. It uses `COPY ... FROM stdin` format (tab-separated),
which is the fastest way for PostgreSQL to ingest bulk data. The dump
includes the `CREATE TABLE`, `ALTER TABLE ... ADD CONSTRAINT`, and
`CREATE INDEX` statements, so the table does not need to exist beforehand
— loading the dump creates everything.

The dump's `OWNER TO sam` statements will produce harmless warnings when
loaded as the `samtrader` user — they can be ignored.

### Ansible tasks

The dump is hosted on Dropbox. Pass the download URL via
`samtrader_sql_dump_url` at deploy time. Ansible downloads it directly
to the server (avoids shipping 13 GB through the control machine).

```yaml
- name: Create import directory
  file:
    path: /var/lib/samtrader/import
    state: directory
    owner: samtrader
    group: samtrader
    mode: '0750'

- name: Check if dump already loaded
  stat:
    path: /var/lib/samtrader/import/.dump_loaded
  register: dump_loaded_marker

- name: Download SQL dump from Dropbox
  get_url:
    url: "{{ samtrader_sql_dump_url }}"
    dest: /var/lib/samtrader/import/samtrader.sql
    owner: samtrader
    group: samtrader
    mode: '0640'
    timeout: 600
  when:
    - samtrader_sql_dump_url is defined
    - not dump_loaded_marker.stat.exists
  register: dump_download

- name: Load SQL dump into database
  become_user: samtrader
  shell: >
    PGPASSWORD={{ samtrader_pg_password }}
    psql -h localhost -U {{ samtrader_pg_user }} -d {{ samtrader_pg_database }}
    < /var/lib/samtrader/import/samtrader.sql
  when:
    - not dump_loaded_marker.stat.exists
    - dump_download is changed
  register: dump_result

- name: Mark dump as loaded
  file:
    path: /var/lib/samtrader/import/.dump_loaded
    state: touch
    owner: samtrader
  when: dump_result is changed

- name: Clean up dump file to reclaim disk space
  file:
    path: /var/lib/samtrader/import/samtrader.sql
    state: absent
  when: dump_result is changed
```

### Usage

```bash
# Automated — pass the Dropbox direct-download URL at deploy time:
ansible-playbook playbook.yml \
  -e samtrader_sql_dump_url="https://www.dropbox.com/scl/fi/9vmctpymdgsasom3a3vvv/samtrader.sql?rlkey=rdm44agxhgl9u3143snrsqx6h&st=2w9nwpio&dl=1"

# Manual — if the file is already on the server:
ssh sam@server "sudo -u samtrader psql -h localhost -U samtrader samtrader \
    < /var/lib/samtrader/import/samtrader.sql"
```

No conversion needed — the pg dump loads directly. The 13 GB import will
take a while; the `COPY` format is as fast as it gets for PostgreSQL
bulk loading.

---

## Files to create/modify

| File | Action |
|------|--------|
| `src/adapters/postgres_adapter.rs` | Major rewrite (pool, schema init, insert_bars) |
| `src/adapters/web/mod.rs` | Conditional session store (memory for pg) |
| `src/cli.rs` | Extend migrate + serve for postgres |
| `Cargo.toml` | Add r2d2_postgres, rename web→web-sqlite, add web-postgres feature |
| `deploy/ansible/roles/postgresql/` | New role (tasks, handlers, defaults) |
| `deploy/ansible/roles/samtrader/templates/config.ini.j2` | Switch to [postgres] section |
| `deploy/ansible/roles/backup/tasks/main.yml` | pg_dump instead of sqlite3 |
| `deploy/ansible/playbook.yml` | Add postgresql role |
| `deploy/ansible/inventory.ini` | Add pg vars |
| `config.ini.example` | Update with postgres config |

---

## Verification

1. **Unit tests:** `cargo test --features postgres` (adapter tests pass)
   - Also run `cargo test --features web-postgres` for web layer
2. **Local integration:** Start local pg, run `samtrader migrate --postgres "postgresql://..."`, load dump, run `samtrader serve -c config.ini`
3. **Web sessions:** Login/logout cycle works with memory session store
4. **Ansible dry-run:** `ansible-playbook --check --diff playbook.yml`
5. **Full deploy:** Deploy to VPS, verify service starts, load data, run backtest through web UI
6. **Backup:** Run backup script manually, verify dump is valid with `pg_restore --list`
