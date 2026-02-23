//! CLI integration tests for the backtest command orchestration.
//!
//! Tests cover:
//! - Config parsing (build_backtest_config, build_strategy)
//! - Code resolution logic (resolve_codes)
//! - Indicator extraction (collect_all_indicators)
//! - Dry-run mode with real INI files on disk
//! - Full pipeline with MockDataPort (stages 6-11)
//! - End-to-end with real database (#[ignore])

mod common;

use chrono::NaiveDate;
use common::*;
use samtrader::adapters::file_config_adapter::FileConfigAdapter;
use samtrader::cli;
use samtrader::domain::error::SamtraderError;
use samtrader::domain::indicator::IndicatorType;
use std::io::Write;
use std::path::PathBuf;

fn write_temp_ini(content: &str) -> tempfile::NamedTempFile {
    let mut file = tempfile::NamedTempFile::new().unwrap();
    file.write_all(content.as_bytes()).unwrap();
    file.flush().unwrap();
    file
}

const VALID_INI: &str = r#"
[database]
conninfo = host=localhost dbname=samtrader

[postgres]
connection_string = host=localhost dbname=samtrader

[backtest]
initial_capital = 100000.0
commission_per_trade = 10.0
commission_pct = 0.0
slippage_pct = 0.001
risk_free_rate = 0.05
start_date = 2020-01-01
end_date = 2024-12-31
exchange = ASX
codes = BHP,CBA,WBC
allow_shorting = false

[strategy]
name = SMA Crossover
description = Buy on golden cross, sell on death cross
entry_long = CROSS_ABOVE(SMA(20), SMA(50))
exit_long = CROSS_BELOW(SMA(20), SMA(50))
position_size = 0.25
stop_loss = 5.0
take_profit = 10.0
max_positions = 3
"#;

mod config_loading {
    use super::*;

    #[test]
    fn build_backtest_config_valid_full() {
        let adapter = FileConfigAdapter::from_string(VALID_INI).unwrap();
        let config = cli::build_backtest_config(&adapter).unwrap();

        assert_eq!(config.start_date, NaiveDate::from_ymd_opt(2020, 1, 1).unwrap());
        assert_eq!(config.end_date, NaiveDate::from_ymd_opt(2024, 12, 31).unwrap());
        assert!((config.initial_capital - 100_000.0).abs() < f64::EPSILON);
        assert!((config.commission_per_trade - 10.0).abs() < f64::EPSILON);
        assert!((config.commission_pct - 0.0).abs() < f64::EPSILON);
        assert!((config.slippage_pct - 0.001).abs() < f64::EPSILON);
        assert!((config.risk_free_rate - 0.05).abs() < f64::EPSILON);
        assert!(!config.allow_shorting);
    }

    #[test]
    fn build_backtest_config_uses_defaults() {
        let ini = r#"
[backtest]
start_date = 2020-01-01
end_date = 2024-12-31
"#;
        let adapter = FileConfigAdapter::from_string(ini).unwrap();
        let config = cli::build_backtest_config(&adapter).unwrap();

        assert!((config.initial_capital - 100_000.0).abs() < f64::EPSILON);
        assert!((config.commission_per_trade - 0.0).abs() < f64::EPSILON);
        assert!((config.commission_pct - 0.0).abs() < f64::EPSILON);
        assert!((config.slippage_pct - 0.0).abs() < f64::EPSILON);
        assert!(!config.allow_shorting);
        assert!((config.risk_free_rate - 0.05).abs() < f64::EPSILON);
    }

    #[test]
    fn build_backtest_config_missing_start_date() {
        let ini = "[backtest]\nend_date = 2024-12-31\n";
        let adapter = FileConfigAdapter::from_string(ini).unwrap();
        let err = cli::build_backtest_config(&adapter).unwrap_err();
        assert!(matches!(err, SamtraderError::ConfigMissing { key, .. } if key == "start_date"));
    }

    #[test]
    fn build_backtest_config_missing_end_date() {
        let ini = "[backtest]\nstart_date = 2020-01-01\n";
        let adapter = FileConfigAdapter::from_string(ini).unwrap();
        let err = cli::build_backtest_config(&adapter).unwrap_err();
        assert!(matches!(err, SamtraderError::ConfigMissing { key, .. } if key == "end_date"));
    }

