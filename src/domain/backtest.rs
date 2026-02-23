//! Backtest engine and event loop (TRD Section 3.4, 8.3).
//!
//! BacktestConfig (TRD Section 3.7) defines backtest parameters.
//! run_backtest executes the unified backtest loop.

use chrono::NaiveDate;
use std::collections::HashMap;

use super::code_data::CodeData;
use super::execution::{self, BacktestConfig as ExecutionConfig, EntryResult, ExecutionParams};
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
        if date < config.start_date || date > config.end_date {
            continue;
        }

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
        let config = BacktestConfig {
            start_date: NaiveDate::from_ymd_opt(2024, 1, 1).unwrap(),
            end_date: NaiveDate::from_ymd_opt(2024, 1, 31).unwrap(),
            ..sample_config()
        };

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
        let config = BacktestConfig {
            start_date: NaiveDate::from_ymd_opt(2024, 1, 1).unwrap(),
            end_date: NaiveDate::from_ymd_opt(2024, 1, 31).unwrap(),
            ..sample_config()
        };

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
        let config = BacktestConfig {
            start_date: NaiveDate::from_ymd_opt(2024, 1, 1).unwrap(),
            end_date: NaiveDate::from_ymd_opt(2024, 1, 31).unwrap(),
            ..sample_config()
        };

        let result = run_backtest(&[code_data], &timeline, &strategy, &config);

        assert_eq!(result.portfolio.equity_curve.len(), 3);
        for point in &result.portfolio.equity_curve {
            assert!((point.equity - 100_000.0).abs() < f64::EPSILON);
        }
    }

    #[test]
    fn run_backtest_respects_start_end_dates() {
        let bars = vec![
            make_bar("BHP", "2024-01-01", 110.0),
            make_bar("BHP", "2024-01-02", 110.0),
            make_bar("BHP", "2024-01-03", 110.0),
            make_bar("BHP", "2024-01-04", 90.0),
            make_bar("BHP", "2024-01-05", 90.0),
        ];
        let code_data = make_code_data("BHP", bars);
        let timeline = build_unified_timeline(&[code_data.clone()]);
        let strategy = make_simple_strategy();
        let config = BacktestConfig {
            start_date: NaiveDate::from_ymd_opt(2024, 1, 2).unwrap(),
            end_date: NaiveDate::from_ymd_opt(2024, 1, 4).unwrap(),
            ..sample_config()
        };

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
        let config = BacktestConfig {
            start_date: NaiveDate::from_ymd_opt(2024, 1, 1).unwrap(),
            end_date: NaiveDate::from_ymd_opt(2024, 1, 31).unwrap(),
            ..sample_config()
        };

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
        let config = BacktestConfig {
            start_date: NaiveDate::from_ymd_opt(2024, 1, 1).unwrap(),
            end_date: NaiveDate::from_ymd_opt(2024, 1, 31).unwrap(),
            ..sample_config()
        };

        let result = run_backtest(&[bhp, cba], &timeline, &strategy, &config);

        assert_eq!(result.portfolio.closed_trades.len(), 2);
    }

    #[test]
    fn run_backtest_stop_loss_triggers() {
        let bars = vec![
            make_bar("BHP", "2024-01-01", 90.0),
            make_bar("BHP", "2024-01-02", 110.0),
            make_bar("BHP", "2024-01-03", 95.0),
        ];
        let code_data = make_code_data("BHP", bars);
        let timeline = build_unified_timeline(&[code_data.clone()]);
        let mut strategy = make_simple_strategy();
        strategy.stop_loss_pct = 10.0;
        let config = BacktestConfig {
            start_date: NaiveDate::from_ymd_opt(2024, 1, 1).unwrap(),
            end_date: NaiveDate::from_ymd_opt(2024, 1, 31).unwrap(),
            ..sample_config()
        };

        let result = run_backtest(&[code_data], &timeline, &strategy, &config);

        assert_eq!(result.portfolio.closed_trades.len(), 1);
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
        let bars = vec![
            make_bar("BHP", "2024-01-01", 90.0),
            make_bar("BHP", "2024-01-02", 110.0),
            make_bar("BHP", "2024-01-03", 90.0),
        ];
        let code_data = make_code_data("BHP", bars);
        let timeline = build_unified_timeline(&[code_data.clone()]);
        let strategy = make_simple_strategy();
        let config = BacktestConfig {
            start_date: NaiveDate::from_ymd_opt(2024, 1, 1).unwrap(),
            end_date: NaiveDate::from_ymd_opt(2024, 1, 31).unwrap(),
            commission_per_trade: 10.0,
            ..sample_config()
        };

        let result = run_backtest(&[code_data], &timeline, &strategy, &config);

        assert_eq!(result.portfolio.closed_trades.len(), 1);
        assert!(result.portfolio.cash < 100_000.0);
    }
}
