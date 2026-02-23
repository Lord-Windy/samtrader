//! Table formatting for reports (TRD §12.1).
//!
//! Provides functions to generate Typst markup for:
//! - Monthly/yearly returns heatmap grid
//! - Trade log table (all codes sorted by date)
//! - Universe summary table (per-code stats)

use crate::domain::metrics::CodeResult;
use crate::domain::portfolio::EquityPoint;
use crate::domain::position::ClosedTrade;
use chrono::Datelike;
use std::collections::BTreeMap;

pub struct MonthlyReturns {
    pub year: i32,
    pub month: u32,
    pub return_pct: f64,
}

pub fn compute_monthly_returns(equity_curve: &[EquityPoint]) -> Vec<MonthlyReturns> {
    if equity_curve.is_empty() {
        return Vec::new();
    }

    let mut monthly_data: BTreeMap<(i32, u32), (f64, f64)> = BTreeMap::new();

    for point in equity_curve {
        let key = (point.date.year(), point.date.month());
        let entry = monthly_data
            .entry(key)
            .or_insert((point.equity, point.equity));
        entry.1 = point.equity;
    }

    let mut sorted_months: Vec<(i32, u32)> = monthly_data.keys().cloned().collect();
    sorted_months.sort();

    let mut returns = Vec::new();
    let mut prev_end_equity: Option<f64> = None;

    for (year, month) in sorted_months {
        let (_start_eq, end_eq) = monthly_data[&(year, month)];

        let return_pct = if let Some(prev) = prev_end_equity {
            if prev > 0.0 {
                (end_eq - prev) / prev
            } else {
                0.0
            }
        } else {
            // First month: no prior reference point, return is 0.
            0.0
        };

        returns.push(MonthlyReturns {
            year,
            month,
            return_pct,
        });

        prev_end_equity = Some(end_eq);
    }

    returns
}

pub fn format_returns_heatmap(returns: &[MonthlyReturns]) -> String {
    if returns.is_empty() {
        return "// No returns data available\n".to_string();
    }

    let mut years: BTreeMap<i32, [Option<f64>; 12]> = BTreeMap::new();

    for r in returns {
        let entry = years.entry(r.year).or_insert([None; 12]);
        entry[(r.month - 1) as usize] = Some(r.return_pct);
    }

    let mut output = String::new();
    output.push_str("#table(\n");
    output.push_str("  columns: 14,\n");
    output.push_str("  [*Year*], [*Jan*], [*Feb*], [*Mar*], [*Apr*], [*May*], [*Jun*], ");
    output.push_str("[*Jul*], [*Aug*], [*Sep*], [*Oct*], [*Nov*], [*Dec*], [*YTD*],\n");

    for (year, monthly) in years.iter() {
        output.push_str(&format!("  [{}],", year));

        let mut ytd = 1.0_f64;
        for &opt_ret in monthly.iter() {
            if let Some(ret) = opt_ret {
                ytd *= 1.0 + ret;
                output.push_str(&format!(" {}, ", format_heatmap_cell(ret)));
            } else {
                output.push_str(" [-],");
            }
        }
        let ytd_ret = ytd - 1.0;
        output.push_str(&format!(" {},\n", format_heatmap_cell(ytd_ret)));
    }

    output.push_str(")\n\n");
    output
}

/// Returns (fill_color, needs_white_text) for a given return value.
fn return_color(ret: f64) -> (&'static str, bool) {
    if ret >= 0.10 {
        ("rgb(\"#006400\")", true)
    } else if ret >= 0.05 {
        ("rgb(\"#228B22\")", true)
    } else if ret >= 0.02 {
        ("rgb(\"#90EE90\")", false)
    } else if ret > 0.0 {
        ("rgb(\"#E0FFE0\")", false)
    } else if ret == 0.0 {
        ("rgb(\"#FFFFFF\")", false)
    } else if ret > -0.02 {
        ("rgb(\"#FFE0E0\")", false)
    } else if ret > -0.05 {
        ("rgb(\"#FF9090\")", false)
    } else if ret > -0.10 {
        ("rgb(\"#FF4444\")", true)
    } else {
        ("rgb(\"#8B0000\")", true)
    }
}

