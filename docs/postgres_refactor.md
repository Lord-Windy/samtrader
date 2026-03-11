# PostgreSQL Migration Plan

## Context

SQLite is the current production database but importing a PostgreSQL dump is
painful due to syntax and index incompatibilities. The project already has a
`postgres_adapter.rs` with working query logic. With more historical data
coming from a paid API, PostgreSQL is the natural choice: it handles
concurrent reads and writes better and eliminates the conversion step
entirely.

The goal of this effort is to make PostgreSQL the preferred production
database for OHLCV data and to make the web stack compatible with a
PostgreSQL-backed deployment on the same VPS via Ansible.

This is not a full removal of SQLite. SQLite remains supported for local use,
development, and as a fallback deployment path during the transition.

---

## Scope

This document covers:

- Updating the PostgreSQL adapter so it is usable behind the same trait and
  concurrency boundaries as the SQLite adapter
- Adding PostgreSQL-aware CLI and web wiring
- Updating deployment automation to install and configure PostgreSQL
- Supporting one-time bulk data import from the existing `samtrader.sql` dump
- Updating backup procedures for PostgreSQL deployments

This document does not require the application to automatically migrate data
from an existing SQLite database into PostgreSQL.

---

## Non-goals

- Removing SQLite support from the codebase
- Implementing persistent PostgreSQL-backed web sessions in this phase
- Building an automatic SQLite-to-PostgreSQL conversion pipeline
- Guaranteeing zero-downtime migration for the first production cutover
- Reworking the OHLCV schema beyond what is required to match the existing
  dump and adapter expectations

---

## Target outcome

After this work:

- Production deploys can run with PostgreSQL as the primary OHLCV database
- The web server can run against a `PostgresAdapter` behind
  `Arc<dyn DataPort + Send + Sync>`
- Web sessions continue to work under PostgreSQL deployments using an
  in-memory session store
- SQLite remains available through separate feature flags for local/dev and
  rollback scenarios
- Deploy automation can provision PostgreSQL, initialize an empty schema, load
  the bulk dump, and back the database up

---

## Migration strategy

This is a staged dual-backend transition.

- PostgreSQL becomes the preferred production backend
- SQLite remains supported for local use and rollback safety
- Feature flags and deployment config should make the backend explicit rather
  than implicitly assuming one database
- Existing SQLite deploys should keep working until production cutover is
  complete

That means code and deployment changes should be additive and explicit. Avoid
any change that silently breaks current SQLite workflows.

---

## Phase 1 — Upgrade `postgres_adapter.rs`

**File:** `src/adapters/postgres_adapter.rs`

### 1a. Replace `RefCell<Client>` with `r2d2` connection pool

The current adapter uses `RefCell<Client>` which is `!Sync`, so it cannot be
shared via `Arc<dyn DataPort + Send + Sync>` as the web server requires.

- Add `r2d2_postgres` dependency in `Cargo.toml`
- Replace `RefCell<Client>` with `Pool<PostgresConnectionManager<NoTls>>`
- Add `pool_size` config with default `4`, read from `[postgres] pool_size`
- All methods call `self.pool.get()` instead of `self.client.borrow_mut()`

**DataPort trait bound verification:**

After the pool change, verify `PostgresAdapter` compiles under
`Arc<dyn DataPort + Send + Sync>`. The pool type is internally `Send + Sync`,
so this should compile cleanly. Add a compile-time assertion:

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

All adapter methods should propagate `DataError::Connection` upward. The web
layer should translate this to HTTP 503 where appropriate. Do not panic on
pool exhaustion or temporary connection failures.

### 1b. Add `initialize_schema()` method

Mirror `SqliteAdapter::initialize_schema()` but with PostgreSQL DDL.

This method is for:

- Fresh empty PostgreSQL installs
- Local development and test setup
- The `samtrader migrate --postgres ...` command when the target database is
  empty

This method is not responsible for creating the schema before loading the full
`samtrader.sql` dump in production. The dump already contains the required
`CREATE TABLE`, `ALTER TABLE`, and `CREATE INDEX` statements.

Implementation requirements:

- The schema must match the existing dump so code-level initialization and
  dump-based initialization create compatible tables
- The method must be idempotent and safe to run on an already-initialized
  database
- The application should not silently mutate a non-matching production schema;
  fail fast if a later compatibility check is added and detects drift

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

ALTER TABLE ONLY public.ohlcv
    ADD CONSTRAINT ohlcv_pkey PRIMARY KEY (code, exchange, date);