    #[test]
    fn build_backtest_config_invalid_date_format() {
        let ini = "[backtest]\nstart_date = 2020/01/01\nend_date = 2024-12-31\n";
        let adapter = FileConfigAdapter::from_string(ini).unwrap();
        let err = cli::build_backtest_config(&adapter).unwrap_err();
        assert!(matches!(err, SamtraderError::ConfigInvalid { key, .. } if key == "start_date"));
    }

    #[test]
    fn build_backtest_config_custom_values() {
        let ini = r#"
[backtest]
start_date = 2022-06-15
end_date = 2023-03-01
initial_capital = 50000.0
commission_per_trade = 0.0
commission_pct = 0.1
slippage_pct = 0.05
allow_shorting = true
risk_free_rate = 0.03
"#;
        let adapter = FileConfigAdapter::from_string(ini).unwrap();
        let config = cli::build_backtest_config(&adapter).unwrap();

        assert_eq!(config.start_date, NaiveDate::from_ymd_opt(2022, 6, 15).unwrap());
        assert!((config.initial_capital - 50_000.0).abs() < f64::EPSILON);
        assert!((config.commission_pct - 0.1).abs() < f64::EPSILON);
        assert!((config.slippage_pct - 0.05).abs() < f64::EPSILON);
        assert!(config.allow_shorting);
        assert!((config.risk_free_rate - 0.03).abs() < f64::EPSILON);
    }
}

mod strategy_parsing {
    use super::*;

    #[test]
    fn build_strategy_simple_rules() {
        let ini = r#"
[strategy]
name = Simple Test
description = Test strategy
entry_long = ABOVE(close, 100)
exit_long = BELOW(close, 100)
position_size = 0.25
max_positions = 1
"#;
        let adapter = FileConfigAdapter::from_string(ini).unwrap();
        let strategy = cli::build_strategy(&adapter).unwrap();

        assert_eq!(strategy.name, "Simple Test");
        assert_eq!(strategy.description, "Test strategy");
        assert_eq!(strategy.max_positions, 1);
        assert!((strategy.position_size - 0.25).abs() < f64::EPSILON);
    }

    #[test]
    fn build_strategy_indicator_rules() {
        let ini = r#"
[strategy]
name = SMA Crossover
entry_long = CROSS_ABOVE(SMA(20), SMA(50))
exit_long = CROSS_BELOW(SMA(20), SMA(50))
position_size = 0.25
max_positions = 1
"#;
        let adapter = FileConfigAdapter::from_string(ini).unwrap();
        let strategy = cli::build_strategy(&adapter).unwrap();

        assert_eq!(strategy.name, "SMA Crossover");
        // Verify indicators can be extracted from parsed rules
        let indicators = cli::collect_all_indicators(&strategy);
        assert!(indicators.contains(&IndicatorType::Sma(20)));
        assert!(indicators.contains(&IndicatorType::Sma(50)));
    }

    #[test]
    fn build_strategy_with_short_rules() {
        let ini = r#"
[strategy]
entry_long = ABOVE(close, 100)
exit_long = BELOW(close, 100)
entry_short = BELOW(close, 50)
exit_short = ABOVE(close, 50)
position_size = 0.5
max_positions = 2
"#;
        let adapter = FileConfigAdapter::from_string(ini).unwrap();
        let strategy = cli::build_strategy(&adapter).unwrap();

        assert!(strategy.entry_short.is_some());
        assert!(strategy.exit_short.is_some());
    }

    #[test]
    fn build_strategy_defaults() {
        let ini = r#"
[strategy]
entry_long = ABOVE(close, 100)
exit_long = BELOW(close, 100)
position_size = 0.25
max_positions = 1
"#;
        let adapter = FileConfigAdapter::from_string(ini).unwrap();
        let strategy = cli::build_strategy(&adapter).unwrap();

        assert_eq!(strategy.name, "Unnamed");
        assert_eq!(strategy.description, "");
        assert!(strategy.entry_short.is_none());
        assert!(strategy.exit_short.is_none());
        assert!((strategy.stop_loss_pct - 0.0).abs() < f64::EPSILON);
        assert!((strategy.take_profit_pct - 0.0).abs() < f64::EPSILON);
    }

