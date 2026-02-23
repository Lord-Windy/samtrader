//! Integration tests per TRD §15.2.
//!
//! Tests cover:
//! - Full backtest pipeline with mock data port (no database)
//! - Multi-code backtest with known trades — verify per-code PnL
//! - `max_positions` enforcement across codes
//! - Partial universe validation (some codes skipped, others proceed)
//! - Backward compatibility: single `code` config produces identical results

use chrono::NaiveDate;
use samtrader::domain::backtest::{run_backtest, BacktestConfig};
use samtrader::domain::code_data::{build_unified_timeline, CodeData};
use samtrader::domain::error::SamtraderError;
use samtrader::domain::ohlcv::OhlcvBar;
use samtrader::domain::rule::{Operand, Rule};
use samtrader::domain::strategy::Strategy;
use samtrader::domain::universe::{parse_codes, validate_universe, SkipReason, MIN_OHLCV_BARS};
use samtrader::ports::data_port::DataPort;
use std::collections::HashMap;

struct MockDataPort {
    data: HashMap<String, Vec<OhlcvBar>>,
    errors: HashMap<String, String>,
}

impl MockDataPort {
    fn new() -> Self {
        Self {
            data: HashMap::new(),
            errors: HashMap::new(),
        }
    }

    fn with_bars(mut self, code: &str, bars: Vec<OhlcvBar>) -> Self {
        self.data.insert(code.to_string(), bars);
        self
    }

