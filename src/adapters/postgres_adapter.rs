//! PostgreSQL data adapter (TRD Section 2.2).

use crate::domain::error::SamtraderError;
use crate::domain::ohlcv::OhlcvBar;
use crate::ports::config_port::ConfigPort;
use crate::ports::data_port::DataPort;
use chrono::{DateTime, NaiveDate, NaiveTime, Utc};
use postgres::types::ToSql;
use postgres::NoTls;
use r2d2::Pool;
use r2d2_postgres::PostgresConnectionManager;
use std::time::Duration;

pub struct PostgresAdapter {
    pool: Pool<PostgresConnectionManager<NoTls>>,
}

fn _assert_sync() {
    fn is_sync<T: Sync>() {}
    is_sync::<PostgresAdapter>();
}

impl PostgresAdapter {
    pub fn from_connection_string(connection_string: &str) -> Result<Self, SamtraderError> {
        let pg_config = connection_string
            .parse()
            .map_err(|e| SamtraderError::Database {
                reason: format!("Invalid connection string: {}", e),
            })?;
        let manager = PostgresConnectionManager::new(pg_config, NoTls);
        let pool = Pool::builder()
            .max_size(4)
            .connection_timeout(Duration::from_secs(120))
            .build(manager)
            .map_err(|e: r2d2::Error| SamtraderError::Database {
                reason: e.to_string(),
            })?;

        Ok(Self { pool })
    }

    pub fn from_config(config: &dyn ConfigPort) -> Result<Self, SamtraderError> {
        let connection_string = config
            .get_string("postgres", "connection_string")
            .or_else(|| config.get_string("database", "conninfo"))
            .ok_or_else(|| SamtraderError::ConfigMissing {
                section: "database".into(),
                key: "conninfo".into(),
            })?;

        let pool_size = config.get_int("postgres", "pool_size", 4) as u32;

        let pg_config = connection_string
            .parse()
            .map_err(|e| SamtraderError::Database {
                reason: format!("Invalid connection string: {}", e),
            })?;
        let manager = PostgresConnectionManager::new(pg_config, NoTls);
        let pool = Pool::builder()
            .max_size(pool_size)
            .connection_timeout(Duration::from_secs(120))
            .build(manager)
            .map_err(|e: r2d2::Error| SamtraderError::Database {
                reason: e.to_string(),
            })?;

        Ok(Self { pool })
    }

    fn get_conn(
        &self,
    ) -> Result<r2d2::PooledConnection<PostgresConnectionManager<NoTls>>, SamtraderError> {
        self.pool.get().map_err(|e| SamtraderError::Database {
            reason: e.to_string(),
        })
    }

    pub fn initialize_schema(&self) -> Result<(), SamtraderError> {
        let mut conn = self.get_conn()?;

        let create_table = "CREATE TABLE IF NOT EXISTS public.ohlcv (
            code CHARACTER VARYING NOT NULL,
            exchange CHARACTER VARYING NOT NULL,
            date TIMESTAMP WITH TIME ZONE NOT NULL,
            open NUMERIC NOT NULL,
            high NUMERIC NOT NULL,
            low NUMERIC NOT NULL,
            close NUMERIC NOT NULL,
            volume INTEGER NOT NULL,
            PRIMARY KEY (code, exchange, date)
        )";

        conn.execute(create_table, &[])
            .map_err(|e| SamtraderError::DatabaseQuery {
                reason: e.to_string(),
            })?;

        let create_idx_code_exchange =
            "CREATE INDEX IF NOT EXISTS ohlcv_code_exchange_idx ON public.ohlcv USING btree (code, exchange)";
        conn.execute(create_idx_code_exchange, &[])
            .map_err(|e| SamtraderError::DatabaseQuery {
                reason: e.to_string(),
            })?;

        let create_idx_date =
            "CREATE INDEX IF NOT EXISTS ohlcv_date_idx ON public.ohlcv USING btree (date)";
        conn.execute(create_idx_date, &[])
            .map_err(|e| SamtraderError::DatabaseQuery {
                reason: e.to_string(),
            })?;