CREATE INDEX IF NOT EXISTS ohlcv_code_exchange_idx ON public.ohlcv USING btree (code, exchange);
CREATE INDEX IF NOT EXISTS ohlcv_date_idx ON public.ohlcv USING btree (date);
```

**Why `NUMERIC` and `INTEGER` instead of `DOUBLE PRECISION` and `BIGINT`:**

The dump uses `NUMERIC` for prices and `INTEGER` for volume. The existing
adapter already casts at query time (`open::double precision`,
`volume::bigint`) so Rust gets the types it expects. Keep the dump-native
types to avoid any conversion during the 228M-row import.

**`TIMESTAMPTZ` handling:**

Keep the current `DateTime<Utc>` conversion logic (`NaiveDate` to
`DateTime<Utc>` for queries, `DateTime<Utc>` to `NaiveDate` for results). The
dump timestamps use a `+10` offset in source text, but PostgreSQL normalizes
them to UTC on storage.

### 1c. Add `insert_bars()` method

Match the SQLite adapter's batch insert behavior with a transaction. Use:

```sql
INSERT ... ON CONFLICT (code, exchange, date) DO UPDATE SET ...
```

Requirements:

- Inserts and updates are transactional per batch
- Duplicate rows are upserted deterministically
- Behavior should match SQLite semantics closely enough that higher layers do
  not care which adapter is in use

### 1d. Add tests

- Keep `from_config_missing_connection_string`
- Add tests for config parsing including default `pool_size`
- Add adapter-level tests for schema initialization and upsert behavior where
  practical
- Integration tests may use a real PostgreSQL instance and can be marked with
  `#[ignore]`

### 1e. Verify `FromSql` and `ToSql` implementations

The existing adapter uses the `postgres` crate's `FromSql` and `ToSql` traits
for type conversion. The pool change does not alter query logic because
`PooledConnection` dereferences to `Client`.

**Sanity check before merge:**

```bash
cargo check --features postgres
```

Ensure there are no type mismatch errors. The existing SQL casts
(`::double precision`, `::bigint`) should continue handling the
`NUMERIC`/`INTEGER` to Rust `f64`/`i64` conversion at the database level.

---

## Cargo.toml changes

```toml
r2d2_postgres = { version = "0.14", optional = true }

[features]
postgres = ["dep:postgres", "dep:r2d2", "dep:r2d2_postgres"]
```

`r2d2` is already present under SQLite-related work and should be shared.

---

## Phase 2 — Web session store and feature flags

**File:** `src/adapters/web/mod.rs`

Use `tower-sessions-memory-store` for PostgreSQL-backed web deployments in
this phase.

Rationale:

- Sessions are ephemeral for this single-user backtesting app
- This avoids adding `sqlx` just to support sessions
- This keeps the database migration focused on OHLCV and web compatibility

Implication:

- Sessions are not persisted across restarts for PostgreSQL deployments
- This is acceptable in phase 1, but should be called out in docs and release
  notes

If persistent sessions become necessary later, swapping `MemoryStore` for a
database-backed store should be isolated to the web layer.

### Feature flag naming

Rename `web` to `web-sqlite` and add `web-postgres`.

Compatibility requirements:

- Keep `web` as a temporary alias to `web-sqlite` for one transition period if
  current scripts or docs already use it
- Update CI, build scripts, deployment scripts, and docs to use explicit
  feature names
- Do not remove the alias until all known callers have migrated

**Cargo.toml changes:**

```toml
web-sqlite = ["sqlite", "dep:axum", "dep:tokio", "dep:askama", "dep:askama_axum",
    "dep:axum-login", "dep:tower-sessions", "dep:tower",
    "dep:tower-http", "dep:argon2", "dep:serde", "dep:rand",
    "dep:time", "dep:hex", "dep:tower-sessions-rusqlite-store", "dep:tokio-rusqlite"]
web-postgres = ["postgres", "dep:axum", "dep:tokio", "dep:askama", "dep:askama_axum",
    "dep:axum-login", "dep:tower-sessions", "dep:tower",
    "dep:tower-http", "dep:argon2", "dep:serde", "dep:rand",
    "dep:time", "dep:hex"]
web = ["web-sqlite"]
```

Leave a deprecation comment near `web` in `Cargo.toml` if the project uses
comments there for migration guidance.

---

## Phase 3 — CLI changes

**File:** `src/cli.rs`

### 3a. Update `run_serve()` to support PostgreSQL

