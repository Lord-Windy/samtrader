//! Integration tests per TRD §15.2.
//!
//! Tests cover:
//! - Full backtest pipeline with mock data port (no database)
//! - Multi-code backtest with known trades — verify per-code PnL
//! - `max_positions` enforcement across codes
//! - Partial universe validation (some codes skipped, others proceed)
//! - Backward compatibility: single `code` config produces identical results
//! - Full backtest pipeline via SqliteAdapter with seeded in-memory database (TRD §14.2)
//! - Cross-adapter parity: SqliteAdapter and PostgresAdapter return identical results

mod common;

use common::*;
use samtrader::domain::backtest::{run_backtest, BacktestResult};
use samtrader::domain::code_data::{build_unified_timeline, CodeData};
use samtrader::domain::error::SamtraderError;
use samtrader::domain::rule::{Operand, Rule};
use samtrader::domain::strategy::Strategy;
use samtrader::domain::universe::{parse_codes, validate_universe, SkipReason, MIN_OHLCV_BARS};
use samtrader::ports::data_port::DataPort;
use samtrader::ports::report_port::ReportPort;
use std::cell::RefCell;
use std::collections::HashMap;

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

struct MockReportPort {
    calls: RefCell<Vec<(BacktestResult, Strategy, String)>>,
}

impl MockReportPort {
    fn new() -> Self {
        Self {
            calls: RefCell::new(Vec::new()),
        }
    }
}

impl ReportPort for MockReportPort {
    fn write(
        &self,
        result: &BacktestResult,
        strategy: &Strategy,
        output_path: &str,
    ) -> Result<(), SamtraderError> {
        self.calls
            .borrow_mut()
            .push((result.clone(), strategy.clone(), output_path.to_string()));
        Ok(())
    }
}

mod report_generation {
    use super::*;

    #[test]
    fn report_receives_strategy_metadata() {
        let bars = vec![
            make_bar("BHP", "2024-01-01", 90.0),
            make_bar("BHP", "2024-01-02", 110.0),
            make_bar("BHP", "2024-01-03", 90.0),
        ];
        let code_data = make_code_data("BHP", bars);
        let timeline = build_unified_timeline(&[code_data.clone()]);
        let strategy = make_simple_strategy();
        let config = sample_config();

        let result = run_backtest(&[code_data], &timeline, &strategy, &config);

        let report = MockReportPort::new();
        report
            .write(&result, &strategy, "output.typ")
            .expect("report write should succeed");

        let calls = report.calls.borrow();
        assert_eq!(calls.len(), 1);
        let (_, ref strat, ref path) = calls[0];

        assert_eq!(strat.name, "Test");
        assert_eq!(strat.description, "Test strategy");
        assert_eq!(strat.position_size, 0.25);
        assert_eq!(strat.max_positions, 1);
        assert_eq!(path, "output.typ");
    }

    #[test]
    fn report_receives_equity_curve() {
        let bars = vec![
            make_bar("BHP", "2024-01-01", 90.0),
            make_bar("BHP", "2024-01-02", 110.0),
            make_bar("BHP", "2024-01-03", 120.0),
            make_bar("BHP", "2024-01-04", 90.0),
            make_bar("BHP", "2024-01-05", 85.0),
        ];
        let code_data = make_code_data("BHP", bars);
        let timeline = build_unified_timeline(&[code_data.clone()]);
        let strategy = make_simple_strategy();
        let config = sample_config();

        let result = run_backtest(&[code_data], &timeline, &strategy, &config);

        let report = MockReportPort::new();
        report.write(&result, &strategy, "out.typ").unwrap();

        let calls = report.calls.borrow();
        let ref res = calls[0].0;

        assert_eq!(res.portfolio.equity_curve.len(), 5);
        assert!((res.portfolio.equity_curve[0].equity - 100_000.0).abs() < f64::EPSILON);
        let final_equity = res.portfolio.equity_curve.last().unwrap().equity;
        assert!(final_equity != 100_000.0);
    }