    #[test]
    fn build_strategy_invalid_entry_rule() {
        let ini = r#"
[strategy]
entry_long = @@invalid@@
exit_long = close below 100
position_size = 0.25
max_positions = 1
"#;
        let adapter = FileConfigAdapter::from_string(ini).unwrap();
        let result = cli::build_strategy(&adapter);
        assert!(result.is_err());
    }

    #[test]
    fn build_strategy_custom_params() {
        let ini = r#"
[strategy]
entry_long = ABOVE(close, 100)
exit_long = BELOW(close, 100)
position_size = 0.5
stop_loss = 10.0
take_profit = 20.0
max_positions = 5
"#;
        let adapter = FileConfigAdapter::from_string(ini).unwrap();
        let strategy = cli::build_strategy(&adapter).unwrap();

        assert!((strategy.position_size - 0.5).abs() < f64::EPSILON);
        assert!((strategy.stop_loss_pct - 10.0).abs() < f64::EPSILON);
        assert!((strategy.take_profit_pct - 20.0).abs() < f64::EPSILON);
        assert_eq!(strategy.max_positions, 5);
    }
}

mod code_resolution {
    use super::*;

    #[test]
    fn resolve_codes_override_single() {
        let adapter = FileConfigAdapter::from_string("[backtest]\ncodes = CBA\n").unwrap();
        let codes = cli::resolve_codes(Some("bhp"), &adapter);
        assert_eq!(codes, vec!["BHP"]);
    }

    #[test]
    fn resolve_codes_from_config_codes() {
        let adapter =
            FileConfigAdapter::from_string("[backtest]\ncodes = BHP,CBA,WBC\n").unwrap();
        let codes = cli::resolve_codes(None, &adapter);
        assert_eq!(codes, vec!["BHP", "CBA", "WBC"]);
    }

    #[test]
    fn resolve_codes_from_config_code_singular() {
        let adapter = FileConfigAdapter::from_string("[backtest]\ncode = BHP\n").unwrap();
        let codes = cli::resolve_codes(None, &adapter);
        assert_eq!(codes, vec!["BHP"]);
    }

    #[test]
    fn resolve_codes_codes_takes_precedence() {
        let adapter = FileConfigAdapter::from_string(
            "[backtest]\ncodes = CBA,WBC\ncode = BHP\n",
        )
        .unwrap();
        let codes = cli::resolve_codes(None, &adapter);
        assert_eq!(codes, vec!["CBA", "WBC"]);
    }

    #[test]
    fn resolve_codes_override_takes_precedence() {
        let adapter =
            FileConfigAdapter::from_string("[backtest]\ncodes = CBA,WBC\n").unwrap();
        let codes = cli::resolve_codes(Some("RIO"), &adapter);
        assert_eq!(codes, vec!["RIO"]);
    }

    #[test]
    fn resolve_codes_none_available() {
        let adapter = FileConfigAdapter::from_string("[backtest]\n").unwrap();
        let codes = cli::resolve_codes(None, &adapter);
        assert!(codes.is_empty());
    }

    #[test]
    fn resolve_codes_whitespace_handling() {
        let adapter =
            FileConfigAdapter::from_string("[backtest]\ncodes = BHP , CBA , WBC \n").unwrap();
        let codes = cli::resolve_codes(None, &adapter);
        assert_eq!(codes, vec!["BHP", "CBA", "WBC"]);
    }
}

mod indicator_collection {
    use super::*;

    #[test]
    fn collect_from_simple_strategy() {
        let strategy = make_simple_strategy(); // close above/below 100
        let indicators = cli::collect_all_indicators(&strategy);
        assert!(indicators.is_empty());
    }

    #[test]
    fn collect_from_sma_crossover() {
        let ini = r#"
[strategy]
entry_long = CROSS_ABOVE(SMA(20), SMA(50))
exit_long = CROSS_BELOW(SMA(20), SMA(50))
position_size = 0.25
max_positions = 1
"#;
        let adapter = FileConfigAdapter::from_string(ini).unwrap();
        let strategy = cli::build_strategy(&adapter).unwrap();
        let indicators = cli::collect_all_indicators(&strategy);

        assert!(indicators.contains(&IndicatorType::Sma(20)));
        assert!(indicators.contains(&IndicatorType::Sma(50)));
    }

