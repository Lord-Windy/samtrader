//! Backtest engine and event loop (TRD Section 3.4, 8.3).
//!
//! BacktestConfig (TRD Section 3.7) defines backtest parameters.
//! run_backtest executes the unified backtest loop.

use chrono::NaiveDate;
use std::collections::HashMap;

use super::code_data::CodeData;
use super::execution::{self, EntryResult, ExecutionConfig, ExecutionParams};
use super::portfolio::Portfolio;
use super::rule_eval;
use super::strategy::Strategy;

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

#[derive(Debug, Clone)]
pub struct BacktestResult {
    pub portfolio: Portfolio,
}

/// Extended result for multi-code backtests (TRD Section 11.3).
///
/// Contains the aggregate portfolio result plus per-code breakdowns.
#[derive(Debug, Clone)]
pub struct MultiCodeResult {
    pub aggregate: BacktestResult,
    pub code_results: Vec<CodeResult>,
}

/// Per-code backtest breakdown.
#[derive(Debug, Clone)]
pub struct CodeResult {
    pub code: String,
    pub result: BacktestResult,
}

pub fn run_backtest(
    code_data: &[CodeData],
    timeline: &[NaiveDate],
    strategy: &Strategy,
    config: &BacktestConfig,
) -> BacktestResult {
    let mut portfolio = Portfolio::new(config.initial_capital);
    let mut entry_commissions: HashMap<String, f64> = HashMap::new();

    let exec_config = ExecutionConfig {
        commission_per_trade: config.commission_per_trade,
        commission_pct: config.commission_pct,
        slippage_pct: config.slippage_pct,
        allow_shorting: config.allow_shorting,
    };

    let exec_params = ExecutionParams {
        position_size: strategy.position_size,
        stop_loss_pct: strategy.stop_loss_pct,
        take_profit_pct: strategy.take_profit_pct,
    };

    for &date in timeline {
        let mut price_map: HashMap<String, f64> = HashMap::new();
        for cd in code_data {
            if let Some(bar) = cd.get_bar(date) {
                price_map.insert(cd.code.clone(), bar.close);
            }
        }

        execution::check_triggers(
            &mut portfolio,
            &price_map,
            date,
            &entry_commissions,
            &exec_config,
        );

        for cd in code_data {
            let bar_index = match cd.get_bar_index(date) {
                Some(idx) => idx,
                None => continue,
            };

            let bar = &cd.ohlcv[bar_index];

            if portfolio.has_position(&cd.code) {
                if rule_eval::evaluate(&strategy.exit_long, &cd.ohlcv, &cd.indicators, bar_index) {
                    let entry_commission = entry_commissions.remove(&cd.code).unwrap_or(0.0);
                    execution::exit_position(
                        &mut portfolio,
                        &cd.code,
                        bar.close,
                        date,
                        entry_commission,
                        &exec_config,
                    );
                }
            }

            if !portfolio.has_position(&cd.code) {
                if portfolio.position_count() >= strategy.max_positions {
                    continue;
                }
                if rule_eval::evaluate(&strategy.entry_long, &cd.ohlcv, &cd.indicators, bar_index) {
                    let result = execution::enter_long(
                        &mut portfolio,
                        &cd.code,
                        &cd.exchange,
                        bar.close,
                        date,
                        &exec_params,
                        &exec_config,
                    );
                    if let EntryResult::Entered { commission, .. } = result {
                        entry_commissions.insert(cd.code.clone(), commission);
                    }
                }
            }
        }

        let equity = portfolio.total_equity(&price_map);
        portfolio.record_equity(date, equity);
    }

    BacktestResult { portfolio }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::domain::code_data::build_unified_timeline;
    use crate::domain::ohlcv::OhlcvBar;
    use crate::domain::rule::{Operand, Rule};

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

    #[test]
    fn run_backtest_no_trades() {
        let bars = vec![
            make_bar("BHP", "2024-01-01", 90.0),
            make_bar("BHP", "2024-01-02", 89.0),
            make_bar("BHP", "2024-01-03", 88.0),
        ];
        let code_data = make_code_data("BHP", bars);
        let timeline = build_unified_timeline(&[code_data.clone()]);
        let strategy = make_simple_strategy();
        let config = sample_config();

        let result = run_backtest(&[code_data], &timeline, &strategy, &config);

        assert_eq!(result.portfolio.closed_trades.len(), 0);
        assert_eq!(result.portfolio.position_count(), 0);
        assert!((result.portfolio.cash - 100_000.0).abs() < f64::EPSILON);
    }

    #[test]
    fn run_backtest_single_entry_exit() {
        let bars = vec![
            make_bar("BHP", "2024-01-01", 90.0),
            make_bar("BHP", "2024-01-02", 110.0),
            make_bar("BHP", "2024-01-03", 105.0),
            make_bar("BHP", "2024-01-04", 95.0),
        ];
        let code_data = make_code_data("BHP", bars);
        let timeline = build_unified_timeline(&[code_data.clone()]);
        let strategy = make_simple_strategy();
        let config = sample_config();

        let result = run_backtest(&[code_data], &timeline, &strategy, &config);

        assert_eq!(result.portfolio.closed_trades.len(), 1);
        let trade = &result.portfolio.closed_trades[0];
        assert_eq!(trade.code, "BHP");
        assert_eq!(
            trade.entry_date,
            NaiveDate::from_ymd_opt(2024, 1, 2).unwrap()
        );
        assert_eq!(
            trade.exit_date,
            NaiveDate::from_ymd_opt(2024, 1, 4).unwrap()
        );
        assert!((trade.entry_price - 110.0).abs() < f64::EPSILON);
        assert!((trade.exit_price - 95.0).abs() < f64::EPSILON);

        // position_size=0.25, so available = 100_000 * 0.25 = 25_000
        // quantity = floor(25_000 / 110) = 227
        // cost = 227 * 110 = 24_970
        // exit_value = 227 * 95 = 21_565
        // pnl = 227 * (95 - 110) = -3_405
        assert_eq!(trade.quantity, 227);
        assert!((trade.pnl - (-3_405.0)).abs() < f64::EPSILON);
        // cash = (100_000 - 24_970) + 21_565 = 96_595
        assert!((result.portfolio.cash - 96_595.0).abs() < f64::EPSILON);
    }

    #[test]
    fn run_backtest_equity_curve() {
        let bars = vec![
            make_bar("BHP", "2024-01-01", 100.0),
            make_bar("BHP", "2024-01-02", 100.0),
            make_bar("BHP", "2024-01-03", 100.0),
        ];
        let code_data = make_code_data("BHP", bars);
        let timeline = build_unified_timeline(&[code_data.clone()]);
        let strategy = make_simple_strategy();
        let config = sample_config();

        let result = run_backtest(&[code_data], &timeline, &strategy, &config);

        assert_eq!(result.portfolio.equity_curve.len(), 3);
        for point in &result.portfolio.equity_curve {
            assert!((point.equity - 100_000.0).abs() < f64::EPSILON);
        }
    }

    #[test]
    fn run_backtest_processes_all_timeline_dates() {
        let bars = vec![
            make_bar("BHP", "2024-01-01", 110.0),
            make_bar("BHP", "2024-01-02", 110.0),
            make_bar("BHP", "2024-01-03", 110.0),
            make_bar("BHP", "2024-01-04", 90.0),
            make_bar("BHP", "2024-01-05", 90.0),
        ];
        let code_data = make_code_data("BHP", bars);
        // Pass a subset of the timeline — caller controls date range
        let timeline: Vec<NaiveDate> = vec![
            NaiveDate::from_ymd_opt(2024, 1, 2).unwrap(),
            NaiveDate::from_ymd_opt(2024, 1, 3).unwrap(),
            NaiveDate::from_ymd_opt(2024, 1, 4).unwrap(),
        ];
        let strategy = make_simple_strategy();
        let config = sample_config();

        let result = run_backtest(&[code_data], &timeline, &strategy, &config);

        assert_eq!(result.portfolio.equity_curve.len(), 3);
        assert_eq!(
            result.portfolio.equity_curve[0].date,
            NaiveDate::from_ymd_opt(2024, 1, 2).unwrap()
        );
        assert_eq!(
            result.portfolio.equity_curve[2].date,
            NaiveDate::from_ymd_opt(2024, 1, 4).unwrap()
        );
    }

    #[test]
    fn run_backtest_max_positions_enforced() {
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
        let config = sample_config();

        let result = run_backtest(&[bhp, cba], &timeline, &strategy, &config);

        assert_eq!(result.portfolio.closed_trades.len(), 1);
        assert_eq!(result.portfolio.closed_trades[0].code, "BHP");
    }

    #[test]
    fn run_backtest_multi_code_positions() {
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

        assert_eq!(result.portfolio.closed_trades.len(), 2);
    }

    #[test]
    fn run_backtest_stop_loss_triggers() {
        // Entry at 110 on day 2. Stop loss at 10% => trigger at 99.0.
        // Day 3 close = 95 which is below 99 => stop loss fires.
        // Exit rule is close < 100, which would also fire at 95,
        // but we set exit_long to never fire (close < 0) to prove
        // the trigger mechanism closed the position, not the rule.
        let bars = vec![
            make_bar("BHP", "2024-01-01", 90.0),
            make_bar("BHP", "2024-01-02", 110.0),
            make_bar("BHP", "2024-01-03", 95.0),
        ];
        let code_data = make_code_data("BHP", bars);
        let timeline = build_unified_timeline(&[code_data.clone()]);
        let mut strategy = make_simple_strategy();
        strategy.stop_loss_pct = 10.0;
        // Set exit rule to never fire (close < 0 is impossible)
        strategy.exit_long = Rule::Below {
            left: Operand::Close,
            right: Operand::Constant(0.0),
        };
        let config = sample_config();

        let result = run_backtest(&[code_data], &timeline, &strategy, &config);

        assert_eq!(result.portfolio.closed_trades.len(), 1);
        let trade = &result.portfolio.closed_trades[0];
        assert!((trade.exit_price - 95.0).abs() < f64::EPSILON);
    }

    #[test]
    fn run_backtest_empty_timeline() {
        let bars: Vec<OhlcvBar> = vec![];
        let code_data = make_code_data("BHP", bars);
        let timeline: Vec<NaiveDate> = vec![];
        let strategy = make_simple_strategy();
        let config = sample_config();

        let result = run_backtest(&[code_data], &timeline, &strategy, &config);

        assert_eq!(result.portfolio.closed_trades.len(), 0);
        assert_eq!(result.portfolio.equity_curve.len(), 0);
    }

    #[test]
    fn run_backtest_with_commission() {
        // commission_per_trade = 10.0
        // Entry at 110 on day 2:
        //   available = 100_000 * 0.25 = 25_000
        //   quantity = floor(25_000 / 110) = 227
        //   cost = 227 * 110 = 24_970, commission = 10.0
        //   cash after entry = 100_000 - 24_970 - 10 = 75_020
        // Exit at 90 on day 3:
        //   exit_value = 227 * 90 = 20_430, exit_commission = 10.0
        //   cash after exit = 75_020 + 20_430 - 10 = 95_440
        //   pnl = 227*(90-110) - 10 - 10 = -4_540 - 20 = -4_560
        let bars = vec![
            make_bar("BHP", "2024-01-01", 90.0),
            make_bar("BHP", "2024-01-02", 110.0),
            make_bar("BHP", "2024-01-03", 90.0),
        ];
        let code_data = make_code_data("BHP", bars);
        let timeline = build_unified_timeline(&[code_data.clone()]);
        let strategy = make_simple_strategy();
        let config = BacktestConfig {
            commission_per_trade: 10.0,
            ..sample_config()
        };

        let result = run_backtest(&[code_data], &timeline, &strategy, &config);

        assert_eq!(result.portfolio.closed_trades.len(), 1);
        let trade = &result.portfolio.closed_trades[0];
        assert!((trade.pnl - (-4_560.0)).abs() < f64::EPSILON);
        assert!((result.portfolio.cash - 95_440.0).abs() < f64::EPSILON);
    }
}
