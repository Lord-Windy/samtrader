//! Table formatting for reports (TRD Section 3.9).
//!
//! Generates Typst table markup for trade logs, monthly returns heatmap,
//! and universe summary tables.

use std::collections::BTreeMap;

use chrono::Datelike;

use crate::domain::backtest::BacktestResult;
use crate::domain::metrics::{CodeResult, Metrics};
use crate::domain::portfolio::EquityPoint;
use crate::domain::position::ClosedTrade;
use crate::domain::strategy::Strategy;

fn fmt_currency(value: f64) -> String {
    if value >= 0.0 {
        format!("\\${:.2}", value)
    } else {
        format!("-\\${:.2}", value.abs())
    }
}

/// Render the strategy summary as a Typst two-column key-value table.
pub fn render_strategy_summary(
    strategy: &Strategy,
    start_date: chrono::NaiveDate,
    end_date: chrono::NaiveDate,
    initial_capital: f64,
) -> String {
    let short_entry = strategy
        .entry_short
        .as_ref()
        .map(|r| r.to_string())
        .unwrap_or_else(|| "N/A".to_string());
    let short_exit = strategy
        .exit_short
        .as_ref()
        .map(|r| r.to_string())
        .unwrap_or_else(|| "N/A".to_string());

    format!(
        r#"#table(
  columns: (auto, 1fr),
  stroke: 0.5pt + luma(200),
  [*Property*], [*Value*],
  [Name], [{name}],
  [Description], [{desc}],
  [Entry Rule (Long)], [{entry_long}],
  [Exit Rule (Long)], [{exit_long}],
  [Entry Rule (Short)], [{entry_short}],
  [Exit Rule (Short)], [{exit_short}],
  [Position Size], [{pos_size:.1}%],
  [Stop Loss], [{stop:.1}%],
  [Take Profit], [{tp:.1}%],
  [Max Positions], [{max_pos}],
  [Start Date], [{start}],
  [End Date], [{end}],
  [Initial Capital], [{capital}],
)"#,
        name = strategy.name,
        desc = strategy.description,
        entry_long = strategy.entry_long,
        exit_long = strategy.exit_long,
        entry_short = short_entry,
        exit_short = short_exit,
        pos_size = strategy.position_size * 100.0,
        stop = strategy.stop_loss_pct,
        tp = strategy.take_profit_pct,
        max_pos = strategy.max_positions,
        start = start_date,
        end = end_date,
        capital = fmt_currency(initial_capital),
    )
}

/// Render aggregate performance metrics as a Typst two-column table.
pub fn render_metrics_table(m: &Metrics) -> String {
    let pf_display = if m.profit_factor.is_infinite() {
        "inf".to_string()
    } else {
        format!("{:.2}", m.profit_factor)
    };

    format!(
        r#"#table(
  columns: (auto, 1fr),
  stroke: 0.5pt + luma(200),
  [*Metric*], [*Value*],
  [Total Return], [{:.2}%],
  [Annualized Return], [{:.2}%],
  [Sharpe Ratio], [{:.2}],
  [Sortino Ratio], [{:.2}],
  [Max Drawdown], [{:.2}%],
  [Max Drawdown Duration], [{:.0} days],
  [Total Trades], [{}],
  [Winning Trades], [{}],
  [Losing Trades], [{}],
  [Break-Even Trades], [{}],
  [Win Rate], [{:.1}%],
  [Profit Factor], [{}],
  [Average Win], [{}],
  [Average Loss], [{}],
  [Largest Win], [{}],
  [Largest Loss], [{}],
  [Avg Trade Duration], [{:.1} days],
)"#,
        m.total_return * 100.0,
        m.annualized_return * 100.0,
        m.sharpe_ratio,
        m.sortino_ratio,
        m.max_drawdown * 100.0,
        m.max_drawdown_duration,
        m.total_trades,
        m.winning_trades,
        m.losing_trades,
        m.break_even_trades,
        m.win_rate * 100.0,
        pf_display,
        fmt_currency(m.average_win),
        fmt_currency(m.average_loss),
        fmt_currency(m.largest_win),
        fmt_currency(m.largest_loss),
        m.average_trade_duration,
    )
}

/// Render the universe summary table (multi-code only).
///
/// Returns empty string if `code_results` is empty.
pub fn render_universe_summary(code_results: &[CodeResult]) -> String {
    if code_results.is_empty() {
        return String::new();
    }

    let mut out = String::from(
        r#"== Universe Summary

#table(
  columns: (auto, auto, auto, auto, auto, auto),
  stroke: 0.5pt + luma(200),
  [*Code*], [*Trades*], [*Win Rate*], [*Total PnL*], [*Largest Win*], [*Largest Loss*],
"#,
    );

    for cr in code_results {
        out.push_str(&format!(
            "  [{}], [{}], [{:.1}%], [{}], [{}], [{}],\n",
            cr.code,
            cr.total_trades,
            cr.win_rate * 100.0,
            fmt_currency(cr.total_pnl),
            fmt_currency(cr.largest_win),
            fmt_currency(cr.largest_loss),
        ));
    }

    out.push(')');
    out
}