        Ok(())
    }

    pub fn insert_bars(&self, bars: &[OhlcvBar]) -> Result<(), SamtraderError> {
        if bars.is_empty() {
            return Ok(());
        }

        let mut conn = self.get_conn()?;
        let mut tx = conn
            .transaction()
            .map_err(|e| SamtraderError::DatabaseQuery {
                reason: e.to_string(),
            })?;

        let mut param_idx = 1;
        let mut value_rows: Vec<String> = Vec::new();
        let mut dts: Vec<DateTime<Utc>> = Vec::with_capacity(bars.len());
        let mut params: Vec<&(dyn ToSql + Sync)> = Vec::new();

        for bar in bars {
            let dt: DateTime<Utc> = bar.date.and_hms_opt(0, 0, 0).unwrap().and_utc();
            dts.push(dt);
        }

        for (i, bar) in bars.iter().enumerate() {
            value_rows.push(format!(
                "(${},${},${},${},${},${},${},${})",
                param_idx,
                param_idx + 1,
                param_idx + 2,
                param_idx + 3,
                param_idx + 4,
                param_idx + 5,
                param_idx + 6,
                param_idx + 7
            ));
            param_idx += 8;

            params.push(&bar.code);
            params.push(&bar.exchange);
            params.push(&dts[i]);
            params.push(&bar.open);
            params.push(&bar.high);
            params.push(&bar.low);
            params.push(&bar.close);
            params.push(&bar.volume);
        }

        let query = format!(
            "INSERT INTO public.ohlcv (code, exchange, date, open, high, low, close, volume) \
             VALUES {} \
             ON CONFLICT (code, exchange, date) DO UPDATE SET \
             open = EXCLUDED.open, \
             high = EXCLUDED.high, \
             low = EXCLUDED.low, \
             close = EXCLUDED.close, \
             volume = EXCLUDED.volume",
            value_rows.join(",")
        );

        tx.execute(&query, &params[..])
            .map_err(|e| SamtraderError::DatabaseQuery {
                reason: e.to_string(),
            })?;

        tx.commit().map_err(|e| SamtraderError::DatabaseQuery {
            reason: e.to_string(),
        })?;

        Ok(())
    }
}

impl DataPort for PostgresAdapter {
    fn fetch_ohlcv(
        &self,
        code: &str,
        exchange: &str,
        start_date: NaiveDate,
        end_date: NaiveDate,
    ) -> Result<Vec<OhlcvBar>, SamtraderError> {
        let mut conn = self.get_conn()?;

        let start_dt: DateTime<Utc> = start_date.and_time(NaiveTime::MIN).and_utc();
        let end_dt: DateTime<Utc> = end_date.and_hms_opt(23, 59, 59).unwrap().and_utc();

        let query = "SELECT code, exchange, date, \
                            open::double precision, high::double precision, \
                            low::double precision, close::double precision, \
                            volume::bigint \
                     FROM public.ohlcv \
                     WHERE code = $1 AND exchange = $2 AND date >= $3 AND date <= $4 \
                     ORDER BY date ASC";

        let params: &[&(dyn ToSql + Sync)] = &[&code, &exchange, &start_dt, &end_dt];
        let rows = conn
            .query(query, params)
            .map_err(|e| SamtraderError::DatabaseQuery {
                reason: e.to_string(),
            })?;

        let bars: Vec<OhlcvBar> = rows
            .into_iter()
            .map(|row| {
                let dt: DateTime<Utc> = row.get(2);
                OhlcvBar {
                    code: row.get(0),
                    exchange: row.get(1),
                    date: dt.naive_utc().date(),
                    open: row.get(3),
                    high: row.get(4),
                    low: row.get(5),
                    close: row.get(6),
                    volume: row.get(7),
                }
            })
            .collect();

        Ok(bars)
    }

    fn list_symbols(&self, exchange: &str) -> Result<Vec<String>, SamtraderError> {
        let mut conn = self.get_conn()?;

        let query = "SELECT DISTINCT code FROM public.ohlcv WHERE exchange = $1 ORDER BY code";

        let rows = conn
            .query(query, &[&exchange])
            .map_err(|e| SamtraderError::DatabaseQuery {
                reason: e.to_string(),
            })?;

        let symbols: Vec<String> = rows.into_iter().map(|row| row.get(0)).collect();

        Ok(symbols)
    }

