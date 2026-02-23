//! PostgreSQL data adapter (TRD Section 2.2).

use crate::domain::error::SamtraderError;
use crate::domain::ohlcv::OhlcvBar;
use crate::ports::config_port::ConfigPort;
use crate::ports::data_port::DataPort;
use chrono::NaiveDate;
use postgres::{Client, NoTls};
use std::cell::RefCell;

pub struct PostgresAdapter {
    client: RefCell<Client>,
}

impl PostgresAdapter {
    pub fn from_config(config: &dyn ConfigPort) -> Result<Self, SamtraderError> {
        let connection_string = config
            .get_string("postgres", "connection_string")
            .ok_or_else(|| SamtraderError::ConfigMissing {
                section: "postgres".into(),
                key: "connection_string".into(),
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
        let query = "SELECT code, exchange, date, open, high, low, close, volume \
                     FROM public.ohlcv \
                     WHERE code = $1 AND exchange = $2 AND date >= $3 AND date <= $4 \
                     ORDER BY date ASC";

        let rows = self
            .client
            .borrow_mut()
            .query(query, &[&code, &exchange, &start_date, &end_date])
            .map_err(|e| SamtraderError::DatabaseQuery {
                reason: e.to_string(),
            })?;

        let bars: Vec<OhlcvBar> = rows
            .into_iter()
            .map(|row| OhlcvBar {
                code: row.get(0),
                exchange: row.get(1),
                date: row.get(2),
                open: row.get(3),
                high: row.get(4),
                low: row.get(5),
                close: row.get(6),
                volume: row.get(7),
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
}