fn format_pct(value: f64) -> String {
    format!("{:+.1}%", value * 100.0)
}

fn format_heatmap_cell(ret: f64) -> String {
    let (color, white_text) = return_color(ret);
    let formatted = format_pct(ret);
    if white_text {
        format!("box(fill: {}, text(fill: white, [{}]))", color, formatted)
    } else {
        format!("box(fill: {}, [{}])", color, formatted)
    }
}

/// Simple trades table used by the Typst report adapter (TRD §11.3).
pub fn format_trades_table(trades: &[ClosedTrade]) -> String {
    if trades.is_empty() {
        return "No trades executed.".to_string();
    }

    let mut out = String::from(
        "#table(\n  columns: 8,\n  align: (left, left, right, right, right, left, left, right),\n",
    );
    out.push_str(
        "  [*Code*], [*Exchange*], [*Qty*], [*Entry*], [*Exit*], [*Entry Date*], [*Exit Date*], [*P&L*],\n",
    );

    for trade in trades {
        out.push_str(&format!(
            "  [{}], [{}], [{}], [{:.2}], [{:.2}], [{}], [{}], [{:.2}],\n",
            trade.code,
            trade.exchange,
            trade.quantity,
            trade.entry_price,
            trade.exit_price,
            trade.entry_date,
            trade.exit_date,
            trade.pnl
        ));
    }

    out.push(')');
    out
}

pub fn format_trade_log(trades: &[ClosedTrade]) -> String {
    if trades.is_empty() {
        return "// No trades recorded\n".to_string();
    }

    let mut sorted_trades = trades.to_vec();
    sorted_trades.sort_by(|a, b| a.entry_date.cmp(&b.entry_date));

    let mut output = String::new();
    output.push_str("#table(\n");
    output.push_str("  columns: 8,\n");
    output.push_str("  [*#*], [*Code*], [*Entry Date*], [*Exit Date*], [*Qty*], ");
    output.push_str("[*Entry*], [*Exit*], [*PnL*],\n");

    for (i, trade) in sorted_trades.iter().enumerate() {
        let pnl_color = if trade.pnl >= 0.0 { "green" } else { "red" };
        output.push_str(&format!(
            "  [{}], [{}], [{}], [{}], [{}], [{:.2}], [{:.2}], text(fill: {}, [{:.2}]),\n",
            i + 1,
            trade.code,
            trade.entry_date.format("%Y-%m-%d"),
            trade.exit_date.format("%Y-%m-%d"),
            trade.quantity,
            trade.entry_price,
            trade.exit_price,
            pnl_color,
            trade.pnl
        ));
    }

    output.push_str(")\n\n");
    output
}

pub fn format_universe_summary(code_results: &[CodeResult]) -> String {
    if code_results.is_empty() {
        return "// No per-code data available\n".to_string();
    }

    let mut output = String::new();
    output.push_str("#table(\n");
    output.push_str("  columns: 6,\n");
    output.push_str("  [*Code*], [*Trades*], [*Win Rate*], [*Total PnL*], ");
    output.push_str("[*Largest Win*], [*Largest Loss*],\n");

    for result in code_results {
        let pnl_color = if result.total_pnl >= 0.0 {
            "green"
        } else {
            "red"
        };
        output.push_str(&format!(
            "  [{}], [{}], [{:.1}%], text(fill: {}, [{:.2}]), [{:.2}], [{:.2}],\n",
            result.code,
            result.total_trades,
            result.win_rate * 100.0,
            pnl_color,
            result.total_pnl,
            result.largest_win,
            result.largest_loss
        ));
    }

    output.push_str(")\n\n");
    output
}

#[cfg(test)]
mod tests {
    use super::*;
    use chrono::NaiveDate;

    fn make_equity_curve(values: &[(i32, u32, u32, f64)]) -> Vec<EquityPoint> {
        values
            .iter()
            .map(|(y, m, d, v)| EquityPoint {
                date: NaiveDate::from_ymd_opt(*y, *m, *d).unwrap(),
                equity: *v,
            })
            .collect()
    }

