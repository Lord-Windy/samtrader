//! Configuration validation (TRD §6.2).
//!
//! Validates all config fields before backtest runs.

use crate::domain::error::SamtraderError;
use crate::ports::config_port::ConfigPort;
use chrono::NaiveDate;

pub fn validate_backtest_config(config: &dyn ConfigPort) -> Result<(), SamtraderError> {
    validate_initial_capital(config)?;
    validate_commission(config)?;
    validate_slippage(config)?;
    validate_risk_free_rate(config)?;
    validate_dates(config)?;
    validate_exchange(config)?;
    validate_codes(config)?;
    Ok(())
}

pub fn validate_strategy_config(config: &dyn ConfigPort) -> Result<(), SamtraderError> {
    validate_position_size(config)?;
    validate_stop_loss(config)?;
    validate_take_profit(config)?;
    validate_max_positions(config)?;
    validate_entry_exit_rules(config)?;
    Ok(())
}

fn validate_initial_capital(config: &dyn ConfigPort) -> Result<(), SamtraderError> {
    let value = config.get_double("backtest", "initial_capital", 0.0);
    if value <= 0.0 {
        return Err(SamtraderError::ConfigInvalid {
            section: "backtest".to_string(),
            key: "initial_capital".to_string(),
            reason: "initial_capital must be positive".to_string(),
        });
    }
    Ok(())
}

fn validate_commission(config: &dyn ConfigPort) -> Result<(), SamtraderError> {
    let per_trade = config.get_double("backtest", "commission_per_trade", 0.0);
    if per_trade < 0.0 {
        return Err(SamtraderError::ConfigInvalid {
            section: "backtest".to_string(),
            key: "commission_per_trade".to_string(),
            reason: "commission_per_trade must be non-negative".to_string(),
        });
    }
    let pct = config.get_double("backtest", "commission_pct", 0.0);
    if pct < 0.0 {
        return Err(SamtraderError::ConfigInvalid {
            section: "backtest".to_string(),
            key: "commission_pct".to_string(),
            reason: "commission_pct must be non-negative".to_string(),
        });
    }
    Ok(())
}

fn validate_slippage(config: &dyn ConfigPort) -> Result<(), SamtraderError> {
    let value = config.get_double("backtest", "slippage_pct", 0.0);
    if value < 0.0 {
        return Err(SamtraderError::ConfigInvalid {
            section: "backtest".to_string(),
            key: "slippage_pct".to_string(),
            reason: "slippage_pct must be non-negative".to_string(),
        });
    }
    Ok(())
}

fn validate_risk_free_rate(config: &dyn ConfigPort) -> Result<(), SamtraderError> {
    let value = config.get_double("backtest", "risk_free_rate", 0.0);
    if value < 0.0 || value >= 1.0 {
        return Err(SamtraderError::ConfigInvalid {
            section: "backtest".to_string(),
            key: "risk_free_rate".to_string(),
            reason: "risk_free_rate must be between 0 and 1".to_string(),
        });
    }
    Ok(())
}

fn validate_dates(config: &dyn ConfigPort) -> Result<(), SamtraderError> {
    let start_str = config.get_string("backtest", "start_date");
    let end_str = config.get_string("backtest", "end_date");

    let start_date = parse_date(start_str.as_deref(), "start_date")?;
    let end_date = parse_date(end_str.as_deref(), "end_date")?;

    if start_date >= end_date {
        return Err(SamtraderError::ConfigInvalid {
            section: "backtest".to_string(),
            key: "start_date".to_string(),
            reason: "start_date must be before end_date".to_string(),
        });
    }
    Ok(())
}

fn parse_date(value: Option<&str>, field: &str) -> Result<NaiveDate, SamtraderError> {
    match value {
        None => Err(SamtraderError::ConfigMissing {
            section: "backtest".to_string(),
            key: field.to_string(),
        }),
        Some(s) => {
            NaiveDate::parse_from_str(s, "%Y-%m-%d").map_err(|_| SamtraderError::ConfigInvalid {
                section: "backtest".to_string(),
                key: field.to_string(),
                reason: format!("invalid {} format, expected YYYY-MM-DD", field),
            })
        }
    }
}