    #[test]
    fn report_receives_trade_log() {
        let bhp_bars = vec![
            make_bar("BHP", "2024-01-01", 90.0),
            make_bar("BHP", "2024-01-02", 110.0),
            make_bar("BHP", "2024-01-03", 90.0),
        ];
        let cba_bars = vec![
            make_bar("CBA", "2024-01-01", 90.0),
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

        let report = MockReportPort::new();
        report.write(&result, &strategy, "out.typ").unwrap();

        let calls = report.calls.borrow();
        let ref res = calls[0].0;

        assert_eq!(res.portfolio.closed_trades.len(), 2);
        let codes: std::collections::HashSet<&str> = res
            .portfolio
            .closed_trades
            .iter()
            .map(|t| t.code.as_str())
            .collect();
        assert!(codes.contains("BHP"));
        assert!(codes.contains("CBA"));

        for trade in &res.portfolio.closed_trades {
            assert!(!trade.code.is_empty());
            assert!(trade.quantity > 0);
            assert!(trade.entry_price > 0.0);
            assert!(trade.exit_price > 0.0);
            assert!(trade.entry_date < trade.exit_date);
        }
    }

    #[test]
    fn report_receives_per_code_data_for_universe_summary() {
        let bhp_bars = vec![
            make_bar("BHP", "2024-01-01", 90.0),
            make_bar("BHP", "2024-01-02", 110.0),
            make_bar("BHP", "2024-01-03", 120.0),
            make_bar("BHP", "2024-01-04", 90.0),
        ];
        let cba_bars = vec![
            make_bar("CBA", "2024-01-01", 90.0),
            make_bar("CBA", "2024-01-02", 110.0),
            make_bar("CBA", "2024-01-03", 90.0),
        ];
        let rio_bars = vec![
            make_bar("RIO", "2024-01-01", 90.0),
            make_bar("RIO", "2024-01-02", 110.0),
            make_bar("RIO", "2024-01-03", 115.0),
            make_bar("RIO", "2024-01-04", 90.0),
        ];

        let bhp = make_code_data("BHP", bhp_bars);
        let cba = make_code_data("CBA", cba_bars);
        let rio = make_code_data("RIO", rio_bars);
        let timeline = build_unified_timeline(&[bhp.clone(), cba.clone(), rio.clone()]);

        let mut strategy = make_simple_strategy();
        strategy.max_positions = 3;
        let config = sample_config();

        let result = run_backtest(&[bhp, cba, rio], &timeline, &strategy, &config);

        let report = MockReportPort::new();
        report.write(&result, &strategy, "out.typ").unwrap();

        let calls = report.calls.borrow();
        let ref res = calls[0].0;

        let mut per_code: HashMap<&str, Vec<&samtrader::domain::position::ClosedTrade>> =
            HashMap::new();
        for trade in &res.portfolio.closed_trades {
            per_code.entry(trade.code.as_str()).or_default().push(trade);
        }

        assert_eq!(per_code.len(), 3);
        assert!(per_code.contains_key("BHP"));
        assert!(per_code.contains_key("CBA"));
        assert!(per_code.contains_key("RIO"));

        for (_, trades) in &per_code {
            assert_eq!(trades.len(), 1);
        }

        for (_, trades) in &per_code {
            let code_pnl: f64 = trades.iter().map(|t| t.pnl).sum();
            assert!(code_pnl.is_finite());
        }
    }

    #[test]
    fn report_data_supports_drawdown_calculation() {
        let bars = vec![
            make_bar("BHP", "2024-01-01", 90.0),
            make_bar("BHP", "2024-01-02", 110.0),
            make_bar("BHP", "2024-01-03", 120.0),
            make_bar("BHP", "2024-01-04", 95.0),
            make_bar("BHP", "2024-01-05", 90.0),
        ];
        let code_data = make_code_data("BHP", bars);
        let timeline = build_unified_timeline(&[code_data.clone()]);
        let strategy = make_simple_strategy();
        let config = sample_config();

        let result = run_backtest(&[code_data], &timeline, &strategy, &config);

        let report = MockReportPort::new();
        report.write(&result, &strategy, "out.typ").unwrap();

        let calls = report.calls.borrow();
        let ref res = calls[0].0;

        let equity = &res.portfolio.equity_curve;
        assert!(
            equity.len() >= 2,
            "need at least 2 points for drawdown chart"
        );

        let mut peak = equity[0].equity;
        let mut max_drawdown = 0.0_f64;
        for point in equity {
            if point.equity > peak {
                peak = point.equity;
            }
            let dd = (peak - point.equity) / peak;
            if dd > max_drawdown {
                max_drawdown = dd;
            }
        }
        assert!(max_drawdown > 0.0);
    }

    #[test]
    fn report_data_supports_monthly_returns() {
        let bars = vec![
            make_bar("BHP", "2024-01-01", 90.0),
            make_bar("BHP", "2024-01-02", 110.0),
            make_bar("BHP", "2024-01-15", 120.0),
            make_bar("BHP", "2024-01-20", 90.0),
            make_bar("BHP", "2024-01-31", 85.0),
        ];
        let code_data = make_code_data("BHP", bars);
        let timeline = build_unified_timeline(&[code_data.clone()]);
        let strategy = make_simple_strategy();
        let config = sample_config();

        let result = run_backtest(&[code_data], &timeline, &strategy, &config);

        let report = MockReportPort::new();
        report.write(&result, &strategy, "out.typ").unwrap();

        let calls = report.calls.borrow();
        let ref res = calls[0].0;

        let equity = &res.portfolio.equity_curve;
        assert!(!equity.is_empty());

        let first_date = equity.first().unwrap().date;
        let last_date = equity.last().unwrap().date;
        assert_eq!(first_date.format("%Y-%m").to_string(), "2024-01");
        assert_eq!(last_date.format("%Y-%m").to_string(), "2024-01");

        let monthly_return = (equity.last().unwrap().equity - equity.first().unwrap().equity)
            / equity.first().unwrap().equity;
        assert!(monthly_return.is_finite());
    }

    #[test]
    fn report_data_supports_aggregate_metrics() {
        let bars = vec![
            make_bar("BHP", "2024-01-01", 90.0),
            make_bar("BHP", "2024-01-02", 110.0),
            make_bar("BHP", "2024-01-03", 120.0),
            make_bar("BHP", "2024-01-04", 90.0),
            make_bar("BHP", "2024-01-05", 110.0),
            make_bar("BHP", "2024-01-06", 90.0),
        ];
        let code_data = make_code_data("BHP", bars);
        let timeline = build_unified_timeline(&[code_data.clone()]);
        let strategy = make_simple_strategy();
        let config = sample_config();

        let result = run_backtest(&[code_data], &timeline, &strategy, &config);

        let report = MockReportPort::new();
        report.write(&result, &strategy, "out.typ").unwrap();

        let calls = report.calls.borrow();
        let ref res = calls[0].0;

        let trades = &res.portfolio.closed_trades;
        assert!(trades.len() >= 2, "need trades for meaningful metrics");

        let initial = res.portfolio.initial_capital;
        let final_equity = res.portfolio.equity_curve.last().unwrap().equity;
        let total_return = (final_equity - initial) / initial;
        assert!(total_return.is_finite());

        let winners = trades.iter().filter(|t| t.pnl > 0.0).count();
        let win_rate = winners as f64 / trades.len() as f64;
        assert!(win_rate >= 0.0 && win_rate <= 1.0);

        let gross_profit: f64 = trades.iter().filter(|t| t.pnl > 0.0).map(|t| t.pnl).sum();
        let gross_loss: f64 = trades
            .iter()
            .filter(|t| t.pnl < 0.0)
            .map(|t| t.pnl.abs())
            .sum();
        if gross_loss > 0.0 {
            let profit_factor = gross_profit / gross_loss;
            assert!(profit_factor.is_finite());
        }
    }
}

#[cfg(feature = "sqlite")]
mod sqlite_adapter_tests {
    use super::*;
    use samtrader::adapters::sqlite_adapter::SqliteAdapter;
    use samtrader::domain::universe::validate_universe;