    fn sample_trade(code: &str, pnl: f64) -> ClosedTrade {
        ClosedTrade {
            code: code.to_string(),
            exchange: "ASX".to_string(),
            quantity: 100,
            entry_price: 50.0,
            exit_price: 55.0,
            entry_date: NaiveDate::from_ymd_opt(2024, 1, 10).unwrap(),
            exit_date: NaiveDate::from_ymd_opt(2024, 1, 15).unwrap(),
            pnl,
        }
    }

    #[test]
    fn compute_monthly_returns_empty() {
        let curve: Vec<EquityPoint> = vec![];
        let returns = compute_monthly_returns(&curve);
        assert!(returns.is_empty());
    }

    #[test]
    fn compute_monthly_returns_single_month() {
        let curve = make_equity_curve(&[
            (2024, 1, 1, 100_000.0),
            (2024, 1, 15, 105_000.0),
            (2024, 1, 31, 110_000.0),
        ]);
        let returns = compute_monthly_returns(&curve);

        assert_eq!(returns.len(), 1);
        assert_eq!(returns[0].year, 2024);
        assert_eq!(returns[0].month, 1);
        // First month has no prior reference, return is 0.
        assert!((returns[0].return_pct - 0.0).abs() < 1e-9);
    }

    #[test]
    fn compute_monthly_returns_multiple_months() {
        let curve = make_equity_curve(&[
            (2024, 1, 31, 100_000.0),
            (2024, 2, 15, 105_000.0),
            (2024, 2, 28, 110_000.0),
            (2024, 3, 15, 99_000.0),
            (2024, 3, 31, 104_000.0),
        ]);
        let returns = compute_monthly_returns(&curve);

        assert_eq!(returns.len(), 3);

        assert_eq!(returns[0].year, 2024);
        assert_eq!(returns[0].month, 1);
        assert!((returns[0].return_pct - 0.0).abs() < 1e-9);

        assert_eq!(returns[1].year, 2024);
        assert_eq!(returns[1].month, 2);
        assert!((returns[1].return_pct - 0.10).abs() < 1e-9);

        assert_eq!(returns[2].year, 2024);
        assert_eq!(returns[2].month, 3);
        assert!((returns[2].return_pct - (-6000.0 / 110_000.0)).abs() < 1e-9);
    }

    #[test]
    fn format_returns_heatmap_empty() {
        let output = format_returns_heatmap(&[]);
        assert!(output.contains("No returns data"));
    }

    #[test]
    fn format_returns_heatmap_with_data() {
        let returns = vec![
            MonthlyReturns {
                year: 2024,
                month: 1,
                return_pct: 0.05,
            },
            MonthlyReturns {
                year: 2024,
                month: 2,
                return_pct: -0.02,
            },
        ];
        let output = format_returns_heatmap(&returns);

        assert!(output.contains("#table"));
        assert!(output.contains("[*Year*]"));
        assert!(output.contains("[*YTD*]"));
        assert!(output.contains("[2024]"));
        assert!(output.contains("+5.0%"));
        assert!(output.contains("-2.0%"));
    }

    #[test]
    fn format_trade_log_empty() {
        let output = format_trade_log(&[]);
        assert!(output.contains("No trades"));
    }

    #[test]
    fn format_trade_log_with_trades() {
        let trades = vec![ClosedTrade {
            code: "BHP".to_string(),
            exchange: "ASX".to_string(),
            quantity: 100,
            entry_price: 50.0,
            exit_price: 55.0,
            entry_date: NaiveDate::from_ymd_opt(2024, 1, 15).unwrap(),
            exit_date: NaiveDate::from_ymd_opt(2024, 1, 20).unwrap(),
            pnl: 500.0,
        }];
        let output = format_trade_log(&trades);

        assert!(output.contains("#table"));
        assert!(output.contains("BHP"));
        assert!(output.contains("2024-01-15"));
        assert!(output.contains("green"));
    }