    fn with_error(mut self, code: &str, reason: &str) -> Self {
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
}

fn make_bar(code: &str, date: &str, close: f64) -> OhlcvBar {
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

fn make_code_data(code: &str, bars: Vec<OhlcvBar>) -> CodeData {
    CodeData::new(code.to_string(), "ASX".to_string(), bars)
}

fn make_simple_strategy() -> Strategy {
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

fn date(y: i32, m: u32, d: u32) -> NaiveDate {
    NaiveDate::from_ymd_opt(y, m, d).unwrap()
}

fn generate_bars(code: &str, start_date: &str, count: usize, start_price: f64) -> Vec<OhlcvBar> {
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

mod full_backtest_pipeline {
    use super::*;

    #[test]
    fn full_pipeline_with_mock_data_port() {
        let bars = vec![
            make_bar("BHP", "2024-01-01", 90.0),
            make_bar("BHP", "2024-01-02", 110.0),
            make_bar("BHP", "2024-01-03", 105.0),
            make_bar("BHP", "2024-01-04", 95.0),
            make_bar("BHP", "2024-01-05", 90.0),
        ];
        let port = MockDataPort::new().with_bars("BHP", bars.clone());

        let ohlcv = port
            .fetch_ohlcv("BHP", "ASX", date(2024, 1, 1), date(2024, 1, 5))
            .unwrap();
        assert_eq!(ohlcv.len(), 5);

        let code_data = make_code_data("BHP", ohlcv);
        let timeline = build_unified_timeline(&[code_data.clone()]);
        let strategy = make_simple_strategy();
        let config = sample_config();

        let result = run_backtest(&[code_data], &timeline, &strategy, &config);

        assert_eq!(result.portfolio.closed_trades.len(), 1);
        let trade = &result.portfolio.closed_trades[0];
        assert_eq!(trade.code, "BHP");
        assert_eq!(trade.entry_date, date(2024, 1, 2));
        assert_eq!(trade.exit_date, date(2024, 1, 4));
    }

    #[test]
    fn pipeline_with_multiple_fetches() {
        let bhp_bars = generate_bars("BHP", "2024-01-01", 50, 100.0);
        let cba_bars = generate_bars("CBA", "2024-01-01", 50, 50.0);

        let port = MockDataPort::new()
            .with_bars("BHP", bhp_bars.clone())
            .with_bars("CBA", cba_bars.clone());

        let bhp_ohlcv = port
            .fetch_ohlcv("BHP", "ASX", date(2024, 1, 1), date(2024, 2, 20))
            .unwrap();
        let cba_ohlcv = port
            .fetch_ohlcv("CBA", "ASX", date(2024, 1, 1), date(2024, 2, 20))
            .unwrap();

        let bhp = make_code_data("BHP", bhp_ohlcv);
        let cba = make_code_data("CBA", cba_ohlcv);
        let timeline = build_unified_timeline(&[bhp.clone(), cba.clone()]);

        assert_eq!(timeline.len(), 50);

        let mut strategy = make_simple_strategy();
        strategy.max_positions = 2;
        let config = sample_config();

        let result = run_backtest(&[bhp, cba], &timeline, &strategy, &config);

        assert!(result.portfolio.equity_curve.len() == 50);
    }
}

mod multi_code_backtest {
    use super::*;

    #[test]
    fn multi_code_with_known_trades_verify_per_code_pnl() {
        let bhp_bars = vec![
            make_bar("BHP", "2024-01-01", 90.0),
            make_bar("BHP", "2024-01-02", 110.0),
            make_bar("BHP", "2024-01-03", 120.0),
            make_bar("BHP", "2024-01-04", 90.0),
        ];
        let cba_bars = vec![
            make_bar("CBA", "2024-01-01", 90.0),
            make_bar("CBA", "2024-01-02", 110.0),
            make_bar("CBA", "2024-01-03", 115.0),
            make_bar("CBA", "2024-01-04", 90.0),
        ];

        let bhp = make_code_data("BHP", bhp_bars);
        let cba = make_code_data("CBA", cba_bars);
        let timeline = build_unified_timeline(&[bhp.clone(), cba.clone()]);

        let mut strategy = make_simple_strategy();
        strategy.max_positions = 2;
        let config = sample_config();

        let result = run_backtest(&[bhp, cba], &timeline, &strategy, &config);

        assert_eq!(result.portfolio.closed_trades.len(), 2);

        let bhp_trade = result
            .portfolio
            .closed_trades
            .iter()
            .find(|t| t.code == "BHP")
            .expect("BHP trade should exist");
        let cba_trade = result
            .portfolio
            .closed_trades
            .iter()
            .find(|t| t.code == "CBA")
            .expect("CBA trade should exist");

        assert!((bhp_trade.entry_price - 110.0).abs() < f64::EPSILON);
        assert!((bhp_trade.exit_price - 90.0).abs() < f64::EPSILON);
        assert!(bhp_trade.pnl < 0.0);

        assert!((cba_trade.entry_price - 110.0).abs() < f64::EPSILON);
        assert!((cba_trade.exit_price - 90.0).abs() < f64::EPSILON);
        assert!(cba_trade.pnl < 0.0);

        let total_pnl: f64 = result.portfolio.closed_trades.iter().map(|t| t.pnl).sum();
        assert!((total_pnl - (bhp_trade.pnl + cba_trade.pnl)).abs() < f64::EPSILON);
    }

    #[test]
    fn multi_code_different_entry_exit_times() {
        let bhp_bars = vec![
            make_bar("BHP", "2024-01-01", 110.0),
            make_bar("BHP", "2024-01-02", 110.0),
            make_bar("BHP", "2024-01-03", 90.0),
        ];
        let rio_bars = vec![
            make_bar("RIO", "2024-01-01", 90.0),
            make_bar("RIO", "2024-01-02", 110.0),
            make_bar("RIO", "2024-01-03", 110.0),
            make_bar("RIO", "2024-01-04", 90.0),
        ];

        let bhp = make_code_data("BHP", bhp_bars);
        let rio = make_code_data("RIO", rio_bars);
        let timeline = build_unified_timeline(&[bhp.clone(), rio.clone()]);

        let mut strategy = make_simple_strategy();
        strategy.max_positions = 2;
        let config = sample_config();

        let result = run_backtest(&[bhp, rio], &timeline, &strategy, &config);

        assert_eq!(result.portfolio.closed_trades.len(), 2);

        let bhp_trade = result
            .portfolio
            .closed_trades
            .iter()
            .find(|t| t.code == "BHP")
            .unwrap();
        let rio_trade = result
            .portfolio
            .closed_trades
            .iter()
            .find(|t| t.code == "RIO")
            .unwrap();

        assert_eq!(bhp_trade.entry_date, date(2024, 1, 1));
        assert_eq!(bhp_trade.exit_date, date(2024, 1, 3));
        assert_eq!(rio_trade.entry_date, date(2024, 1, 2));
        assert_eq!(rio_trade.exit_date, date(2024, 1, 4));
    }
}

mod max_positions_enforcement {
    use super::*;

    #[test]
    fn max_positions_one_only_first_entry_kept() {
        let bhp_bars = vec![
            make_bar("BHP", "2024-01-01", 110.0),
            make_bar("BHP", "2024-01-02", 110.0),
            make_bar("BHP", "2024-01-03", 90.0),
        ];
        let cba_bars = vec![
            make_bar("CBA", "2024-01-01", 90.0),
            make_bar("CBA", "2024-01-02", 110.0),
            make_bar("CBA", "2024-01-03", 110.0),
        ];

        let bhp = make_code_data("BHP", bhp_bars);
        let cba = make_code_data("CBA", cba_bars);
        let timeline = build_unified_timeline(&[bhp.clone(), cba.clone()]);

        let strategy = make_simple_strategy();
        assert_eq!(strategy.max_positions, 1);
        let config = sample_config();

        let result = run_backtest(&[bhp, cba], &timeline, &strategy, &config);

        assert_eq!(result.portfolio.closed_trades.len(), 1);
        assert_eq!(result.portfolio.closed_trades[0].code, "BHP");
    }

    #[test]
    fn max_positions_two_allows_both() {
        let bhp_bars = vec![
            make_bar("BHP", "2024-01-01", 110.0),
            make_bar("BHP", "2024-01-02", 110.0),
            make_bar("BHP", "2024-01-03", 90.0),
        ];
        let cba_bars = vec![
            make_bar("CBA", "2024-01-01", 110.0),
            make_bar("CBA", "2024-01-02", 110.0),
            make_bar("CBA", "2024-01-03", 90.0),
        ];

        let bhp = make_code_data("BHP", bhp_bars);
        let cba = make_code_data("CBA", cba_bars);
        let timeline = build_unified_timeline(&[bhp.clone(), cba.clone()]);

        let mut strategy = make_simple_strategy();
        strategy.max_positions = 2;
        let config = sample_config();

        let result = run_backtest(&[bhp, cba], &timeline, &strategy, &config);

        assert_eq!(result.portfolio.closed_trades.len(), 2);
    }

    #[test]
    fn max_positions_respected_after_exit() {
        let a_bars = vec![
            make_bar("A", "2024-01-01", 110.0),
            make_bar("A", "2024-01-02", 110.0),
            make_bar("A", "2024-01-03", 90.0),
            make_bar("A", "2024-01-04", 90.0),
        ];
        let b_bars = vec![
            make_bar("B", "2024-01-01", 90.0),
            make_bar("B", "2024-01-02", 90.0),
            make_bar("B", "2024-01-03", 90.0),
            make_bar("B", "2024-01-04", 110.0),
            make_bar("B", "2024-01-05", 90.0),
        ];

        let a = make_code_data("A", a_bars);
        let b = make_code_data("B", b_bars);
        let timeline = build_unified_timeline(&[a.clone(), b.clone()]);

        let strategy = make_simple_strategy();
        let config = sample_config();

        let result = run_backtest(&[a, b], &timeline, &strategy, &config);

        assert_eq!(result.portfolio.closed_trades.len(), 2);

        let codes: std::collections::HashSet<_> = result
            .portfolio
            .closed_trades
            .iter()
            .map(|t| t.code.as_str())
            .collect();
        assert!(codes.contains("A"));
        assert!(codes.contains("B"));
    }

    #[test]
    fn max_positions_three_with_five_codes() {
        let codes = ["A", "B", "C", "D", "E"];
        let all_data: Vec<CodeData> = codes
            .iter()
            .map(|&c| {
                let bars = vec![
                    make_bar(c, "2024-01-01", 110.0),
                    make_bar(c, "2024-01-02", 110.0),
                    make_bar(c, "2024-01-03", 90.0),
                ];
                make_code_data(c, bars)
            })
            .collect();

        let timeline = build_unified_timeline(&all_data);

        let mut strategy = make_simple_strategy();
        strategy.max_positions = 3;
        let config = sample_config();

        let result = run_backtest(&all_data, &timeline, &strategy, &config);

        assert_eq!(result.portfolio.closed_trades.len(), 3);

        let traded_codes: std::collections::HashSet<_> = result
            .portfolio
            .closed_trades
            .iter()
            .map(|t| t.code.as_str())
            .collect();
        assert_eq!(traded_codes.len(), 3);
    }
}

mod partial_universe_validation {
    use super::*;

    #[test]
    fn partial_universe_some_skipped_others_proceed() {
        let good_bars = generate_bars("GOOD", "2024-01-01", 50, 100.0);
        let few_bars = generate_bars("FEW", "2024-01-01", 10, 50.0);

        let port = MockDataPort::new()
            .with_bars("GOOD", good_bars)
            .with_bars("FEW", few_bars);

        let codes = vec!["GOOD".to_string(), "FEW".to_string()];
        let result =
            validate_universe(&port, codes, "ASX", date(2024, 1, 1), date(2024, 12, 31)).unwrap();

        assert_eq!(result.universe.codes, vec!["GOOD"]);
        assert_eq!(result.skipped.len(), 1);
        assert_eq!(result.skipped[0].code, "FEW");
        assert!(matches!(
            result.skipped[0].reason,
            SkipReason::InsufficientBars { bars: 10 }
        ));
    }

    #[test]
    fn partial_universe_no_data_skipped() {
        let good_bars = generate_bars("GOOD", "2024-01-01", 50, 100.0);

        let port = MockDataPort::new().with_bars("GOOD", good_bars);

        let codes = vec!["GOOD".to_string(), "MISSING".to_string()];
        let result =
            validate_universe(&port, codes, "ASX", date(2024, 1, 1), date(2024, 12, 31)).unwrap();

        assert_eq!(result.universe.codes, vec!["GOOD"]);
        assert_eq!(result.skipped.len(), 1);
        assert_eq!(result.skipped[0].code, "MISSING");
        assert!(matches!(result.skipped[0].reason, SkipReason::NoData));
    }

    #[test]
    fn partial_universe_fetch_error_skipped() {
        let good_bars = generate_bars("GOOD", "2024-01-01", 50, 100.0);

        let port = MockDataPort::new()
            .with_bars("GOOD", good_bars)
            .with_error("BAD", "connection refused");

        let codes = vec!["GOOD".to_string(), "BAD".to_string()];
        let result =
            validate_universe(&port, codes, "ASX", date(2024, 1, 1), date(2024, 12, 31)).unwrap();

        assert_eq!(result.universe.codes, vec!["GOOD"]);
        assert_eq!(result.skipped.len(), 1);
        assert_eq!(result.skipped[0].code, "BAD");
    }

    #[test]
    fn partial_universe_exact_min_bars_valid() {
        let bars = generate_bars("EXACT", "2024-01-01", MIN_OHLCV_BARS, 100.0);

        let port = MockDataPort::new().with_bars("EXACT", bars);

        let codes = vec!["EXACT".to_string()];
        let result =
            validate_universe(&port, codes, "ASX", date(2024, 1, 1), date(2024, 12, 31)).unwrap();

        assert_eq!(result.universe.codes, vec!["EXACT"]);
        assert!(result.skipped.is_empty());
    }
}

mod backward_compat_single_code {
    use super::*;

    #[test]
    fn single_code_produces_consistent_results() {
        let bars1 = vec![
            make_bar("BHP", "2024-01-01", 90.0),
            make_bar("BHP", "2024-01-02", 110.0),
            make_bar("BHP", "2024-01-03", 105.0),
            make_bar("BHP", "2024-01-04", 95.0),
        ];
        let bars2 = bars1.clone();

        let code_data1 = make_code_data("BHP", bars1);
        let code_data2 = make_code_data("BHP", bars2);

        let timeline1 = build_unified_timeline(&[code_data1.clone()]);
        let timeline2 = build_unified_timeline(&[code_data2.clone()]);

        let strategy = make_simple_strategy();
        let config = sample_config();

        let result1 = run_backtest(&[code_data1], &timeline1, &strategy, &config);
        let result2 = run_backtest(&[code_data2], &timeline2, &strategy, &config);

        assert_eq!(
            result1.portfolio.closed_trades.len(),
            result2.portfolio.closed_trades.len()
        );
        assert_eq!(
            result1.portfolio.equity_curve.len(),
            result2.portfolio.equity_curve.len()
        );
        assert!((result1.portfolio.cash - result2.portfolio.cash).abs() < f64::EPSILON);
    }

    #[test]
    fn single_code_vs_universe_of_one_equivalent() {
        let bars = vec![
            make_bar("BHP", "2024-01-01", 90.0),
            make_bar("BHP", "2024-01-02", 110.0),
            make_bar("BHP", "2024-01-03", 105.0),
            make_bar("BHP", "2024-01-04", 95.0),
        ];

        let code_data = make_code_data("BHP", bars);

        let timeline_single = build_unified_timeline(&[code_data.clone()]);
        let timeline_universe = build_unified_timeline(&[code_data.clone()]);

        let strategy = make_simple_strategy();
        let config = sample_config();

        let result_single =
            run_backtest(&[code_data.clone()], &timeline_single, &strategy, &config);
        let result_universe = run_backtest(&[code_data], &timeline_universe, &strategy, &config);

        assert_eq!(
            result_single.portfolio.closed_trades.len(),
            result_universe.portfolio.closed_trades.len()
        );
        assert_eq!(result_single.portfolio.cash, result_universe.portfolio.cash);
    }

    #[test]
    fn single_code_parse_and_validate() {
        let codes = parse_codes("BHP").unwrap();
        assert_eq!(codes, vec!["BHP"]);

        let bars = generate_bars("BHP", "2024-01-01", 50, 100.0);
        let port = MockDataPort::new().with_bars("BHP", bars);

        let result =
            validate_universe(&port, codes, "ASX", date(2024, 1, 1), date(2024, 12, 31)).unwrap();

        assert_eq!(result.universe.count(), 1);
        assert!(result.skipped.is_empty());
    }
}

mod equity_curve_and_metrics {
    use super::*;

    #[test]
    fn equity_curve_records_all_dates() {
        let bars = vec![
            make_bar("BHP", "2024-01-01", 100.0),
            make_bar("BHP", "2024-01-02", 100.0),
            make_bar("BHP", "2024-01-03", 100.0),
            make_bar("BHP", "2024-01-04", 100.0),
            make_bar("BHP", "2024-01-05", 100.0),
        ];

        let code_data = make_code_data("BHP", bars);
        let timeline = build_unified_timeline(&[code_data.clone()]);

        let strategy = make_simple_strategy();
        let config = sample_config();

        let result = run_backtest(&[code_data], &timeline, &strategy, &config);

        assert_eq!(result.portfolio.equity_curve.len(), 5);
        for (i, point) in result.portfolio.equity_curve.iter().enumerate() {
            assert_eq!(point.date, timeline[i]);
        }
    }

    #[test]
    fn equity_curve_reflects_pnl() {
        let bars = vec![
            make_bar("BHP", "2024-01-01", 90.0),
            make_bar("BHP", "2024-01-02", 110.0),
            make_bar("BHP", "2024-01-03", 120.0),
            make_bar("BHP", "2024-01-04", 90.0),
        ];

        let code_data = make_code_data("BHP", bars);
        let timeline = build_unified_timeline(&[code_data.clone()]);

        let strategy = make_simple_strategy();
        let config = sample_config();

        let result = run_backtest(&[code_data], &timeline, &strategy, &config);

        let initial_equity = result.portfolio.equity_curve[0].equity;
        let final_equity = result.portfolio.equity_curve.last().unwrap().equity;

        assert!((initial_equity - 100_000.0).abs() < f64::EPSILON);
        assert!(final_equity < initial_equity);
    }

    #[test]
    fn portfolio_invariant_cash_plus_positions_equals_equity() {
        let bars = vec![
            make_bar("BHP", "2024-01-01", 110.0),
            make_bar("BHP", "2024-01-02", 120.0),
            make_bar("BHP", "2024-01-03", 130.0),
        ];

        let code_data = make_code_data("BHP", bars);
        let timeline = build_unified_timeline(&[code_data.clone()]);

        let mut strategy = make_simple_strategy();
        strategy.exit_long = Rule::Below {
            left: Operand::Close,
            right: Operand::Constant(0.0),
        };
        let config = sample_config();

        let result = run_backtest(&[code_data], &timeline, &strategy, &config);

        assert_eq!(result.portfolio.position_count(), 1);

        let mut price_map = HashMap::new();
        price_map.insert("BHP".to_string(), 130.0);

        let position_value = result
            .portfolio
            .positions
            .values()
            .map(|p| p.market_value(130.0))
            .sum::<f64>();
        let expected_equity = result.portfolio.cash + position_value;
        let actual_equity = result.portfolio.total_equity(&price_map);

        assert!((actual_equity - expected_equity).abs() < f64::EPSILON);
    }
}