Add `#[cfg(feature = "web-postgres")]` and `#[cfg(feature = "web-sqlite")]`
blocks that construct `PostgresAdapter::from_config()` or
`SqliteAdapter::from_config()` respectively.

Requirements:

- Backend selection should be explicit at compile time
- Startup errors should clearly identify missing config sections or invalid
  connection settings
- Existing SQLite behavior should remain unchanged when built with
  `web-sqlite`

### 3b. Update `run_migrate()` to support PostgreSQL

Extend the `Migrate` command to accept `--postgres <connstring>` as an
alternative to `--sqlite <path>`.

The PostgreSQL path should call `PostgresAdapter::initialize_schema()`.

```rust
Migrate {
    #[arg(long, group = "db")]
    sqlite: Option<PathBuf>,
    #[arg(long, group = "db")]
    postgres: Option<String>,
}
```

Clarify command semantics in help text:

- `migrate --postgres` initializes an empty compatible schema
- It does not import `samtrader.sql`
- Bulk import remains an operational step handled separately

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

### Implementation notes

- Verify any Python driver requirements for `community.postgresql` modules on
  the managed host or controller, depending on how the playbook is executed
- Keep PostgreSQL package versioning explicit enough that the configured
  `pg_version` matches the installed service layout
- Ensure auth rules match the actual connection mode used by the app
  (`localhost` TCP vs Unix socket)

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
    contype: host
    address: 127.0.0.1/32
    databases: "{{ samtrader_pg_database }}"
    users: "{{ samtrader_pg_user }}"
    method: scram-sha-256
  notify: Restart PostgreSQL
```

`contype: host` is a better fit if the application uses
`postgresql://...@localhost:5432/...`.

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

Also update `config.ini.example` so both database modes are discoverable.

Recommended approach:

- Keep a `[sqlite]` example for backwards compatibility if SQLite remains
  supported
- Add a `[postgres]` example and note which features/builds expect it
- Avoid making the example imply that both backends are active at once

### 5b. Playbook

**File:** `deploy/ansible/playbook.yml`

```yaml
- hosts: samtrader
  become: yes
  roles:
    - base
    - postgresql
    - caddy
    - samtrader
    - backup
```

### 5c. Samtrader role

Update the binary build and deploy path to use `--features web-postgres`
instead of `--features web` for PostgreSQL deployments.

If the same role can still deploy SQLite builds, make the feature selection a
variable rather than hardcoding one backend.

### 5d. Migrate task

Update the migration task in the samtrader role to use:

```bash
/usr/local/bin/samtrader migrate --postgres "postgresql://..."
```

This task should be safe for empty-database initialization. It should not run
immediately before a full dump import if the dump already includes schema
creation and there is a risk of conflicting object creation order.

---

## Phase 6 — Update backup role

**File:** `deploy/ansible/roles/backup/tasks/main.yml`

Replace SQLite backups with PostgreSQL backups.

Preferred approach:

```bash
PGPASSWORD="${PG_PASSWORD}" pg_dump -h localhost -U samtrader -Fc samtrader \
    > "${BACKUP_DIR}/samtrader-${TIMESTAMP}.dump"
```

Notes:

- `-Fc` already produces a compressed PostgreSQL custom-format archive
- Do not additionally gzip the file unless there is a measured reason to do so
- Restore should use `pg_restore`, not `psql`
- Rotation logic can change the glob from `samtrader-*.db.gz` to
  `samtrader-*.dump`

If you prefer plain SQL backups instead, switch to a `.sql.gz` workflow
explicitly and document restore commands accordingly.

---

## Phase 7 — Data migration (one-time)

### About the dump

The source file is `samtrader.sql`, a plain-text `pg_dump` output of the
`ohlcv` table. It is approximately 13 GB and 228M rows, covering US and AU
exchanges from 1999 to present.

It uses `COPY ... FROM stdin` format, which is the fastest practical ingest
path for PostgreSQL from a plain SQL dump. The dump includes `CREATE TABLE`,
`ALTER TABLE ... ADD CONSTRAINT`, and `CREATE INDEX` statements, so loading
the dump creates the schema.

`OWNER TO sam` statements may produce harmless warnings when loaded as the
`samtrader` user unless ownership statements are rewritten. These warnings may
be ignored if the resulting objects are owned correctly for application use.

### Operational constraints

Before running the import:

- Ensure sufficient free disk space for the downloaded dump, PostgreSQL data
  files, indexes, and temporary working space
- Expect the import to take substantial time; treat it as a maintenance
  operation rather than a routine deploy step
- Plan to stop the application or keep it out of service until the import and
  first verification queries complete