fn validate_exchange(config: &dyn ConfigPort) -> Result<(), SamtraderError> {
    match config.get_string("backtest", "exchange") {
        Some(s) if !s.trim().is_empty() => Ok(()),
        _ => Err(SamtraderError::ConfigMissing {
            section: "backtest".to_string(),
            key: "exchange".to_string(),
        }),
    }
}

fn validate_codes(config: &dyn ConfigPort) -> Result<(), SamtraderError> {
    let codes = config.get_string("backtest", "codes");
    let code = config.get_string("backtest", "code");

    match (codes, code) {
        (Some(c), _) if !c.trim().is_empty() => Ok(()),
        (None, Some(c)) if !c.trim().is_empty() => Ok(()),
        _ => Err(SamtraderError::ConfigMissing {
            section: "backtest".to_string(),
            key: "code".to_string(),
        }),
    }
}

fn validate_position_size(config: &dyn ConfigPort) -> Result<(), SamtraderError> {
    let value = config.get_double("strategy", "position_size", 0.0);
    if value <= 0.0 || value > 1.0 {
        return Err(SamtraderError::ConfigInvalid {
            section: "strategy".to_string(),
            key: "position_size".to_string(),
            reason: "position_size must be between 0 and 1".to_string(),
        });
    }
    Ok(())
}

fn validate_stop_loss(config: &dyn ConfigPort) -> Result<(), SamtraderError> {
    let value = config.get_double("strategy", "stop_loss", 0.0);
    if value < 0.0 {
        return Err(SamtraderError::ConfigInvalid {
            section: "strategy".to_string(),
            key: "stop_loss".to_string(),
            reason: "stop_loss must be non-negative".to_string(),
        });
    }
    Ok(())
}

fn validate_take_profit(config: &dyn ConfigPort) -> Result<(), SamtraderError> {
    let value = config.get_double("strategy", "take_profit", 0.0);
    if value < 0.0 {
        return Err(SamtraderError::ConfigInvalid {
            section: "strategy".to_string(),
            key: "take_profit".to_string(),
            reason: "take_profit must be non-negative".to_string(),
        });
    }
    Ok(())
}

fn validate_max_positions(config: &dyn ConfigPort) -> Result<(), SamtraderError> {
    let value = config.get_int("strategy", "max_positions", 0);
    if value < 1 {
        return Err(SamtraderError::ConfigInvalid {
            section: "strategy".to_string(),
            key: "max_positions".to_string(),
            reason: "max_positions must be at least 1".to_string(),
        });
    }
    Ok(())
}

