//! Performance metrics and statistics (TRD Section 3.7, §10).

use super::portfolio::{EquityPoint, Portfolio};
use super::position::ClosedTrade;

const TRADING_DAYS_PER_YEAR: f64 = 252.0;

#[derive(Debug, Clone, PartialEq)]
pub struct CodeResult {
    pub code: String,
    pub exchange: String,
    pub total_trades: usize,
    pub winning_trades: usize,
    pub losing_trades: usize,
    pub total_pnl: f64,
    pub win_rate: f64,
    pub largest_win: f64,
    pub largest_loss: f64,
}

impl CodeResult {
    pub fn compute_per_code(trades: &[ClosedTrade]) -> Vec<Self> {
        let mut results: std::collections::HashMap<String, CodeResult> =
            std::collections::HashMap::new();

        for trade in trades {
            let entry = results.entry(trade.code.clone()).or_insert(CodeResult {
                code: trade.code.clone(),
                exchange: trade.exchange.clone(),
                total_trades: 0,
                winning_trades: 0,
                losing_trades: 0,
                total_pnl: 0.0,
                win_rate: 0.0,
                largest_win: 0.0,
                largest_loss: 0.0,
            });

            entry.total_trades += 1;
            entry.total_pnl += trade.pnl;

            if trade.pnl > 0.0 {
                entry.winning_trades += 1;
                if trade.pnl > entry.largest_win {
                    entry.largest_win = trade.pnl;
                }
            } else if trade.pnl < 0.0 {
                entry.losing_trades += 1;
                if trade.pnl < entry.largest_loss {
                    entry.largest_loss = trade.pnl;
                }
            }
        }

        let mut results: Vec<CodeResult> = results.into_values().collect();
        for r in &mut results {
            if r.total_trades > 0 {
                r.win_rate = r.winning_trades as f64 / r.total_trades as f64;
            }
        }
        results.sort_by(|a, b| a.code.cmp(&b.code));
        results
    }
}

#[derive(Debug, Clone, PartialEq)]
pub struct Metrics {
    pub total_return: f64,
    pub annualized_return: f64,
    pub sharpe_ratio: f64,
    pub sortino_ratio: f64,
    pub max_drawdown: f64,
    pub max_drawdown_duration: f64,
    pub total_trades: usize,
    pub winning_trades: usize,
    pub losing_trades: usize,
    pub break_even_trades: usize,
    pub win_rate: f64,
    pub profit_factor: f64,
    pub average_win: f64,
    pub average_loss: f64,
    pub largest_win: f64,
    pub largest_loss: f64,
    pub average_trade_duration: f64,
}