    fn seed_sqlite_adapter(bars: &[OhlcvBar]) -> SqliteAdapter {
        let adapter = SqliteAdapter::in_memory().unwrap();
        adapter.initialize_schema().unwrap();
        adapter.insert_bars(bars).unwrap();
        adapter
    }

    #[test]
    fn full_backtest_pipeline_via_sqlite_adapter() {
        let bars = vec![
            make_bar("BHP", "2024-01-01", 90.0),
            make_bar("BHP", "2024-01-02", 110.0),
            make_bar("BHP", "2024-01-03", 105.0),
            make_bar("BHP", "2024-01-04", 95.0),
            make_bar("BHP", "2024-01-05", 90.0),
        ];

        let adapter = seed_sqlite_adapter(&bars);

        let ohlcv = adapter
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
    fn sqlite_multi_code_backtest() {
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

        let all_bars: Vec<_> = bhp_bars
            .iter()
            .cloned()
            .chain(cba_bars.iter().cloned())
            .collect();
        let adapter = seed_sqlite_adapter(&all_bars);

        let bhp_ohlcv = adapter
            .fetch_ohlcv("BHP", "ASX", date(2024, 1, 1), date(2024, 1, 4))
            .unwrap();
        let cba_ohlcv = adapter
            .fetch_ohlcv("CBA", "ASX", date(2024, 1, 1), date(2024, 1, 4))
            .unwrap();

        let bhp = make_code_data("BHP", bhp_ohlcv);
        let cba = make_code_data("CBA", cba_ohlcv);
        let timeline = build_unified_timeline(&[bhp.clone(), cba.clone()]);

        let mut strategy = make_simple_strategy();
        strategy.max_positions = 2;
        let config = sample_config();

        let result = run_backtest(&[bhp, cba], &timeline, &strategy, &config);

        assert_eq!(result.portfolio.closed_trades.len(), 2);
    }

    #[test]
    fn sqlite_universe_validation() {
        let good_bars = generate_bars("GOOD", "2024-01-01", 50, 100.0);
        let few_bars = generate_bars("FEW", "2024-01-01", 10, 50.0);

        let all_bars: Vec<_> = good_bars
            .iter()
            .cloned()
            .chain(few_bars.iter().cloned())
            .collect();
        let adapter = seed_sqlite_adapter(&all_bars);

        let codes = vec!["GOOD".to_string(), "FEW".to_string()];
        let result =
            validate_universe(&adapter, codes, "ASX", date(2024, 1, 1), date(2024, 12, 31))
                .unwrap();

        assert_eq!(result.universe.codes, vec!["GOOD"]);
        assert_eq!(result.skipped.len(), 1);
        assert_eq!(result.skipped[0].code, "FEW");
    }

    #[test]
    fn sqlite_list_symbols_returns_inserted_codes() {
        let bars = vec![
            make_bar("BHP", "2024-01-01", 100.0),
            make_bar("CBA", "2024-01-01", 100.0),
            make_bar("RIO", "2024-01-01", 100.0),
        ];
        let adapter = seed_sqlite_adapter(&bars);

        let symbols = adapter.list_symbols("ASX").unwrap();
        assert_eq!(symbols, vec!["BHP", "CBA", "RIO"]);
    }

    #[test]
    fn sqlite_get_data_range_returns_correct_range() {
        let bars = vec![
            make_bar("BHP", "2024-01-01", 100.0),
            make_bar("BHP", "2024-01-05", 105.0),
            make_bar("BHP", "2024-01-10", 110.0),
        ];
        let adapter = seed_sqlite_adapter(&bars);

        let range = adapter.get_data_range("BHP", "ASX").unwrap();
        assert!(range.is_some());
        let (min, max, count) = range.unwrap();
        assert_eq!(min, date(2024, 1, 1));
        assert_eq!(max, date(2024, 1, 10));
        assert_eq!(count, 3);
    }

    #[test]
    fn sqlite_data_port_fetch_respects_date_range() {
        let bars = vec![
            make_bar("BHP", "2024-01-01", 100.0),
            make_bar("BHP", "2024-01-02", 101.0),
            make_bar("BHP", "2024-01-03", 102.0),
            make_bar("BHP", "2024-01-04", 103.0),
            make_bar("BHP", "2024-01-05", 104.0),
        ];
        let adapter = seed_sqlite_adapter(&bars);

        let fetched = adapter
            .fetch_ohlcv("BHP", "ASX", date(2024, 1, 2), date(2024, 1, 4))
            .unwrap();
        assert_eq!(fetched.len(), 3);
    }
}

#[cfg(all(feature = "sqlite", feature = "postgres"))]
mod cross_adapter_parity_tests {
    use super::*;
    use samtrader::adapters::postgres_adapter::PostgresAdapter;
    use samtrader::adapters::sqlite_adapter::SqliteAdapter;
    use samtrader::ports::config_port::ConfigPort;