    #[test]
    fn format_trade_log_sorted_by_date() {
        let trades = vec![
            ClosedTrade {
                code: "CBA".to_string(),
                exchange: "ASX".to_string(),
                quantity: 100,
                entry_price: 100.0,
                exit_price: 105.0,
                entry_date: NaiveDate::from_ymd_opt(2024, 2, 1).unwrap(),
                exit_date: NaiveDate::from_ymd_opt(2024, 2, 5).unwrap(),
                pnl: 500.0,
            },
            ClosedTrade {
                code: "BHP".to_string(),
                exchange: "ASX".to_string(),
                quantity: 100,
                entry_price: 50.0,
                exit_price: 55.0,
                entry_date: NaiveDate::from_ymd_opt(2024, 1, 15).unwrap(),
                exit_date: NaiveDate::from_ymd_opt(2024, 1, 20).unwrap(),
                pnl: 500.0,
            },
        ];
        let output = format_trade_log(&trades);

        let bhp_pos = output.find("BHP").unwrap();
        let cba_pos = output.find("CBA").unwrap();
        assert!(bhp_pos < cba_pos);
    }

    #[test]
    fn format_universe_summary_empty() {
        let output = format_universe_summary(&[]);
        assert!(output.contains("No per-code"));
    }

    #[test]
    fn format_universe_summary_with_data() {
        let results = vec![CodeResult {
            code: "BHP".to_string(),
            exchange: "ASX".to_string(),
            total_trades: 10,
            winning_trades: 6,
            losing_trades: 4,
            total_pnl: 1500.0,
            win_rate: 0.6,
            largest_win: 500.0,
            largest_loss: -200.0,
        }];
        let output = format_universe_summary(&results);

        assert!(output.contains("#table"));
        assert!(output.contains("BHP"));
        assert!(output.contains("10"));
        assert!(output.contains("60.0%"));
        assert!(output.contains("1500"));
    }

    #[test]
    fn return_color_positive() {
        assert!(return_color(0.15).0.contains("006400"));
        assert!(return_color(0.08).0.contains("228B22"));
        assert!(return_color(0.03).0.contains("90EE90"));
        assert!(return_color(0.01).0.contains("E0FFE0"));
    }

    #[test]
    fn return_color_zero() {
        assert!(return_color(0.0).0.contains("FFFFFF"));
    }

    #[test]
    fn return_color_negative() {
        assert!(return_color(-0.01).0.contains("FFE0E0"));
        assert!(return_color(-0.03).0.contains("FF9090"));
        assert!(return_color(-0.08).0.contains("FF4444"));
        assert!(return_color(-0.15).0.contains("8B0000"));
    }

    #[test]
    fn return_color_white_text_on_dark() {
        assert!(return_color(0.15).1, "dark green needs white text");
        assert!(return_color(0.08).1, "forest green needs white text");
        assert!(!return_color(0.03).1, "light green uses dark text");
        assert!(return_color(-0.08).1, "red needs white text");
        assert!(return_color(-0.15).1, "dark red needs white text");
        assert!(!return_color(-0.01).1, "light pink uses dark text");
    }

    #[test]
    fn format_empty_trades() {
        let result = format_trades_table(&[]);
        assert_eq!(result, "No trades executed.");
    }

    #[test]
    fn format_single_trade() {
        let trades = vec![sample_trade("BHP", 500.0)];
        let result = format_trades_table(&trades);

        assert!(result.contains("#table("));
        assert!(result.contains("[BHP]"));
        assert!(result.contains("500.00"));
    }

    #[test]
    fn format_multiple_trades() {
        let trades = vec![sample_trade("BHP", 500.0), sample_trade("CBA", -200.0)];
        let result = format_trades_table(&trades);

        assert!(result.contains("[BHP]"));
        assert!(result.contains("[CBA]"));
        assert!(result.contains("-200.00"));
    }

    #[test]
    fn table_has_header_row() {
        let trades = vec![sample_trade("BHP", 500.0)];
        let result = format_trades_table(&trades);

        assert!(result.contains("[*Code*]"));
        assert!(result.contains("[*P&L*]"));
    }
}
