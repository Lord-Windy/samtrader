//! PostgreSQL data adapter (TRD Section 2.2).

use crate::domain::error::SamtraderError;
use crate::domain::ohlcv::OhlcvBar;
use crate::ports::config_port::ConfigPort;
use crate::ports::data_port::DataPort;
use chrono::{DateTime, NaiveDate, NaiveTime, Utc};
use postgres::types::ToSql;
use postgres::{Client, NoTls};
use std::cell::RefCell;

pub struct PostgresAdapter {
    client: RefCell<Client>,
}

impl PostgresAdapter {
    pub fn from_config(config: &dyn ConfigPort) -> Result<Self, SamtraderError> {
        // Try [postgres] connection_string first, fall back to [database] conninfo
        let connection_string = config
            .get_string("postgres", "connection_string")
            .or_else(|| config.get_string("database", "conninfo"))
            .ok_or_else(|| SamtraderError::ConfigMissing {
                section: "database".into(),
                key: "conninfo".into(),
            })?;

        let client =
            Client::connect(&connection_string, NoTls).map_err(|e| SamtraderError::Database {
                reason: e.to_string(),
            })?;

        Ok(Self {
            client: RefCell::new(client),
        })
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
        // Convert NaiveDate to DateTime<Utc> for timestamptz columns
        let start_dt: DateTime<Utc> =
            start_date.and_time(NaiveTime::MIN).and_utc();
        let end_dt: DateTime<Utc> =
            end_date.and_hms_opt(23, 59, 59).unwrap().and_utc();

        let query = "SELECT code, exchange, date, \
                            open::double precision, high::double precision, \
                            low::double precision, close::double precision, \
                            volume::bigint \
                     FROM public.ohlcv \
                     WHERE code = $1 AND exchange = $2 AND date >= $3 AND date <= $4 \
                     ORDER BY date ASC";

        let params: &[&(dyn ToSql + Sync)] = &[&code, &exchange, &start_dt, &end_dt];
        let rows = self
            .client
            .borrow_mut()
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
        let query = "SELECT DISTINCT code FROM public.ohlcv WHERE exchange = $1 ORDER BY code";

        let rows = self
            .client
            .borrow_mut()
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
        let query = "SELECT MIN(date), MAX(date), COUNT(*) FROM public.ohlcv WHERE code = $1 AND exchange = $2";

        let rows = self
            .client
            .borrow_mut()
            .query(query, &[&code, &exchange])
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
}
