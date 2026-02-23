//! Report generation port trait (TRD Section 2.2, 11.3).

use crate::domain::backtest::BacktestResult;
use crate::domain::error::SamtraderError;
use crate::domain::portfolio::{EquityPoint, Portfolio};
use crate::domain::strategy::Strategy;
use std::collections::HashMap;

pub trait ReportPort {
    fn write(
        &self,
        result: &BacktestResult,
        strategy: &Strategy,
        output_path: &str,
    ) -> Result<(), SamtraderError>;

    fn write_multi(
        &self,
        results: &[BacktestResult],
        strategy: &Strategy,
        output_path: &str,
    ) -> Result<(), SamtraderError> {
        let aggregated = aggregate_results(results);
        self.write(&aggregated, strategy, output_path)
    }
}

fn aggregate_results(results: &[BacktestResult]) -> BacktestResult {
    if results.is_empty() {
        return BacktestResult {
            portfolio: Portfolio::new(0.0),
        };
    }

    if results.len() == 1 {
        return results[0].clone();
    }

    let mut total_cash = 0.0;
    let mut total_initial_capital = 0.0;
    let mut all_positions = HashMap::new();
    let mut all_trades = Vec::new();
    let mut equity_by_date: HashMap<chrono::NaiveDate, f64> = HashMap::new();

    for result in results {
        let portfolio = &result.portfolio;
        total_cash += portfolio.cash;
        total_initial_capital += portfolio.initial_capital;

        for (code, pos) in &portfolio.positions {
            all_positions.insert(code.clone(), pos.clone());
        }

        all_trades.extend(portfolio.closed_trades.clone());

        for point in &portfolio.equity_curve {
            *equity_by_date.entry(point.date).or_insert(0.0) += point.equity;
        }
    }

    let mut equity_curve: Vec<EquityPoint> = equity_by_date
        .into_iter()
        .map(|(date, equity)| EquityPoint { date, equity })
        .collect();
    equity_curve.sort_by_key(|p| p.date);

    let mut portfolio = Portfolio::new(total_initial_capital);
    portfolio.cash = total_cash;
    portfolio.positions = all_positions;
    portfolio.closed_trades = all_trades;
    portfolio.equity_curve = equity_curve;

    BacktestResult { portfolio }
}
