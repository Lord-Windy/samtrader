//! SQLite data adapter (TRD Section 3.2).

use crate::domain::error::SamtraderError;
use crate::domain::ohlcv::OhlcvBar;
use crate::ports::config_port::ConfigPort;
use crate::ports::data_port::DataPort;
use chrono::NaiveDate;
use r2d2::Pool;
use r2d2_sqlite::SqliteConnectionManager;
use rusqlite::params;

pub struct SqliteAdapter {
    pool: Pool<SqliteConnectionManager>,
}

impl SqliteAdapter {
    pub fn from_config(config: &dyn ConfigPort) -> Result<Self, SamtraderError> {
        let db_path =
            config
                .get_string("sqlite", "path")
                .ok_or_else(|| SamtraderError::ConfigMissing {
                    section: "sqlite".into(),
                    key: "path".into(),
                })?;

        let pool_size = config.get_int("sqlite", "pool_size", 4) as u32;

        let manager = SqliteConnectionManager::file(&db_path);
        let pool =
            Pool::builder()
                .max_size(pool_size)
                .build(manager)
                .map_err(|e: r2d2::Error| SamtraderError::Database {
                    reason: e.to_string(),
                })?;

        Ok(Self { pool })
    }

    pub fn in_memory() -> Result<Self, SamtraderError> {
        let manager = SqliteConnectionManager::memory();
        let pool = Pool::builder()
            .max_size(1)
            .build(manager)
            .map_err(|e: r2d2::Error| SamtraderError::Database {
                reason: e.to_string(),
            })?;

        Ok(Self { pool })
    }

    pub fn initialize_schema(&self) -> Result<(), SamtraderError> {
        let conn = self
            .pool
            .get()
            .map_err(|e: r2d2::Error| SamtraderError::Database {
                reason: e.to_string(),
            })?;

        conn.execute_batch(
            "CREATE TABLE IF NOT EXISTS ohlcv (
                code TEXT NOT NULL,
                exchange TEXT NOT NULL,
                date TEXT NOT NULL,
                open REAL NOT NULL,
                high REAL NOT NULL,
                low REAL NOT NULL,
                close REAL NOT NULL,
                volume INTEGER NOT NULL,
                PRIMARY KEY (code, exchange, date)
            );
            CREATE INDEX IF NOT EXISTS idx_ohlcv_code_exchange ON ohlcv(code, exchange);
            CREATE INDEX IF NOT EXISTS idx_ohlcv_date ON ohlcv(date);",
        )
        .map_err(|e: rusqlite::Error| SamtraderError::DatabaseQuery {
            reason: e.to_string(),
        })?;

        Ok(())
    }

    pub fn insert_bars(&self, bars: &[OhlcvBar]) -> Result<(), SamtraderError> {
        let mut conn = self
            .pool
            .get()
            .map_err(|e: r2d2::Error| SamtraderError::Database {
                reason: e.to_string(),
            })?;

        let tx =
            conn.transaction()
                .map_err(|e: rusqlite::Error| SamtraderError::DatabaseQuery {
                    reason: e.to_string(),
                })?;

        for bar in bars {
            tx.execute(
                "INSERT OR REPLACE INTO ohlcv (code, exchange, date, open, high, low, close, volume)
                 VALUES (?1, ?2, ?3, ?4, ?5, ?6, ?7, ?8)",
                params![
                    bar.code,
                    bar.exchange,
                    bar.date.format("%Y-%m-%d").to_string(),
                    bar.open,
                    bar.high,
                    bar.low,
                    bar.close,
                    bar.volume
                ],
            )
            .map_err(|e: rusqlite::Error| SamtraderError::DatabaseQuery {
                reason: e.to_string(),
            })?;
        }

        tx.commit()
            .map_err(|e: rusqlite::Error| SamtraderError::DatabaseQuery {
                reason: e.to_string(),
            })?;

        Ok(())
    }
}

impl DataPort for SqliteAdapter {
    fn fetch_ohlcv(
        &self,
        code: &str,
        exchange: &str,
        start_date: NaiveDate,
        end_date: NaiveDate,
    ) -> Result<Vec<OhlcvBar>, SamtraderError> {
        let conn = self
            .pool
            .get()
            .map_err(|e: r2d2::Error| SamtraderError::Database {
                reason: e.to_string(),
            })?;

        let start_str = start_date.format("%Y-%m-%d").to_string();
        let end_str = end_date.format("%Y-%m-%d").to_string();

        let query = "SELECT code, exchange, date, open, high, low, close, volume
                     FROM ohlcv
                     WHERE code = ?1 AND exchange = ?2 AND date >= ?3 AND date <= ?4
                     ORDER BY date ASC";

        let mut stmt =
            conn.prepare(query)
                .map_err(|e: rusqlite::Error| SamtraderError::DatabaseQuery {
                    reason: e.to_string(),
                })?;

        let rows = stmt
            .query_map(params![code, exchange, start_str, end_str], |row| {
                let date_str: String = row.get(2)?;
                let date = NaiveDate::parse_from_str(&date_str, "%Y-%m-%d").map_err(|e| {
                    rusqlite::Error::FromSqlConversionFailure(
                        date_str.len(),
                        rusqlite::types::Type::Text,
                        Box::new(e),
                    )
                })?;
                Ok(OhlcvBar {
                    code: row.get(0)?,
                    exchange: row.get(1)?,
                    date,
                    open: row.get(3)?,
                    high: row.get(4)?,
                    low: row.get(5)?,
                    close: row.get(6)?,
                    volume: row.get(7)?,
                })
            })
            .map_err(|e: rusqlite::Error| SamtraderError::DatabaseQuery {
                reason: e.to_string(),
            })?;

        let mut bars = Vec::new();
        for row in rows {
            bars.push(
                row.map_err(|e: rusqlite::Error| SamtraderError::DatabaseQuery {
                    reason: e.to_string(),
                })?,
            );
        }

        Ok(bars)
    }