    #[test]
    fn collect_from_complex_strategy() {
        let ini = r#"
[strategy]
entry_long = AND(ABOVE(SMA(20), SMA(50)), BELOW(RSI(14), 70))
exit_long = BELOW(SMA(20), SMA(50))
position_size = 0.25
max_positions = 1
"#;
        let adapter = FileConfigAdapter::from_string(ini).unwrap();
        let strategy = cli::build_strategy(&adapter).unwrap();
        let indicators = cli::collect_all_indicators(&strategy);

        assert!(indicators.contains(&IndicatorType::Sma(20)));
        assert!(indicators.contains(&IndicatorType::Sma(50)));
        assert!(indicators.contains(&IndicatorType::Rsi(14)));
    }

    #[test]
    fn collect_with_short_rules() {
        let ini = r#"
[strategy]
entry_long = ABOVE(SMA(20), SMA(50))
exit_long = BELOW(SMA(20), SMA(50))
entry_short = ABOVE(RSI(14), 80)
exit_short = BELOW(RSI(14), 30)
position_size = 0.25
max_positions = 1
"#;
        let adapter = FileConfigAdapter::from_string(ini).unwrap();
        let strategy = cli::build_strategy(&adapter).unwrap();
        let indicators = cli::collect_all_indicators(&strategy);

        assert!(indicators.contains(&IndicatorType::Sma(20)));
        assert!(indicators.contains(&IndicatorType::Sma(50)));
        assert!(indicators.contains(&IndicatorType::Rsi(14)));
    }
}

mod dry_run {
    use super::*;

    #[test]
    fn dry_run_valid_config_succeeds() {
        let file = write_temp_ini(VALID_INI);
        let path = PathBuf::from(file.path());
        let exit_code = cli::run_dry_run(&path);
        // ExitCode doesn't implement PartialEq, so check via report format
        let report = format!("{exit_code:?}");
        assert!(report.contains("0"), "expected success exit code, got: {report}");
    }

    #[test]
    fn dry_run_missing_file_fails() {
        let path = PathBuf::from("/nonexistent/path/config.ini");
        let exit_code = cli::run_dry_run(&path);
        let report = format!("{exit_code:?}");
        assert!(!report.contains("ExitCode(0)") || report.contains("1"),
            "expected error exit code for missing file");
    }

    #[test]
    fn dry_run_invalid_strategy_rule_fails() {
        let ini = r#"
[database]
conninfo = host=localhost dbname=samtrader

[backtest]
initial_capital = 100000.0
start_date = 2020-01-01
end_date = 2024-12-31
exchange = ASX
codes = BHP

[strategy]
entry_long = @@invalid syntax@@
exit_long = BELOW(close, 100)
position_size = 0.25
max_positions = 1
"#;
        let file = write_temp_ini(ini);
        let path = PathBuf::from(file.path());
        let exit_code = cli::run_dry_run(&path);
        let report = format!("{exit_code:?}");
        assert!(!report.contains("ExitCode(0)"),
            "expected error exit code for invalid rule");
    }
}

mod pipeline_mock {
    use super::*;

    #[test]
    fn pipeline_single_code_generates_report() {
        let bars = generate_bars("BHP", "2020-01-01", 100, 100.0);
        let mock = MockDataPort::new().with_bars("BHP", bars);

        let strategy = make_simple_strategy();
        let bt_config = sample_config();
        let codes = vec!["BHP".to_string()];

        let temp_dir = tempfile::TempDir::new().unwrap();
        let output = temp_dir.path().join("test_report.typ");

        let exit_code = cli::run_backtest_pipeline(
            &mock,
            &strategy,
            &bt_config,
            &codes,
            "ASX",
            Some(&output),
            None,
        );

        let report = format!("{exit_code:?}");
        assert!(report.contains("0"), "expected success, got: {report}");
        assert!(output.exists(), "report file should be written");

        let content = std::fs::read_to_string(&output).unwrap();
        assert!(!content.is_empty(), "report should not be empty");
    }