/// Render per-code detail sections with individual trade logs.
///
/// Returns empty string if `code_results` is empty.
pub fn render_per_code_sections(result: &BacktestResult, code_results: &[CodeResult]) -> String {
    if code_results.is_empty() {
        return String::new();
    }

    let mut out = String::new();

    for cr in code_results {
        out.push_str(&format!(
            r#"
== Per-Code Details: {}

#table(
  columns: (auto, 1fr),
  stroke: 0.5pt + luma(200),
  [*Property*], [*Value*],
  [Total Trades], [{}],
  [Win Rate], [{:.1}%],
  [Total PnL], [{}],
  [Largest Win], [{}],
  [Largest Loss], [{}],
)
"#,
            cr.code,
            cr.total_trades,
            cr.win_rate * 100.0,
            fmt_currency(cr.total_pnl),
            fmt_currency(cr.largest_win),
            fmt_currency(cr.largest_loss),
        ));

        let code_trades: Vec<&ClosedTrade> = result
            .portfolio
            .closed_trades
            .iter()
            .filter(|t| t.code == cr.code)
            .collect();

        if !code_trades.is_empty() {
            out.push_str(&format!("=== Trade Log for {}\n\n", cr.code));
            out.push_str(
                r#"#table(
  columns: (auto, auto, auto, auto, auto, auto, auto),
  stroke: 0.5pt + luma(200),
  [*\#*], [*Entry Date*], [*Exit Date*], [*Qty*], [*Entry Price*], [*Exit Price*], [*PnL*],
"#,
            );
            for (i, trade) in code_trades.iter().enumerate() {
                out.push_str(&format!(
                    "  [{}], [{}], [{}], [{}], [{}], [{}], [{}],\n",
                    i + 1,
                    trade.entry_date,
                    trade.exit_date,
                    trade.quantity,
                    fmt_currency(trade.entry_price),
                    fmt_currency(trade.exit_price),
                    fmt_currency(trade.pnl),
                ));
            }
            out.push_str(")\n");
        }
    }

    out
}