impl Metrics {
    pub fn compute(portfolio: &Portfolio, risk_free_rate: f64) -> Self {
        let equity_curve = &portfolio.equity_curve;
        let trades = &portfolio.closed_trades;
        let initial_capital = portfolio.initial_capital;

        let final_equity = equity_curve
            .last()
            .map(|p| p.equity)
            .unwrap_or(initial_capital);

        let total_return = if initial_capital > 0.0 {
            (final_equity - initial_capital) / initial_capital
        } else {
            0.0
        };

        let trading_days = if equity_curve.len() > 1 {
            (equity_curve.len() - 1) as f64
        } else {
            0.0
        };
        let annualized_return = if trading_days > 0.0 && total_return.is_finite() {
            (1.0 + total_return).powf(TRADING_DAYS_PER_YEAR / trading_days) - 1.0
        } else {
            0.0
        };

        let (max_drawdown, max_drawdown_duration) = compute_drawdown(equity_curve);

        let daily_rf = risk_free_rate / TRADING_DAYS_PER_YEAR;
        let (sharpe_ratio, sortino_ratio) = compute_risk_adjusted(equity_curve, daily_rf);

        let mut winning_trades = 0usize;
        let mut losing_trades = 0usize;
        let mut break_even_trades = 0usize;
        let mut total_wins = 0.0_f64;
        let mut total_losses = 0.0_f64;
        let mut largest_win = 0.0_f64;
        let mut largest_loss = 0.0_f64;
        let mut total_duration_days = 0i64;

        for trade in trades {
            let pnl = trade.pnl;
            if pnl > 0.0 {
                winning_trades += 1;
                total_wins += pnl;
                if pnl > largest_win {
                    largest_win = pnl;
                }
            } else if pnl < 0.0 {
                losing_trades += 1;
                total_losses += pnl.abs();
                if pnl < largest_loss {
                    largest_loss = pnl;
                }
            } else {
                break_even_trades += 1;
            }

            let duration = (trade.exit_date - trade.entry_date).num_days();
            total_duration_days += duration;
        }

        let total_trades = winning_trades + losing_trades + break_even_trades;
        let win_rate = if total_trades > 0 {
            winning_trades as f64 / total_trades as f64
        } else {
            0.0
        };

        let profit_factor = if total_losses > 0.0 {
            total_wins / total_losses
        } else if total_wins > 0.0 {
            f64::INFINITY
        } else {
            0.0
        };

        let average_win = if winning_trades > 0 {
            total_wins / winning_trades as f64
        } else {
            0.0
        };

        let average_loss = if losing_trades > 0 {
            -(total_losses / losing_trades as f64)
        } else {
            0.0
        };

        let average_trade_duration = if total_trades > 0 {
            total_duration_days as f64 / total_trades as f64
        } else {
            0.0
        };

        Metrics {
            total_return,
            annualized_return,
            sharpe_ratio,
            sortino_ratio,
            max_drawdown,
            max_drawdown_duration,
            total_trades,
            winning_trades,
            losing_trades,
            break_even_trades,
            win_rate,
            profit_factor,
            average_win,
            average_loss,
            largest_win,
            largest_loss,
            average_trade_duration,
        }
    }
}

fn compute_drawdown(equity_curve: &[EquityPoint]) -> (f64, f64) {
    if equity_curve.is_empty() {
        return (0.0, 0.0);
    }

    let mut peak = equity_curve[0].equity;
    let mut max_dd = 0.0_f64;
    let mut max_dd_duration = 0_i64;
    let mut current_dd_duration = 0_i64;

    for point in equity_curve {
        if point.equity > peak {
            peak = point.equity;
            current_dd_duration = 0;
        } else if peak > 0.0 {
            let dd = (peak - point.equity) / peak;
            if dd > max_dd {
                max_dd = dd;
            }
            current_dd_duration += 1;
            if current_dd_duration > max_dd_duration {
                max_dd_duration = current_dd_duration;
            }
        }
    }

    (max_dd, max_dd_duration as f64)
}

