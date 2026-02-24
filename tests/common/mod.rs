#![allow(dead_code)]

use chrono::NaiveDate;
use samtrader::domain::backtest::BacktestConfig;
use samtrader::domain::code_data::CodeData;
use samtrader::domain::error::SamtraderError;
pub use samtrader::domain::ohlcv::OhlcvBar;
use samtrader::domain::rule::{Operand, Rule};
use samtrader::domain::strategy::Strategy;
use samtrader::ports::data_port::DataPort;
use std::collections::HashMap;

pub struct MockDataPort {
    pub data: HashMap<String, Vec<OhlcvBar>>,
    pub errors: HashMap<String, String>,
}

impl MockDataPort {
    pub fn new() -> Self {
        Self {
            data: HashMap::new(),
            errors: HashMap::new(),
        }
    }

    pub fn with_bars(mut self, code: &str, bars: Vec<OhlcvBar>) -> Self {
        self.data.insert(code.to_string(), bars);
        self
    }

    pub fn with_error(mut self, code: &str, reason: &str) -> Self {
        self.errors.insert(code.to_string(), reason.to_string());
        self
    }
}

impl DataPort for MockDataPort {
    fn fetch_ohlcv(
        &self,
        code: &str,
        _exchange: &str,
        _start_date: NaiveDate,
        _end_date: NaiveDate,
    ) -> Result<Vec<OhlcvBar>, SamtraderError> {
        if let Some(reason) = self.errors.get(code) {
            return Err(SamtraderError::Database {
                reason: reason.clone(),
            });
        }
        Ok(self.data.get(code).cloned().unwrap_or_default())
    }

    fn list_symbols(&self, _exchange: &str) -> Result<Vec<String>, SamtraderError> {
        Ok(self.data.keys().cloned().collect())
    }

    fn get_data_range(
        &self,
        code: &str,
        _exchange: &str,
    ) -> Result<Option<(NaiveDate, NaiveDate, usize)>, SamtraderError> {
        if let Some(reason) = self.errors.get(code) {
            return Err(SamtraderError::Database {
                reason: reason.clone(),
            });
        }
        match self.data.get(code) {
            Some(bars) if !bars.is_empty() => {
                let min = bars.iter().map(|b| b.date).min().unwrap();
                let max = bars.iter().map(|b| b.date).max().unwrap();
                Ok(Some((min, max, bars.len())))
            }
            _ => Ok(None),
        }
    }
}

pub fn make_bar(code: &str, date: &str, close: f64) -> OhlcvBar {
    OhlcvBar {
        code: code.to_string(),
        exchange: "ASX".to_string(),
        date: NaiveDate::parse_from_str(date, "%Y-%m-%d").unwrap(),
        open: close - 1.0,
        high: close + 1.0,
        low: close - 2.0,
        close,
        volume: 1000,
    }
}

pub fn make_code_data(code: &str, bars: Vec<OhlcvBar>) -> CodeData {
    CodeData::new(code.to_string(), "ASX".to_string(), bars)
}

pub fn make_simple_strategy() -> Strategy {
    Strategy {
        name: "Test".into(),
        description: "Test strategy".into(),
        entry_long: Rule::Above {
            left: Operand::Close,
            right: Operand::Constant(100.0),
        },
        exit_long: Rule::Below {
            left: Operand::Close,
            right: Operand::Constant(100.0),
        },
        entry_short: None,
        exit_short: None,
        position_size: 0.25,
        stop_loss_pct: 0.0,
        take_profit_pct: 0.0,
        max_positions: 1,
    }
}

pub fn sample_config() -> BacktestConfig {
    BacktestConfig {
        start_date: NaiveDate::from_ymd_opt(2020, 1, 1).unwrap(),
        end_date: NaiveDate::from_ymd_opt(2024, 12, 31).unwrap(),
        initial_capital: 100_000.0,
        commission_per_trade: 0.0,
        commission_pct: 0.0,
        slippage_pct: 0.0,
        allow_shorting: false,
        risk_free_rate: 0.05,
    }
}

pub fn date(y: i32, m: u32, d: u32) -> NaiveDate {
    NaiveDate::from_ymd_opt(y, m, d).unwrap()
}

pub fn generate_bars(
    code: &str,
    start_date: &str,
    count: usize,
    start_price: f64,
) -> Vec<OhlcvBar> {
    let start = NaiveDate::parse_from_str(start_date, "%Y-%m-%d").unwrap();
    (0..count)
        .map(|i| OhlcvBar {
            code: code.to_string(),
            exchange: "ASX".to_string(),
            date: start + chrono::Duration::days(i as i64),
            open: start_price + i as f64,
            high: start_price + i as f64 + 1.0,
            low: start_price + i as f64 - 1.0,
            close: start_price + i as f64,
            volume: 1000,
        })
        .collect()
}
