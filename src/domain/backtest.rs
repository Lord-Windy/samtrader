//! Backtest engine and event loop (TRD Section 3.4).
//!
//! BacktestConfig (TRD Section 3.7) defines backtest parameters.

use chrono::NaiveDate;

#[derive(Debug, Clone)]
pub struct BacktestConfig {
    pub start_date: NaiveDate,
    pub end_date: NaiveDate,
    pub initial_capital: f64,
    pub commission_per_trade: f64,
    pub commission_pct: f64,
    pub slippage_pct: f64,
    pub allow_shorting: bool,
    pub risk_free_rate: f64,
}

#[cfg(test)]
mod tests {
    use super::*;

    fn sample_config() -> BacktestConfig {
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

    #[test]
    fn config_fields() {
        let c = sample_config();
        assert_eq!(c.start_date, NaiveDate::from_ymd_opt(2020, 1, 1).unwrap());
        assert_eq!(c.end_date, NaiveDate::from_ymd_opt(2024, 12, 31).unwrap());
        assert!((c.initial_capital - 100_000.0).abs() < f64::EPSILON);
        assert!((c.commission_per_trade - 0.0).abs() < f64::EPSILON);
        assert!((c.commission_pct - 0.0).abs() < f64::EPSILON);
        assert!((c.slippage_pct - 0.0).abs() < f64::EPSILON);
        assert!(!c.allow_shorting);
        assert!((c.risk_free_rate - 0.05).abs() < f64::EPSILON);
    }

    #[test]
    fn config_with_shorting() {
        let c = BacktestConfig {
            allow_shorting: true,
            ..sample_config()
        };
        assert!(c.allow_shorting);
    }

    #[test]
    fn config_with_commission() {
        let c = BacktestConfig {
            commission_per_trade: 10.0,
            commission_pct: 0.1,
            ..sample_config()
        };
        assert!((c.commission_per_trade - 10.0).abs() < f64::EPSILON);
        assert!((c.commission_pct - 0.1).abs() < f64::EPSILON);
    }
}