/// Render the full trade log as a Typst table.
pub fn render_trade_log(trades: &[ClosedTrade]) -> String {
    if trades.is_empty() {
        return String::from("_No trades executed._");
    }

    let mut out = String::from(
        r#"#table(
  columns: (auto, auto, auto, auto, auto, auto, auto, auto),
  stroke: 0.5pt + luma(200),
  [*\#*], [*Code*], [*Entry Date*], [*Exit Date*], [*Qty*], [*Entry Price*], [*Exit Price*], [*PnL*],
"#,
    );

    for (i, trade) in trades.iter().enumerate() {
        out.push_str(&format!(
            "  [{}], [{}], [{}], [{}], [{}], [{}], [{}], [{}],\n",
            i + 1,
            trade.code,
            trade.entry_date,
            trade.exit_date,
            trade.quantity,
            fmt_currency(trade.entry_price),
            fmt_currency(trade.exit_price),
            fmt_currency(trade.pnl),
        ));
    }

    out.push(')');
    out
}

/// Render monthly returns as a Typst heatmap grid (years as rows, months as columns).
///
/// Returns empty string if the equity curve has fewer than 2 points.
pub fn render_monthly_returns(equity_curve: &[EquityPoint]) -> String {
    if equity_curve.len() < 2 {
        return String::new();
    }

    // Accumulate daily returns into year-month buckets using log returns.
    let mut monthly_data: BTreeMap<(i32, u32), Vec<f64>> = BTreeMap::new();

    for window in equity_curve.windows(2) {
        let prev = &window[0];
        let curr = &window[1];
        let return_rate = if prev.equity > 0.0 {
            (curr.equity - prev.equity) / prev.equity
        } else {
            0.0
        };
        let key = (curr.date.year(), curr.date.month());
        monthly_data.entry(key).or_default().push(return_rate);
    }

    // Compute compounded monthly return per bucket.
    let mut returns: BTreeMap<(i32, u32), f64> = BTreeMap::new();
    for (&key, daily) in &monthly_data {
        let compounded = daily.iter().map(|r| (1.0 + r).ln()).sum::<f64>().exp() - 1.0;
        returns.insert(key, compounded);
    }

    // Determine year range.
    let min_year = returns.keys().map(|k| k.0).min().unwrap();
    let max_year = returns.keys().map(|k| k.0).max().unwrap();

    // Build the heatmap grid: 13 columns (Year + Jan..Dec).
    let mut out = String::from(
        r#"#table(
  columns: (auto, auto, auto, auto, auto, auto, auto, auto, auto, auto, auto, auto, auto),
  stroke: 0.5pt + luma(200),
  [*Year*], [*Jan*], [*Feb*], [*Mar*], [*Apr*], [*May*], [*Jun*], [*Jul*], [*Aug*], [*Sep*], [*Oct*], [*Nov*], [*Dec*],
"#,
    );

    for year in min_year..=max_year {
        out.push_str(&format!("  [{}],", year));
        for month in 1..=12u32 {
            if let Some(&ret) = returns.get(&(year, month)) {
                let pct = ret * 100.0;
                let color = if pct > 0.0 {
                    "green"
                } else if pct < 0.0 {
                    "red"
                } else {
                    "black"
                };
                out.push_str(&format!(" [#text(fill: {color})[{pct:.1}%]],",));
            } else {
                out.push_str(" [],");
            }
        }
        out.push('\n');
    }

    out.push(')');
    out
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::domain::rule::{Operand, Rule};
    use chrono::NaiveDate;

    fn sample_strategy() -> Strategy {
        Strategy {
            name: "Test Strategy".into(),
            description: "A test strategy".into(),
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
            stop_loss_pct: 5.0,
            take_profit_pct: 10.0,
            max_positions: 5,
        }
    }

    #[test]
    fn strategy_summary_contains_typst_table() {
        let s = sample_strategy();
        let out = render_strategy_summary(
            &s,
            NaiveDate::from_ymd_opt(2020, 1, 1).unwrap(),
            NaiveDate::from_ymd_opt(2024, 12, 31).unwrap(),
            100_000.0,
        );
        assert!(out.contains("#table("));
        assert!(out.contains("Test Strategy"));
        assert!(out.contains("Entry Rule (Long)"));
        assert!(out.contains("close above 100"));
        assert!(out.contains("Position Size"));
        assert!(out.contains("\\$100000.00"));
    }

    #[test]
    fn metrics_table_contains_typst_table() {
        let m = Metrics {
            total_return: 0.25,
            annualized_return: 0.10,
            sharpe_ratio: 1.5,
            sortino_ratio: 2.0,
            max_drawdown: 0.15,
            max_drawdown_duration: 30.0,
            total_trades: 20,
            winning_trades: 12,
            losing_trades: 6,
            break_even_trades: 2,
            win_rate: 0.60,
            profit_factor: 2.5,
            average_win: 500.0,
            average_loss: -200.0,
            largest_win: 1000.0,
            largest_loss: -500.0,
            average_trade_duration: 5.0,
        };
        let out = render_metrics_table(&m);
        assert!(out.contains("#table("));
        assert!(out.contains("25.00%"));
        assert!(out.contains("1.50"));
    }

    #[test]
    fn trade_log_empty() {
        let out = render_trade_log(&[]);
        assert!(out.contains("No trades executed"));
    }

    #[test]
    fn trade_log_with_trades() {
        let trades = vec![ClosedTrade {
            code: "BHP".into(),
            exchange: "ASX".into(),
            quantity: 100,
            entry_price: 50.0,
            exit_price: 55.0,
            entry_date: NaiveDate::from_ymd_opt(2024, 1, 1).unwrap(),
            exit_date: NaiveDate::from_ymd_opt(2024, 1, 5).unwrap(),
            pnl: 500.0,
        }];
        let out = render_trade_log(&trades);
        assert!(out.contains("#table("));
        assert!(out.contains("BHP"));
        assert!(out.contains("\\$500.00"));
    }

    #[test]
    fn universe_summary_empty() {
        assert!(render_universe_summary(&[]).is_empty());
    }

    #[test]
    fn universe_summary_has_codes() {
        let code_results = vec![CodeResult {
            code: "BHP".into(),
            exchange: "ASX".into(),
            total_trades: 10,
            winning_trades: 6,
            losing_trades: 4,
            total_pnl: 5000.0,
            win_rate: 0.6,
            largest_win: 1000.0,
            largest_loss: -500.0,
        }];
        let out = render_universe_summary(&code_results);
        assert!(out.contains("#table("));
        assert!(out.contains("BHP"));
        assert!(out.contains("Universe Summary"));
    }

    #[test]
    fn monthly_returns_heatmap_grid() {
        let curve = vec![
            EquityPoint {
                date: NaiveDate::from_ymd_opt(2024, 1, 1).unwrap(),
                equity: 100_000.0,
            },
            EquityPoint {
                date: NaiveDate::from_ymd_opt(2024, 1, 15).unwrap(),
                equity: 102_000.0,
            },
            EquityPoint {
                date: NaiveDate::from_ymd_opt(2024, 2, 1).unwrap(),
                equity: 101_000.0,
            },
            EquityPoint {
                date: NaiveDate::from_ymd_opt(2024, 2, 15).unwrap(),
                equity: 103_000.0,
            },
        ];
        let out = render_monthly_returns(&curve);
        assert!(out.contains("#table("));
        assert!(out.contains("[*Year*]"));
        assert!(out.contains("[*Jan*]"));
        assert!(out.contains("[*Dec*]"));
        assert!(out.contains("2024"));
        // Check color coding present
        assert!(out.contains("#text(fill:"));
    }

    #[test]
    fn monthly_returns_too_short() {
        let curve = vec![EquityPoint {
            date: NaiveDate::from_ymd_opt(2024, 1, 1).unwrap(),
            equity: 100_000.0,
        }];
        assert!(render_monthly_returns(&curve).is_empty());
    }
}