fn validate_entry_exit_rules(config: &dyn ConfigPort) -> Result<(), SamtraderError> {
    match config.get_string("strategy", "entry_long") {
        Some(s) if !s.trim().is_empty() => {}
        _ => {
            return Err(SamtraderError::ConfigMissing {
                section: "strategy".to_string(),
                key: "entry_long".to_string(),
            })
        }
    }

    match config.get_string("strategy", "exit_long") {
        Some(s) if !s.trim().is_empty() => {}
        _ => {
            return Err(SamtraderError::ConfigMissing {
                section: "strategy".to_string(),
                key: "exit_long".to_string(),
            })
        }
    }

    Ok(())
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::adapters::file_config_adapter::FileConfigAdapter;

    fn make_config(content: &str) -> FileConfigAdapter {
        FileConfigAdapter::from_string(content).unwrap()
    }

    #[test]
    fn valid_backtest_config_passes() {
        let config = make_config(
            r#"
[backtest]
initial_capital = 100000.0
commission_per_trade = 10.0
commission_pct = 0.1
slippage_pct = 0.05
risk_free_rate = 0.05
start_date = 2020-01-01
end_date = 2024-12-31
exchange = ASX
code = CBA
"#,
        );
        assert!(validate_backtest_config(&config).is_ok());
    }

    #[test]
    fn initial_capital_must_be_positive() {
        let config = make_config("[backtest]\ninitial_capital = -100\nstart_date = 2020-01-01\nend_date = 2024-12-31\nexchange = ASX\ncode = CBA\n");
        let err = validate_backtest_config(&config).unwrap_err();
        assert!(
            matches!(err, SamtraderError::ConfigInvalid { key, .. } if key == "initial_capital")
        );
    }

    #[test]
    fn initial_capital_zero_fails() {
        let config = make_config("[backtest]\ninitial_capital = 0\nstart_date = 2020-01-01\nend_date = 2024-12-31\nexchange = ASX\ncode = CBA\n");
        let err = validate_backtest_config(&config).unwrap_err();
        assert!(
            matches!(err, SamtraderError::ConfigInvalid { key, .. } if key == "initial_capital")
        );
    }

    #[test]
    fn commission_per_trade_negative_fails() {
        let config = make_config("[backtest]\ninitial_capital = 100\ncommission_per_trade = -5\nstart_date = 2020-01-01\nend_date = 2024-12-31\nexchange = ASX\ncode = CBA\n");
        let err = validate_backtest_config(&config).unwrap_err();
        assert!(
            matches!(err, SamtraderError::ConfigInvalid { key, .. } if key == "commission_per_trade")
        );
    }

    #[test]
    fn commission_pct_negative_fails() {
        let config = make_config("[backtest]\ninitial_capital = 100\ncommission_pct = -0.1\nstart_date = 2020-01-01\nend_date = 2024-12-31\nexchange = ASX\ncode = CBA\n");
        let err = validate_backtest_config(&config).unwrap_err();
        assert!(
            matches!(err, SamtraderError::ConfigInvalid { key, .. } if key == "commission_pct")
        );
    }

    #[test]
    fn slippage_negative_fails() {
        let config = make_config("[backtest]\ninitial_capital = 100\nslippage_pct = -0.01\nstart_date = 2020-01-01\nend_date = 2024-12-31\nexchange = ASX\ncode = CBA\n");
        let err = validate_backtest_config(&config).unwrap_err();
        assert!(matches!(err, SamtraderError::ConfigInvalid { key, .. } if key == "slippage_pct"));
    }

    #[test]
    fn risk_free_rate_out_of_range_fails() {
        let config = make_config("[backtest]\ninitial_capital = 100\nrisk_free_rate = 1.5\nstart_date = 2020-01-01\nend_date = 2024-12-31\nexchange = ASX\ncode = CBA\n");
        let err = validate_backtest_config(&config).unwrap_err();
        assert!(
            matches!(err, SamtraderError::ConfigInvalid { key, .. } if key == "risk_free_rate")
        );
    }

    #[test]
    fn risk_free_rate_negative_fails() {
        let config = make_config("[backtest]\ninitial_capital = 100\nrisk_free_rate = -0.05\nstart_date = 2020-01-01\nend_date = 2024-12-31\nexchange = ASX\ncode = CBA\n");
        let err = validate_backtest_config(&config).unwrap_err();
        assert!(
            matches!(err, SamtraderError::ConfigInvalid { key, .. } if key == "risk_free_rate")
        );
    }

    #[test]
    fn invalid_start_date_format_fails() {
        let config = make_config("[backtest]\ninitial_capital = 100\nstart_date = 2020/01/01\nend_date = 2024-12-31\nexchange = ASX\ncode = CBA\n");
        let err = validate_backtest_config(&config).unwrap_err();
        assert!(matches!(err, SamtraderError::ConfigInvalid { key, .. } if key == "start_date"));
    }

    #[test]
    fn missing_end_date_fails() {
        let config = make_config("[backtest]\ninitial_capital = 100\nstart_date = 2020-01-01\nexchange = ASX\ncode = CBA\n");
        let err = validate_backtest_config(&config).unwrap_err();
        assert!(matches!(err, SamtraderError::ConfigMissing { key, .. } if key == "end_date"));
    }

    #[test]
    fn start_date_after_end_date_fails() {
        let config = make_config("[backtest]\ninitial_capital = 100\nstart_date = 2024-12-31\nend_date = 2020-01-01\nexchange = ASX\ncode = CBA\n");
        let err = validate_backtest_config(&config).unwrap_err();
        assert!(matches!(err, SamtraderError::ConfigInvalid { key, .. } if key == "start_date"));
    }

    #[test]
    fn missing_exchange_fails() {
        let config = make_config("[backtest]\ninitial_capital = 100\nstart_date = 2020-01-01\nend_date = 2024-12-31\ncode = CBA\n");
        let err = validate_backtest_config(&config).unwrap_err();
        assert!(matches!(err, SamtraderError::ConfigMissing { key, .. } if key == "exchange"));
    }

    #[test]
    fn missing_code_fails() {
        let config = make_config("[backtest]\ninitial_capital = 100\nstart_date = 2020-01-01\nend_date = 2024-12-31\nexchange = ASX\n");
        let err = validate_backtest_config(&config).unwrap_err();
        assert!(matches!(err, SamtraderError::ConfigMissing { key, .. } if key == "code"));
    }

    #[test]
    fn codes_field_accepted() {
        let config = make_config("[backtest]\ninitial_capital = 100\nstart_date = 2020-01-01\nend_date = 2024-12-31\nexchange = ASX\ncodes = CBA,BHP,WBC\n");
        assert!(validate_backtest_config(&config).is_ok());
    }

    #[test]
    fn valid_strategy_config_passes() {
        let config = make_config(
            r#"
[strategy]
position_size = 0.25
stop_loss = 0.0
take_profit = 0.0
max_positions = 1
entry_long = CROSS_ABOVE(SMA(20), SMA(50))
exit_long = CROSS_BELOW(SMA(20), SMA(50))
"#,
        );
        assert!(validate_strategy_config(&config).is_ok());
    }

    #[test]
    fn position_size_zero_fails() {
        let config = make_config(
            "[strategy]\nposition_size = 0\nmax_positions = 1\nentry_long = foo\nexit_long = bar\n",
        );
        let err = validate_strategy_config(&config).unwrap_err();
        assert!(matches!(err, SamtraderError::ConfigInvalid { key, .. } if key == "position_size"));
    }

    #[test]
    fn position_size_above_one_fails() {
        let config = make_config("[strategy]\nposition_size = 1.5\nmax_positions = 1\nentry_long = foo\nexit_long = bar\n");
        let err = validate_strategy_config(&config).unwrap_err();
        assert!(matches!(err, SamtraderError::ConfigInvalid { key, .. } if key == "position_size"));
    }

    #[test]
    fn stop_loss_negative_fails() {
        let config = make_config("[strategy]\nposition_size = 0.25\nstop_loss = -5\nmax_positions = 1\nentry_long = foo\nexit_long = bar\n");
        let err = validate_strategy_config(&config).unwrap_err();
        assert!(matches!(err, SamtraderError::ConfigInvalid { key, .. } if key == "stop_loss"));
    }

    #[test]
    fn take_profit_negative_fails() {
        let config = make_config("[strategy]\nposition_size = 0.25\ntake_profit = -10\nmax_positions = 1\nentry_long = foo\nexit_long = bar\n");
        let err = validate_strategy_config(&config).unwrap_err();
        assert!(matches!(err, SamtraderError::ConfigInvalid { key, .. } if key == "take_profit"));
    }

    #[test]
    fn max_positions_zero_fails() {
        let config = make_config("[strategy]\nposition_size = 0.25\nmax_positions = 0\nentry_long = foo\nexit_long = bar\n");
        let err = validate_strategy_config(&config).unwrap_err();
        assert!(matches!(err, SamtraderError::ConfigInvalid { key, .. } if key == "max_positions"));
    }

    #[test]
    fn missing_entry_long_fails() {
        let config =
            make_config("[strategy]\nposition_size = 0.25\nmax_positions = 1\nexit_long = bar\n");
        let err = validate_strategy_config(&config).unwrap_err();
        assert!(matches!(err, SamtraderError::ConfigMissing { key, .. } if key == "entry_long"));
    }

    #[test]
    fn missing_exit_long_fails() {
        let config =
            make_config("[strategy]\nposition_size = 0.25\nmax_positions = 1\nentry_long = foo\n");
        let err = validate_strategy_config(&config).unwrap_err();
        assert!(matches!(err, SamtraderError::ConfigMissing { key, .. } if key == "exit_long"));
    }
}