    fn list_symbols(&self, exchange: &str) -> Result<Vec<String>, SamtraderError> {
        let conn = self
            .pool
            .get()
            .map_err(|e: r2d2::Error| SamtraderError::Database {
                reason: e.to_string(),
            })?;

        let query = "SELECT DISTINCT code FROM ohlcv WHERE exchange = ?1 ORDER BY code";

        let mut stmt =
            conn.prepare(query)
                .map_err(|e: rusqlite::Error| SamtraderError::DatabaseQuery {
                    reason: e.to_string(),
                })?;

        let rows = stmt
            .query_map(params![exchange], |row| row.get(0))
            .map_err(|e: rusqlite::Error| SamtraderError::DatabaseQuery {
                reason: e.to_string(),
            })?;

        let mut symbols = Vec::new();
        for row in rows {
            symbols.push(
                row.map_err(|e: rusqlite::Error| SamtraderError::DatabaseQuery {
                    reason: e.to_string(),
                })?,
            );
        }

        Ok(symbols)
    }

    fn get_data_range(
        &self,
        code: &str,
        exchange: &str,
    ) -> Result<Option<(NaiveDate, NaiveDate, usize)>, SamtraderError> {
        let conn = self
            .pool
            .get()
            .map_err(|e: r2d2::Error| SamtraderError::Database {
                reason: e.to_string(),
            })?;

        let query =
            "SELECT MIN(date), MAX(date), COUNT(*) FROM ohlcv WHERE code = ?1 AND exchange = ?2";

        let result: (Option<String>, Option<String>, i64) = conn
            .query_row(query, params![code, exchange], |row| {
                Ok((row.get(0)?, row.get(1)?, row.get(2)?))
            })
            .map_err(|e: rusqlite::Error| SamtraderError::DatabaseQuery {
                reason: e.to_string(),
            })?;

        match result {
            (Some(min_str), Some(max_str), count) if count > 0 => {
                let min = NaiveDate::parse_from_str(&min_str, "%Y-%m-%d").map_err(
                    |e: chrono::ParseError| SamtraderError::Database {
                        reason: e.to_string(),
                    },
                )?;
                let max = NaiveDate::parse_from_str(&max_str, "%Y-%m-%d").map_err(
                    |e: chrono::ParseError| SamtraderError::Database {
                        reason: e.to_string(),
                    },
                )?;
                Ok(Some((min, max, count as usize)))
            }
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
    fn from_config_missing_path() {
        let config = EmptyConfig;
        let result = SqliteAdapter::from_config(&config);
        match result {
            Err(SamtraderError::ConfigMissing { section, key }) => {
                assert_eq!(section, "sqlite");
                assert_eq!(key, "path");
            }
            Err(other) => panic!("expected ConfigMissing, got: {other}"),
            Ok(_) => panic!("expected error, got Ok"),
        }
    }

    #[test]
    fn in_memory_initialization() {
        let adapter = SqliteAdapter::in_memory().unwrap();
        adapter.initialize_schema().unwrap();
    }

    #[test]
    fn sqlite_fetch_ohlcv_returns_bars() {
        let adapter = SqliteAdapter::in_memory().unwrap();
        adapter.initialize_schema().unwrap();

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
    fn sqlite_list_symbols() {
        let adapter = SqliteAdapter::in_memory().unwrap();
        adapter.initialize_schema().unwrap();

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
                code: "CBA".to_string(),
                exchange: "ASX".to_string(),
                date: NaiveDate::from_ymd_opt(2024, 1, 1).unwrap(),
                open: 150.0,
                high: 151.0,
                low: 149.0,
                close: 150.5,
                volume: 2000,
            },
        ];

        adapter.insert_bars(&bars).unwrap();

        let symbols = adapter.list_symbols("ASX").unwrap();
        assert_eq!(symbols, vec!["BHP", "CBA"]);
    }

    #[test]
    fn sqlite_get_data_range() {
        let adapter = SqliteAdapter::in_memory().unwrap();
        adapter.initialize_schema().unwrap();

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
                date: NaiveDate::from_ymd_opt(2024, 1, 5).unwrap(),
                open: 102.0,
                high: 103.0,
                low: 101.0,
                close: 102.5,
                volume: 1500,
            },
        ];

        adapter.insert_bars(&bars).unwrap();

        let range = adapter.get_data_range("BHP", "ASX").unwrap();
        assert!(range.is_some());
        let (min, max, count) = range.unwrap();
        assert_eq!(min, NaiveDate::from_ymd_opt(2024, 1, 1).unwrap());
        assert_eq!(max, NaiveDate::from_ymd_opt(2024, 1, 5).unwrap());
        assert_eq!(count, 2);
    }

    #[test]
    fn sqlite_get_data_range_no_data() {
        let adapter = SqliteAdapter::in_memory().unwrap();
        adapter.initialize_schema().unwrap();

        let range = adapter.get_data_range("BHP", "ASX").unwrap();
        assert!(range.is_none());
    }
}
