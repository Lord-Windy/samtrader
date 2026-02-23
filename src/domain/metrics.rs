//! Performance metrics and statistics (TRD Section 3.7, §10).

use super::portfolio::{EquityPoint, Portfolio};
use chrono::NaiveDate;

const TRADING_DAYS_PER_YEAR: f64 = 252.0;

#[derive(Debug, Clone, PartialEq)]
pub struct Metrics {
    pub total_return: f64,
    pub annualized_return: f64,
    pub sharpe_ratio: f64,
    pub sortino_ratio: f64,
    pub max_drawdown: f64,
    pub max_drawdown_duration: i64,
    pub trades_won: usize,
    pub trades_lost: usize,
    pub trades_breakeven: usize,
    pub win_rate: f64,
    pub profit_factor: f64,
    pub avg_win: f64,
    pub avg_loss: f64,
    pub largest_win: f64,
    pub largest_loss: f64,
    pub avg_trade_duration: f64,
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

        let trading_days = equity_curve.len() as f64;
        let years = trading_days / TRADING_DAYS_PER_YEAR;
        let annualized_return = if years > 0.0 && total_return.is_finite() {
            (1.0 + total_return).powf(1.0 / years) - 1.0
        } else {
            0.0
        };

        let (max_drawdown, max_drawdown_duration) = compute_drawdown(equity_curve);

        let daily_rf = risk_free_rate / TRADING_DAYS_PER_YEAR;
        let (sharpe_ratio, sortino_ratio) = compute_risk_adjusted(equity_curve, daily_rf);

        let mut trades_won = 0usize;
        let mut trades_lost = 0usize;
        let mut trades_breakeven = 0usize;
        let mut total_wins = 0.0_f64;
        let mut total_losses = 0.0_f64;
        let mut largest_win = 0.0_f64;
        let mut largest_loss = 0.0_f64;
        let mut total_duration_days = 0i64;

        for trade in trades {
            let pnl = trade.pnl;
            if pnl > 0.0 {
                trades_won += 1;
                total_wins += pnl;
                if pnl > largest_win {
                    largest_win = pnl;
                }
            } else if pnl < 0.0 {
                trades_lost += 1;
                total_losses += pnl.abs();
                if pnl.abs() > largest_loss {
                    largest_loss = pnl.abs();
                }
            } else {
                trades_breakeven += 1;
            }

            let duration = (trade.exit_date - trade.entry_date).num_days();
            total_duration_days += duration;
        }

        let total_trades = trades_won + trades_lost + trades_breakeven;
        let win_rate = if total_trades > 0 {
            trades_won as f64 / total_trades as f64
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

        let avg_win = if trades_won > 0 {
            total_wins / trades_won as f64
        } else {
            0.0
        };

        let avg_loss = if trades_lost > 0 {
            total_losses / trades_lost as f64
        } else {
            0.0
        };

        let avg_trade_duration = if total_trades > 0 {
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
            trades_won,
            trades_lost,
            trades_breakeven,
            win_rate,
            profit_factor,
            avg_win,
            avg_loss,
            largest_win,
            largest_loss,
            avg_trade_duration,
        }
    }
}

fn compute_drawdown(equity_curve: &[EquityPoint]) -> (f64, i64) {
    if equity_curve.is_empty() {
        return (0.0, 0);
    }

    let mut peak = equity_curve[0].equity;
    let mut max_dd = 0.0_f64;
    let mut max_dd_duration = 0i64;
    let mut dd_start: Option<NaiveDate> = None;
    let mut current_dd_duration = 0i64;

    for point in equity_curve {
        if point.equity > peak {
            peak = point.equity;
            dd_start = None;
            current_dd_duration = 0;
        } else if peak > 0.0 {
            let dd = (peak - point.equity) / peak;
            if dd > max_dd {
                max_dd = dd;
            }
            if dd_start.is_none() {
                dd_start = Some(point.date);
            }
            current_dd_duration += 1;
            if current_dd_duration > max_dd_duration {
                max_dd_duration = current_dd_duration;
            }
        }
    }

    (max_dd, max_dd_duration)
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
        assert_eq!(metrics.trades_won, 0);
        assert_eq!(metrics.trades_lost, 0);
        assert_eq!(metrics.trades_breakeven, 0);
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
    fn metrics_annualized_return() {
        let mut values = vec![100_000.0];
        for _ in 0..251 {
            values.push(100_000.0);
        }
        let portfolio = make_portfolio(values, vec![]);
        let metrics = Metrics::compute(&portfolio, 0.05);
        assert!((metrics.annualized_return - 0.0).abs() < 1e-9);
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

        assert_eq!(metrics.trades_won, 2);
        assert_eq!(metrics.trades_lost, 1);
        assert_eq!(metrics.trades_breakeven, 1);
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
    fn metrics_avg_win_and_loss() {
        let trades = vec![
            make_trade("A", 100.0, 5),
            make_trade("B", -60.0, 3),
            make_trade("C", 200.0, 10),
            make_trade("D", -40.0, 2),
        ];
        let portfolio = make_portfolio(vec![100_000.0, 100_200.0], trades);
        let metrics = Metrics::compute(&portfolio, 0.05);

        assert!((metrics.avg_win - 150.0).abs() < 1e-9);
        assert!((metrics.avg_loss - 50.0).abs() < 1e-9);
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
        assert!((metrics.largest_loss - 150.0).abs() < 1e-9);
    }

    #[test]
    fn metrics_avg_trade_duration() {
        let trades = vec![
            make_trade("A", 100.0, 5),
            make_trade("B", -50.0, 10),
            make_trade("C", 200.0, 15),
        ];
        let portfolio = make_portfolio(vec![100_000.0, 100_250.0], trades);
        let metrics = Metrics::compute(&portfolio, 0.05);

        assert!((metrics.avg_trade_duration - 10.0).abs() < 1e-9);
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

        assert_eq!(duration, 4);
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

        assert_eq!(metrics.trades_won, 0);
        assert_eq!(metrics.trades_lost, 0);
        assert_eq!(metrics.trades_breakeven, 0);
        assert!((metrics.win_rate - 0.0).abs() < f64::EPSILON);
        assert!((metrics.profit_factor - 0.0).abs() < f64::EPSILON);
        assert!((metrics.avg_win - 0.0).abs() < f64::EPSILON);
        assert!((metrics.avg_loss - 0.0).abs() < f64::EPSILON);
        assert!((metrics.largest_win - 0.0).abs() < f64::EPSILON);
        assert!((metrics.largest_loss - 0.0).abs() < f64::EPSILON);
        assert!((metrics.avg_trade_duration - 0.0).abs() < f64::EPSILON);
    }
}