    fn get_data_range(
        &self,
        code: &str,
        exchange: &str,
    ) -> Result<Option<(NaiveDate, NaiveDate, usize)>, SamtraderError> {
        let mut conn = self.get_conn()?;

        let query = "SELECT MIN(date), MAX(date), COUNT(*) FROM public.ohlcv WHERE code = $1 AND exchange = $2";

        let rows =
            conn.query(query, &[&code, &exchange])
                .map_err(|e| SamtraderError::DatabaseQuery {
                    reason: e.to_string(),
                })?;

        if rows.is_empty() {
            return Ok(None);
        }

        let row = &rows[0];
        let min_dt: Option<DateTime<Utc>> = row.get(0);
        let max_dt: Option<DateTime<Utc>> = row.get(1);
        let count: i64 = row.get(2);

        match (min_dt, max_dt) {
            (Some(min), Some(max)) if count > 0 => Ok(Some((
                min.naive_utc().date(),
                max.naive_utc().date(),
                count as usize,
            ))),
            _ => Ok(None),
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    struct EmptyConfig;

    impl ConfigPort for EmptyConfig {
        fn get_string(&self, _section: &str, _key: &str) -> Option<String> {
            None
        }
        fn get_int(&self, _section: &str, _key: &str, default: i64) -> i64 {
            default
        }
        fn get_double(&self, _section: &str, _key: &str, default: f64) -> f64 {
            default
        }
        fn get_bool(&self, _section: &str, _key: &str, default: bool) -> bool {
            default
        }
    }

    #[test]
    fn from_config_missing_connection_string() {
        let config = EmptyConfig;
        let result = PostgresAdapter::from_config(&config);
        match result {
            Err(SamtraderError::ConfigMissing { section, key }) => {
                assert_eq!(section, "database");
                assert_eq!(key, "conninfo");
            }
            Err(other) => panic!("expected ConfigMissing, got: {other}"),
            Ok(_) => panic!("expected error, got Ok"),
        }
    }

    struct MockConfig {
        postgres_conn: Option<String>,
        database_conninfo: Option<String>,
        pool_size: Option<i64>,
    }

    impl MockConfig {
        fn new() -> Self {
            Self {
                postgres_conn: None,
                database_conninfo: None,
                pool_size: None,
            }
        }
    }

    impl ConfigPort for MockConfig {
        fn get_string(&self, section: &str, key: &str) -> Option<String> {
            match (section, key) {
                ("postgres", "connection_string") => self.postgres_conn.clone(),
                ("database", "conninfo") => self.database_conninfo.clone(),
                _ => None,
            }
        }
        fn get_int(&self, section: &str, key: &str, default: i64) -> i64 {
            match (section, key) {
                ("postgres", "pool_size") => self.pool_size.unwrap_or(default),
                _ => default,
            }
        }
        fn get_double(&self, _section: &str, _key: &str, default: f64) -> f64 {
            default
        }
        fn get_bool(&self, _section: &str, _key: &str, default: bool) -> bool {
            default
        }
    }

    #[test]
    fn from_config_uses_postgres_connection_string_when_set() {
        let mut config = MockConfig::new();
        config.postgres_conn = Some("invalid://connection".to_string());
        config.database_conninfo = Some("fallback://should-not-be-used".to_string());
        let result = PostgresAdapter::from_config(&config);
        match result {
            Err(SamtraderError::Database { reason }) => {
                assert!(
                    reason.contains("Invalid connection string"),
                    "expected 'Invalid connection string' error, got: {reason}"
                );
            }
            Err(other) => panic!("expected Database error, got: {other}"),
            Ok(_) => panic!("expected error for invalid connection string, got Ok"),
        }
    }

    #[test]
    fn from_config_uses_database_conninfo_as_fallback() {
        let mut config = MockConfig::new();
        config.postgres_conn = None;
        config.database_conninfo = Some("invalid://fallback".to_string());
        let result = PostgresAdapter::from_config(&config);
        match result {
            Err(SamtraderError::Database { reason }) => {
                assert!(
                    reason.contains("Invalid connection string"),
                    "expected 'Invalid connection string' error, got: {reason}"
                );
            }
            Err(other) => panic!("expected Database error, got: {other}"),
            Ok(_) => panic!("expected error for invalid connection string, got Ok"),
        }
    }

    #[test]
    fn from_config_default_pool_size_is_four() {
        let config = MockConfig::new();
        assert_eq!(
            config.get_int("postgres", "pool_size", 4),
            4,
            "default pool_size should be 4"
        );
    }

    #[test]
    fn from_config_custom_pool_size() {
        let mut config = MockConfig::new();
        config.pool_size = Some(10);
        assert_eq!(
            config.get_int("postgres", "pool_size", 4),
            10,
            "custom pool_size should override default"
        );
    }

    fn get_test_adapter() -> Option<PostgresAdapter> {
        let conn_str = std::env::var("SAMTRADER_PG_TEST_CONN").ok()?;
        let pg_config: postgres::Config = conn_str.parse().ok()?;
        let manager = PostgresConnectionManager::new(pg_config, NoTls);
        let pool = Pool::builder().max_size(1).build(manager).ok()?;
        Some(PostgresAdapter { pool })
    }

    #[test]
    #[ignore]
    fn initialize_schema_is_idempotent() {
        let adapter = get_test_adapter().expect("Set SAMTRADER_PG_TEST_CONN to run this test");
        adapter.initialize_schema().unwrap();
        adapter.initialize_schema().unwrap();
    }

    #[test]
    #[ignore]
    fn postgres_insert_bars_and_fetch() {
        let adapter = get_test_adapter().expect("Set SAMTRADER_PG_TEST_CONN to run this test");
        adapter.initialize_schema().unwrap();

        let mut conn = adapter.get_conn().unwrap();
        conn.execute("DELETE FROM public.ohlcv", &[]).unwrap();
        drop(conn);

        let bars = vec![
            OhlcvBar {
                code: "BHP".to_string(),
                exchange: "ASX".to_string(),
                date: NaiveDate::from_ymd_opt(2024, 1, 1).unwrap(),
                open: 100.0,
                high: 101.0,
                low: 99.0,
                close: 100.5,
                volume: 1000,
            },
            OhlcvBar {
                code: "BHP".to_string(),
                exchange: "ASX".to_string(),
                date: NaiveDate::from_ymd_opt(2024, 1, 2).unwrap(),
                open: 100.5,
                high: 102.0,
                low: 100.0,
                close: 101.5,
                volume: 1500,
            },
        ];

        adapter.insert_bars(&bars).unwrap();

        let fetched = adapter
            .fetch_ohlcv(
                "BHP",
                "ASX",
                NaiveDate::from_ymd_opt(2024, 1, 1).unwrap(),
                NaiveDate::from_ymd_opt(2024, 1, 2).unwrap(),
            )
            .unwrap();

        assert_eq!(fetched.len(), 2);
        assert_eq!(fetched[0].code, "BHP");
        assert_eq!(fetched[1].close, 101.5);
    }

    #[test]
    #[ignore]
    fn postgres_insert_bars_upsert() {
        let adapter = get_test_adapter().expect("Set SAMTRADER_PG_TEST_CONN to run this test");
        adapter.initialize_schema().unwrap();

        let mut conn = adapter.get_conn().unwrap();
        conn.execute("DELETE FROM public.ohlcv", &[]).unwrap();
        drop(conn);

        let bars = vec![OhlcvBar {
            code: "BHP".to_string(),
            exchange: "ASX".to_string(),
            date: NaiveDate::from_ymd_opt(2024, 1, 1).unwrap(),
            open: 100.0,
            high: 101.0,
            low: 99.0,
            close: 100.5,
            volume: 1000,
        }];

        adapter.insert_bars(&bars).unwrap();

        let updated_bars = vec![OhlcvBar {
            code: "BHP".to_string(),
            exchange: "ASX".to_string(),
            date: NaiveDate::from_ymd_opt(2024, 1, 1).unwrap(),
            open: 105.0,
            high: 106.0,
            low: 104.0,
            close: 105.5,
            volume: 2000,
        }];

        adapter.insert_bars(&updated_bars).unwrap();

        let fetched = adapter
            .fetch_ohlcv(
                "BHP",
                "ASX",
                NaiveDate::from_ymd_opt(2024, 1, 1).unwrap(),
                NaiveDate::from_ymd_opt(2024, 1, 1).unwrap(),
            )
            .unwrap();

        assert_eq!(fetched.len(), 1);
        assert_eq!(fetched[0].close, 105.5);
        assert_eq!(fetched[0].volume, 2000);
    }

    #[test]
    fn postgres_insert_bars_empty() {
        let adapter = get_test_adapter();
        if let Some(a) = adapter {
            a.initialize_schema().unwrap();
            let result = a.insert_bars(&[]);
            assert!(result.is_ok());
        }
    }
}