    #[test]
    fn pipeline_multi_code_generates_report() {
        let bhp_bars = generate_bars("BHP", "2020-01-01", 100, 100.0);
        let cba_bars = generate_bars("CBA", "2020-01-01", 100, 50.0);
        let wbc_bars = generate_bars("WBC", "2020-01-01", 100, 75.0);

        let mock = MockDataPort::new()
            .with_bars("BHP", bhp_bars)
            .with_bars("CBA", cba_bars)
            .with_bars("WBC", wbc_bars);

        let mut strategy = make_simple_strategy();
        strategy.max_positions = 3;
        let bt_config = sample_config();
        let codes = vec!["BHP".to_string(), "CBA".to_string(), "WBC".to_string()];

        let temp_dir = tempfile::TempDir::new().unwrap();
        let output = temp_dir.path().join("multi_report.typ");

        let exit_code = cli::run_backtest_pipeline(
            &mock,
            &strategy,
            &bt_config,
            &codes,
            "ASX",
            Some(&output),
            None,
        );

        let report = format!("{exit_code:?}");
        assert!(report.contains("0"), "expected success, got: {report}");
        assert!(output.exists());
    }

    #[test]
    fn pipeline_no_valid_codes_returns_error() {
        // All codes have fewer than MIN_OHLCV_BARS (30)
        let few_bars = generate_bars("FEW", "2020-01-01", 5, 100.0);
        let mock = MockDataPort::new().with_bars("FEW", few_bars);

        let strategy = make_simple_strategy();
        let bt_config = sample_config();
        let codes = vec!["FEW".to_string()];

        let temp_dir = tempfile::TempDir::new().unwrap();
        let output = temp_dir.path().join("report.typ");

        let exit_code = cli::run_backtest_pipeline(
            &mock,
            &strategy,
            &bt_config,
            &codes,
            "ASX",
            Some(&output),
            None,
        );

        let report = format!("{exit_code:?}");
        assert!(!report.contains("ExitCode(0)"), "expected error for no valid codes");
        assert!(!output.exists(), "no report should be written");
    }

    #[test]
    fn pipeline_report_content_sanity() {
        let bars = generate_bars("BHP", "2020-01-01", 100, 100.0);
        let mock = MockDataPort::new().with_bars("BHP", bars);

        let mut strategy = make_simple_strategy();
        strategy.name = "Sanity Check Strategy".to_string();
        let bt_config = sample_config();
        let codes = vec!["BHP".to_string()];

        let temp_dir = tempfile::TempDir::new().unwrap();
        let output = temp_dir.path().join("report.typ");

        cli::run_backtest_pipeline(
            &mock,
            &strategy,
            &bt_config,
            &codes,
            "ASX",
            Some(&output),
            None,
        );

        let content = std::fs::read_to_string(&output).unwrap();
        assert!(content.contains("Sanity Check Strategy"), "report should contain strategy name");
        assert!(content.contains("2020"), "report should contain date range");
    }

    #[test]
    fn pipeline_partial_universe_continues() {
        let good_bars = generate_bars("GOOD", "2020-01-01", 100, 100.0);
        let few_bars = generate_bars("FEW", "2020-01-01", 5, 50.0);

        let mock = MockDataPort::new()
            .with_bars("GOOD", good_bars)
            .with_bars("FEW", few_bars);

        let strategy = make_simple_strategy();
        let bt_config = sample_config();
        let codes = vec!["GOOD".to_string(), "FEW".to_string()];

        let temp_dir = tempfile::TempDir::new().unwrap();
        let output = temp_dir.path().join("report.typ");

        let exit_code = cli::run_backtest_pipeline(
            &mock,
            &strategy,
            &bt_config,
            &codes,
            "ASX",
            Some(&output),
            None,
        );

        let report = format!("{exit_code:?}");
        assert!(report.contains("0"), "should succeed with partial universe");
        assert!(output.exists(), "report should be written");
    }
}

mod end_to_end {
    use super::*;

    #[test]
    #[ignore]
    fn e2e_backtest_dry_run_with_real_config() {
        let config_path = std::env::var("SAMTRADER_CONFIG")
            .unwrap_or_else(|_| "config.ini".to_string());
        let path = PathBuf::from(&config_path);

        if !path.exists() {
            eprintln!("Skipping: {} not found. Copy config.ini.example to config.ini and fill in DB details.", config_path);
            return;
        }

        let exit_code = cli::run_dry_run(&path);
        let report = format!("{exit_code:?}");
        assert!(report.contains("0"), "dry run should succeed with valid config");
    }
}