fn compute_risk_adjusted(equity_curve: &[EquityPoint], daily_rf: f64) -> (f64, f64) {
    if equity_curve.len() < 2 {
        return (0.0, 0.0);
    }

    let returns: Vec<f64> = equity_curve
        .windows(2)
        .map(|w| {
            let prev = w[0].equity;
            let curr = w[1].equity;
            if prev > 0.0 {
                (curr - prev) / prev
            } else {
                0.0
            }
        })
        .collect();

    if returns.is_empty() {
        return (0.0, 0.0);
    }

    let n = returns.len() as f64;
    let mean: f64 = returns.iter().sum::<f64>() / n;

    let variance: f64 = returns.iter().map(|r| (r - mean).powi(2)).sum::<f64>() / n;
    let stddev = variance.sqrt();

    let excess_return = mean - daily_rf;

    let sharpe = if stddev > 0.0 {
        (excess_return / stddev) * TRADING_DAYS_PER_YEAR.sqrt()
    } else {
        0.0
    };

    let downside_returns: Vec<f64> = returns
        .iter()
        .filter(|&&r| r < daily_rf)
        .map(|&r| (r - daily_rf).powi(2))
        .collect();

    let downside_stddev = if !downside_returns.is_empty() {
        let ds_variance: f64 = downside_returns.iter().sum::<f64>() / n;
        ds_variance.sqrt()
    } else {
        0.0
    };

    let sortino = if downside_stddev > 0.0 {
        (excess_return / downside_stddev) * TRADING_DAYS_PER_YEAR.sqrt()
    } else {
        0.0
    };

    (sharpe, sortino)
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::domain::position::ClosedTrade;
    use chrono::NaiveDate;

    fn make_equity_curve(values: &[f64]) -> Vec<EquityPoint> {
        values
            .iter()
            .enumerate()
            .map(|(i, &v)| EquityPoint {
                date: NaiveDate::from_ymd_opt(2024, 1, 1).unwrap()
                    + chrono::Duration::days(i as i64),
                equity: v,
            })
            .collect()
    }

    fn make_portfolio(equity: Vec<f64>, trades: Vec<ClosedTrade>) -> Portfolio {
        let initial = equity.first().copied().unwrap_or(100_000.0);
        let mut portfolio = Portfolio::new(initial);
        for trade in trades {
            portfolio.record_trade(trade);
        }
        for point in make_equity_curve(&equity) {
            portfolio.record_equity(point.date, point.equity);
        }
        portfolio
    }

    fn make_trade(code: &str, pnl: f64, days: i64) -> ClosedTrade {
        let entry_date = NaiveDate::from_ymd_opt(2024, 1, 1).unwrap();
        ClosedTrade {
            code: code.to_string(),
            exchange: "ASX".to_string(),
            quantity: 100,
            entry_price: 100.0,
            exit_price: 100.0 + pnl / 100.0,
            entry_date,
            exit_date: entry_date + chrono::Duration::days(days),
            pnl,
        }
    }

    #[test]
    fn metrics_empty_portfolio() {
        let portfolio = Portfolio::new(100_000.0);
        let metrics = Metrics::compute(&portfolio, 0.05);
        assert!((metrics.total_return - 0.0).abs() < f64::EPSILON);
        assert_eq!(metrics.total_trades, 0);
        assert_eq!(metrics.winning_trades, 0);
        assert_eq!(metrics.losing_trades, 0);
        assert_eq!(metrics.break_even_trades, 0);
    }

    #[test]
    fn metrics_total_return_positive() {
        let portfolio = make_portfolio(vec![100_000.0, 110_000.0], vec![]);
        let metrics = Metrics::compute(&portfolio, 0.05);
        assert!((metrics.total_return - 0.10).abs() < 1e-9);
    }

    #[test]
    fn metrics_total_return_negative() {
        let portfolio = make_portfolio(vec![100_000.0, 90_000.0], vec![]);
        let metrics = Metrics::compute(&portfolio, 0.05);
        assert!((metrics.total_return - (-0.10)).abs() < 1e-9);
    }

    #[test]
    fn metrics_annualized_return_flat() {
        // 252 points = 251 trading days = 1 year, flat equity => 0% annualized
        let mut values = vec![100_000.0];
        for _ in 0..251 {
            values.push(100_000.0);
        }
        let portfolio = make_portfolio(values, vec![]);
        let metrics = Metrics::compute(&portfolio, 0.05);
        assert!((metrics.annualized_return - 0.0).abs() < 1e-9);
    }

    #[test]
    fn metrics_annualized_return_with_growth() {
        // 10% return over 252 points (251 trading days = 1 year)
        // annualized = (1.10)^(252/251) - 1 ≈ 0.10039...
        let mut values = Vec::new();
        for i in 0..252 {
            values.push(100_000.0 + 10_000.0 * (i as f64) / 251.0);
        }
        let portfolio = make_portfolio(values, vec![]);
        let metrics = Metrics::compute(&portfolio, 0.0);
        let expected = (1.10_f64).powf(252.0 / 251.0) - 1.0;
        assert!((metrics.annualized_return - expected).abs() < 1e-9);
    }

    #[test]
    fn metrics_trade_stats_wins_and_losses() {
        let trades = vec![
            make_trade("A", 100.0, 5),
            make_trade("B", -50.0, 3),
            make_trade("C", 200.0, 10),
            make_trade("D", 0.0, 1),
        ];
        let portfolio = make_portfolio(vec![100_000.0, 100_250.0], trades);
        let metrics = Metrics::compute(&portfolio, 0.05);

        assert_eq!(metrics.total_trades, 4);
        assert_eq!(metrics.winning_trades, 2);
        assert_eq!(metrics.losing_trades, 1);
        assert_eq!(metrics.break_even_trades, 1);
        assert!((metrics.win_rate - 0.5).abs() < f64::EPSILON);
    }

    #[test]
    fn metrics_profit_factor() {
        let trades = vec![
            make_trade("A", 100.0, 5),
            make_trade("B", -50.0, 3),
            make_trade("C", 200.0, 10),
        ];
        let portfolio = make_portfolio(vec![100_000.0, 100_250.0], trades);
        let metrics = Metrics::compute(&portfolio, 0.05);

        assert!((metrics.profit_factor - 6.0).abs() < 1e-9);
    }

    #[test]
    fn metrics_profit_factor_no_losses() {
        let trades = vec![make_trade("A", 100.0, 5), make_trade("B", 200.0, 3)];
        let portfolio = make_portfolio(vec![100_000.0, 100_300.0], trades);
        let metrics = Metrics::compute(&portfolio, 0.05);

        assert!(metrics.profit_factor.is_infinite());
    }

    #[test]
    fn metrics_average_win_and_loss() {
        let trades = vec![
            make_trade("A", 100.0, 5),
            make_trade("B", -60.0, 3),
            make_trade("C", 200.0, 10),
            make_trade("D", -40.0, 2),
        ];
        let portfolio = make_portfolio(vec![100_000.0, 100_200.0], trades);
        let metrics = Metrics::compute(&portfolio, 0.05);

        assert!((metrics.average_win - 150.0).abs() < 1e-9);
        assert!((metrics.average_loss - (-50.0)).abs() < 1e-9);
    }

    #[test]
    fn metrics_largest_win_and_loss() {
        let trades = vec![
            make_trade("A", 100.0, 5),
            make_trade("B", 300.0, 3),
            make_trade("C", -50.0, 10),
            make_trade("D", -150.0, 2),
        ];
        let portfolio = make_portfolio(vec![100_000.0, 100_200.0], trades);
        let metrics = Metrics::compute(&portfolio, 0.05);

        assert!((metrics.largest_win - 300.0).abs() < 1e-9);
        assert!((metrics.largest_loss - (-150.0)).abs() < 1e-9);
    }

    #[test]
    fn metrics_average_trade_duration() {
        let trades = vec![
            make_trade("A", 100.0, 5),
            make_trade("B", -50.0, 10),
            make_trade("C", 200.0, 15),
        ];
        let portfolio = make_portfolio(vec![100_000.0, 100_250.0], trades);
        let metrics = Metrics::compute(&portfolio, 0.05);

        assert!((metrics.average_trade_duration - 10.0).abs() < 1e-9);
    }

    #[test]
    fn metrics_max_drawdown() {
        let equity = vec![100.0, 110.0, 90.0, 95.0, 80.0, 100.0];
        let curve = make_equity_curve(&equity);
        let (dd, _) = compute_drawdown(&curve);

        assert!((dd - (110.0 - 80.0) / 110.0).abs() < 1e-9);
    }

    #[test]
    fn metrics_max_drawdown_duration() {
        let equity = vec![100.0, 110.0, 100.0, 90.0, 85.0, 95.0];
        let curve = make_equity_curve(&equity);
        let (_, duration) = compute_drawdown(&curve);

        assert!((duration - 4.0).abs() < f64::EPSILON);
    }

    #[test]
    fn metrics_sharpe_ratio_positive() {
        let mut values = vec![100_000.0];
        for i in 1..253 {
            values.push(100_000.0 * (1.0 + 0.001 * (i as f64)));
        }
        let portfolio = make_portfolio(values, vec![]);
        let metrics = Metrics::compute(&portfolio, 0.0);

        assert!(metrics.sharpe_ratio > 0.0);
    }

    #[test]
    fn metrics_sortino_ratio() {
        let equity = vec![100.0, 101.0, 100.5, 101.5, 100.0, 102.0];
        let curve = make_equity_curve(&equity);
        let (sharpe, sortino) = compute_risk_adjusted(&curve, 0.0);

        assert!(sharpe.is_finite());
        assert!(sortino.is_finite());
    }

    #[test]
    fn metrics_no_trades() {
        let portfolio = make_portfolio(vec![100_000.0, 110_000.0], vec![]);
        let metrics = Metrics::compute(&portfolio, 0.05);

        assert_eq!(metrics.total_trades, 0);
        assert_eq!(metrics.winning_trades, 0);
        assert_eq!(metrics.losing_trades, 0);
        assert_eq!(metrics.break_even_trades, 0);
        assert!((metrics.win_rate - 0.0).abs() < f64::EPSILON);
        assert!((metrics.profit_factor - 0.0).abs() < f64::EPSILON);
        assert!((metrics.average_win - 0.0).abs() < f64::EPSILON);
        assert!((metrics.average_loss - 0.0).abs() < f64::EPSILON);
        assert!((metrics.largest_win - 0.0).abs() < f64::EPSILON);
        assert!((metrics.largest_loss - 0.0).abs() < f64::EPSILON);
        assert!((metrics.average_trade_duration - 0.0).abs() < f64::EPSILON);
    }

    #[test]
    fn code_result_empty_trades() {
        let results = CodeResult::compute_per_code(&[]);
        assert!(results.is_empty());
    }

    #[test]
    fn code_result_single_code() {
        let trades = vec![
            make_trade("BHP", 100.0, 5),
            make_trade("BHP", -50.0, 3),
            make_trade("BHP", 200.0, 10),
        ];
        let results = CodeResult::compute_per_code(&trades);

        assert_eq!(results.len(), 1);
        let r = &results[0];
        assert_eq!(r.code, "BHP");
        assert_eq!(r.total_trades, 3);
        assert_eq!(r.winning_trades, 2);
        assert_eq!(r.losing_trades, 1);
        assert!((r.total_pnl - 250.0).abs() < 1e-9);
        assert!((r.win_rate - 2.0 / 3.0).abs() < 1e-9);
        assert!((r.largest_win - 200.0).abs() < 1e-9);
        assert!((r.largest_loss - (-50.0)).abs() < 1e-9);
    }

    #[test]
    fn code_result_multiple_codes() {
        let trades = vec![
            make_trade("BHP", 100.0, 5),
            make_trade("CBA", -50.0, 3),
            make_trade("BHP", 200.0, 10),
            make_trade("RIO", 150.0, 7),
        ];
        let results = CodeResult::compute_per_code(&trades);

        assert_eq!(results.len(), 3);

        let bhp = results.iter().find(|r| r.code == "BHP").unwrap();
        assert_eq!(bhp.total_trades, 2);
        assert_eq!(bhp.winning_trades, 2);
        assert!((bhp.total_pnl - 300.0).abs() < 1e-9);
        assert!((bhp.win_rate - 1.0).abs() < 1e-9);

        let cba = results.iter().find(|r| r.code == "CBA").unwrap();
        assert_eq!(cba.total_trades, 1);
        assert_eq!(cba.losing_trades, 1);
        assert!((cba.win_rate - 0.0).abs() < 1e-9);

        let rio = results.iter().find(|r| r.code == "RIO").unwrap();
        assert_eq!(rio.total_trades, 1);
        assert_eq!(rio.winning_trades, 1);
    }

    #[test]
    fn code_result_sorted_by_code() {
        let trades = vec![
            make_trade("RIO", 100.0, 5),
            make_trade("BHP", 100.0, 5),
            make_trade("CBA", 100.0, 5),
        ];
        let results = CodeResult::compute_per_code(&trades);

        assert_eq!(results.len(), 3);
        assert_eq!(results[0].code, "BHP");
        assert_eq!(results[1].code, "CBA");
        assert_eq!(results[2].code, "RIO");
    }

    #[test]
    fn code_result_zero_pnl_is_neither_win_nor_loss() {
        let trades = vec![
            make_trade("BHP", 100.0, 5),
            make_trade("BHP", 0.0, 3),
            make_trade("BHP", -50.0, 2),
        ];
        let results = CodeResult::compute_per_code(&trades);

        assert_eq!(results.len(), 1);
        let r = &results[0];
        assert_eq!(r.total_trades, 3);
        assert_eq!(r.winning_trades, 1);
        assert_eq!(r.losing_trades, 1);
        assert!((r.total_pnl - 50.0).abs() < 1e-9);
        assert!((r.win_rate - 1.0 / 3.0).abs() < 1e-9);
    }
}