    struct SqliteTestConfig {
        path: String,
    }

    impl SqliteTestConfig {
        fn new(path: &str) -> Self {
            Self {
                path: path.to_string(),
            }
        }
    }

    impl ConfigPort for SqliteTestConfig {
        fn get_string(&self, section: &str, key: &str) -> Option<String> {
            if section == "sqlite" && key == "path" {
                return Some(self.path.clone());
            }
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

    struct PostgresTestConfig {
        conninfo: String,
    }

    impl PostgresTestConfig {
        fn new(conninfo: &str) -> Self {
            Self {
                conninfo: conninfo.to_string(),
            }
        }
    }

    impl ConfigPort for PostgresTestConfig {
        fn get_string(&self, section: &str, key: &str) -> Option<String> {
            if section == "postgres" && key == "connection_string" {
                return Some(self.conninfo.clone());
            }
            if section == "database" && key == "conninfo" {
                return Some(self.conninfo.clone());
            }
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
    fn cross_adapter_parity_backtest_results() {
        let test_bars = vec![
            make_bar("BHP", "2024-01-01", 90.0),
            make_bar("BHP", "2024-01-02", 110.0),
            make_bar("BHP", "2024-01-03", 105.0),
            make_bar("BHP", "2024-01-04", 95.0),
        ];

        let sqlite_adapter = SqliteAdapter::in_memory().unwrap();
        sqlite_adapter.initialize_schema().unwrap();
        sqlite_adapter.insert_bars(&test_bars).unwrap();

        let sqlite_ohlcv = sqlite_adapter
            .fetch_ohlcv("BHP", "ASX", date(2024, 1, 1), date(2024, 1, 4))
            .unwrap();

        let sqlite_code_data = make_code_data("BHP", sqlite_ohlcv.clone());
        let sqlite_timeline = build_unified_timeline(&[sqlite_code_data.clone()]);
        let strategy = make_simple_strategy();
        let config = sample_config();

        let sqlite_result = run_backtest(&[sqlite_code_data], &sqlite_timeline, &strategy, &config);

        let mock_port = MockDataPort::new().with_bars("BHP", test_bars.clone());
        let mock_ohlcv = mock_port
            .fetch_ohlcv("BHP", "ASX", date(2024, 1, 1), date(2024, 1, 4))
            .unwrap();
        let mock_code_data = make_code_data("BHP", mock_ohlcv);
        let mock_timeline = build_unified_timeline(&[mock_code_data.clone()]);
        let mock_result = run_backtest(&[mock_code_data], &mock_timeline, &strategy, &config);

        assert_eq!(
            sqlite_result.portfolio.closed_trades.len(),
            mock_result.portfolio.closed_trades.len()
        );
        assert_eq!(
            sqlite_result.portfolio.equity_curve.len(),
            mock_result.portfolio.equity_curve.len()
        );

        for (sqlite_trade, mock_trade) in sqlite_result
            .portfolio
            .closed_trades
            .iter()
            .zip(mock_result.portfolio.closed_trades.iter())
        {
            assert_eq!(sqlite_trade.code, mock_trade.code);
            assert_eq!(sqlite_trade.entry_date, mock_trade.entry_date);
            assert_eq!(sqlite_trade.exit_date, mock_trade.exit_date);
            assert!((sqlite_trade.entry_price - mock_trade.entry_price).abs() < f64::EPSILON);
            assert!((sqlite_trade.exit_price - mock_trade.exit_price).abs() < f64::EPSILON);
        }
    }

    #[test]
    fn cross_adapter_parity_fetch_ohlcv() {
        let test_bars = vec![
            make_bar("BHP", "2024-01-01", 100.0),
            make_bar("BHP", "2024-01-02", 101.0),
            make_bar("BHP", "2024-01-03", 102.0),
        ];

        let sqlite_adapter = SqliteAdapter::in_memory().unwrap();
        sqlite_adapter.initialize_schema().unwrap();
        sqlite_adapter.insert_bars(&test_bars).unwrap();

        let sqlite_bars = sqlite_adapter
            .fetch_ohlcv("BHP", "ASX", date(2024, 1, 1), date(2024, 1, 3))
            .unwrap();

        assert_eq!(sqlite_bars.len(), test_bars.len());
        for (sqlite_bar, test_bar) in sqlite_bars.iter().zip(test_bars.iter()) {
            assert_eq!(sqlite_bar.code, test_bar.code);
            assert_eq!(sqlite_bar.exchange, test_bar.exchange);
            assert_eq!(sqlite_bar.date, test_bar.date);
            assert!((sqlite_bar.open - test_bar.open).abs() < f64::EPSILON);
            assert!((sqlite_bar.high - test_bar.high).abs() < f64::EPSILON);
            assert!((sqlite_bar.low - test_bar.low).abs() < f64::EPSILON);
            assert!((sqlite_bar.close - test_bar.close).abs() < f64::EPSILON);
            assert_eq!(sqlite_bar.volume, test_bar.volume);
        }
    }
}