- Decide ahead of time whether a failed import will be retried on the same
  database or by recreating the database from scratch

For the first production cutover, the simplest rollback is to keep the SQLite
deployment path available and only switch traffic after PostgreSQL has been
loaded and verified.

### Ansible tasks

The dump is hosted on Dropbox. Pass the download URL via
`samtrader_sql_dump_url` at deploy time. Ansible downloads it directly to the
server to avoid routing 13 GB through the control machine.

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

### Import behavior requirements

- The import should run only once unless the marker is removed intentionally
- A partial or failed import should not create a false success marker
- If retries are needed, the operator should recreate or clean the database
  intentionally rather than layering a second import attempt onto an unknown
  state

### Usage

```bash
# Automated - pass the Dropbox direct-download URL at deploy time:
ansible-playbook playbook.yml \
  -e samtrader_sql_dump_url="https://www.dropbox.com/scl/fi/9vmctpymdgsasom3a3vvv/samtrader.sql?rlkey=rdm44agxhgl9u3143snrsqx6h&st=2w9nwpio&dl=1"

# Manual - if the file is already on the server:
ssh sam@server "sudo -u samtrader psql -h localhost -U samtrader samtrader \
    < /var/lib/samtrader/import/samtrader.sql"
```

No conversion is needed. The `COPY` format is already appropriate for fast
bulk loading into PostgreSQL.

---

## Compatibility and rollback

### Existing SQLite users

- Existing SQLite configuration should continue to work when building with
  `web-sqlite`
- Any renamed feature flag should preserve a transition alias where practical
- Documentation should clearly state which build/config combinations are valid

### Rollback plan

If PostgreSQL deployment fails after code rollout but before cutover:

- Rebuild or redeploy with the SQLite feature path
- Restore previous SQLite-backed config if needed
- Treat in-memory PostgreSQL web sessions as disposable
- Leave PostgreSQL data intact for later debugging unless there is an explicit
  cleanup task

This is sufficient for the first migration because SQLite support remains in
the codebase.

---

## Files to create or modify

| File | Action |
|------|--------|
| `src/adapters/postgres_adapter.rs` | Major rewrite (pool, schema init, insert_bars) |
| `src/adapters/web/mod.rs` | Conditional session store and backend-specific wiring |
| `src/cli.rs` | Extend migrate and serve for PostgreSQL |
| `Cargo.toml` | Add `r2d2_postgres`, rename `web` to `web-sqlite`, add `web-postgres`, keep transition alias |
| `deploy/ansible/roles/postgresql/` | New role (tasks, handlers, defaults) |
| `deploy/ansible/roles/samtrader/templates/config.ini.j2` | Add PostgreSQL config template |
| `deploy/ansible/roles/backup/tasks/main.yml` | `pg_dump`-based backups |
| `deploy/ansible/playbook.yml` | Add PostgreSQL role |
| `deploy/ansible/inventory.ini` | Add PostgreSQL vars |
| `config.ini.example` | Show SQLite and PostgreSQL examples |
| CI/build scripts/docs | Update explicit feature names and migration guidance |

---

## Acceptance criteria

The work is complete when all of the following are true:

- `PostgresAdapter` compiles and is usable behind `Arc<dyn DataPort + Send + Sync>`
- PostgreSQL schema initialization is idempotent and compatible with the dump
- Batch insert and upsert behavior works correctly in PostgreSQL
- SQLite and PostgreSQL adapters return equivalent results for the same test
  fixture data on core read paths
- `samtrader serve` works in both SQLite and PostgreSQL builds using the
  intended feature flags
- Login and logout work in PostgreSQL web builds using the in-memory session
  store
- Ansible can provision PostgreSQL and deploy the app with explicit backend
  selection
- Backup and restore commands are documented and verified for PostgreSQL

---

## Verification

1. `cargo test --features postgres`
2. `cargo test --features web-postgres`
3. `cargo test --features web-sqlite`
4. Compare SQLite and PostgreSQL adapter results against the same fixture data
   for representative queries
5. Start local PostgreSQL, run `samtrader migrate --postgres "postgresql://..."`,
   then run `samtrader serve -c config.ini`
6. Load the dump into a local or staging PostgreSQL instance and verify the app
   can query expected symbols and date ranges
7. Verify login and logout cycle works with the memory session store
8. Run `ansible-playbook --check --diff playbook.yml`
9. Perform a staging or production-like deploy, verify service startup, then
   run at least one end-to-end backtest through the web UI
10. Run a backup manually and verify it with `pg_restore --list`
