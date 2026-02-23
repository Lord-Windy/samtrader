//! Default Typst report template (TRD Section 3.9, 12.1).
//!
//! Built-in Typst report markup with `{{PLACEHOLDER}}` substitution.

use chrono::{Datelike, NaiveDate};
use std::collections::HashMap;

use crate::domain::backtest::BacktestResult;
use crate::domain::metrics::{CodeResult, Metrics};
use crate::domain::portfolio::EquityPoint;
use crate::domain::position::ClosedTrade;
use crate::domain::strategy::Strategy;

pub struct TemplateContext<'a> {
    pub strategy: &'a Strategy,
    pub result: &'a BacktestResult,
    pub metrics: &'a Metrics,
    pub code_results: Option<&'a [CodeResult]>,
    pub start_date: NaiveDate,
    pub end_date: NaiveDate,
    pub initial_capital: f64,
}

pub fn render(ctx: &TemplateContext) -> String {
    let mut output = String::new();

    output.push_str(&render_header());
    output.push_str(&render_strategy_summary(ctx));
    output.push_str(&render_metrics_table(ctx));
    output.push_str(&render_equity_curve(ctx));
    output.push_str(&render_drawdown_chart(ctx));

    if let Some(code_results) = ctx.code_results {
        if !code_results.is_empty() {
            output.push_str(&render_universe_summary(code_results));
            output.push_str(&render_per_code_sections(ctx.result, code_results));
        }
    }

    output.push_str(&render_trade_log(&ctx.result.portfolio.closed_trades));
    output.push_str(&render_monthly_returns(&ctx.result.portfolio.equity_curve));

    output
}

fn render_header() -> String {
    String::from("# Backtest Report\n\n")
}

fn render_strategy_summary(ctx: &TemplateContext) -> String {
    let mut output = String::from("## Strategy Summary\n\n");
    output.push_str("| Property | Value |\n| --- | --- |\n");
    output.push_str(&format!("| Name | {} |\n", ctx.strategy.name));
    output.push_str(&format!("| Description | {} |\n", ctx.strategy.description));
    output.push_str(&format!(
        "| Entry Rule (Long) | {} |\n",
        ctx.strategy.entry_long
    ));
    output.push_str(&format!(
        "| Exit Rule (Long) | {} |\n",
        ctx.strategy.exit_long
    ));
    output.push_str(&format!(
        "| Entry Rule (Short) | {} |\n",
        ctx.strategy
            .entry_short
            .as_ref()
            .map(|r| r.to_string())
            .unwrap_or_else(|| "N/A".to_string())
    ));
    output.push_str(&format!(
        "| Exit Rule (Short) | {} |\n",
        ctx.strategy
            .exit_short
            .as_ref()
            .map(|r| r.to_string())
            .unwrap_or_else(|| "N/A".to_string())
    ));
    output.push_str(&format!(
        "| Position Size | {:.1}% |\n",
        ctx.strategy.position_size * 100.0
    ));
    output.push_str(&format!(
        "| Stop Loss | {:.1}% |\n",
        ctx.strategy.stop_loss_pct
    ));
    output.push_str(&format!(
        "| Take Profit | {:.1}% |\n",
        ctx.strategy.take_profit_pct
    ));
    output.push_str(&format!(
        "| Max Positions | {} |\n",
        ctx.strategy.max_positions
    ));
    output.push_str(&format!("| Start Date | {} |\n", ctx.start_date));
    output.push_str(&format!("| End Date | {} |\n", ctx.end_date));
    output.push_str(&format!(
        "| Initial Capital | {} |\n",
        fmt_currency(ctx.initial_capital)
    ));
    output.push('\n');
    output
}

fn fmt_currency(value: f64) -> String {
    if value >= 0.0 {
        format!("${:.2}", value)
    } else {
        format!("-${:.2}", value.abs())
    }
}

fn render_metrics_table(ctx: &TemplateContext) -> String {
    let m = ctx.metrics;
    let mut output = String::from("## Performance Metrics\n\n");
    output.push_str("| Metric | Value |\n| --- | --- |\n");
    output.push_str(&format!(
        "| Total Return | {:.2}% |\n",
        m.total_return * 100.0
    ));
    output.push_str(&format!(
        "| Annualized Return | {:.2}% |\n",
        m.annualized_return * 100.0
    ));
    output.push_str(&format!("| Sharpe Ratio | {:.2} |\n", m.sharpe_ratio));
    output.push_str(&format!("| Sortino Ratio | {:.2} |\n", m.sortino_ratio));
    output.push_str(&format!(
        "| Max Drawdown | {:.2}% |\n",
        m.max_drawdown * 100.0
    ));
    output.push_str(&format!(
        "| Max Drawdown Duration | {:.0} days |\n",
        m.max_drawdown_duration
    ));
    output.push_str(&format!("| Total Trades | {} |\n", m.total_trades));
    output.push_str(&format!("| Winning Trades | {} |\n", m.winning_trades));
    output.push_str(&format!("| Losing Trades | {} |\n", m.losing_trades));
    output.push_str(&format!(
        "| Break-Even Trades | {} |\n",
        m.break_even_trades
    ));
    output.push_str(&format!("| Win Rate | {:.1}% |\n", m.win_rate * 100.0));
    output.push_str(&format!(
        "| Profit Factor | {:.2} |\n",
        if m.profit_factor.is_infinite() {
            f64::INFINITY
        } else {
            m.profit_factor
        }
    ));
    output.push_str(&format!(
        "| Average Win | {} |\n",
        fmt_currency(m.average_win)
    ));
    output.push_str(&format!(
        "| Average Loss | {} |\n",
        fmt_currency(m.average_loss)
    ));
    output.push_str(&format!(
        "| Largest Win | {} |\n",
        fmt_currency(m.largest_win)
    ));
    output.push_str(&format!(
        "| Largest Loss | {} |\n",
        fmt_currency(m.largest_loss)
    ));
    output.push_str(&format!(
        "| Avg Trade Duration | {:.1} days |\n",
        m.average_trade_duration
    ));
    output.push('\n');
    output
}

fn render_equity_curve(ctx: &TemplateContext) -> String {
    let equity_curve = &ctx.result.portfolio.equity_curve;
    if equity_curve.is_empty() {
        return String::new();
    }

    let svg = generate_equity_svg(equity_curve);
    format!("## Equity Curve\n\n{svg}\n\n")
}

fn render_drawdown_chart(ctx: &TemplateContext) -> String {
    let equity_curve = &ctx.result.portfolio.equity_curve;
    if equity_curve.len() < 2 {
        return String::new();
    }

    let svg = generate_drawdown_svg(equity_curve);
    format!("## Drawdown Chart\n\n{svg}\n\n")
}

fn render_universe_summary(code_results: &[CodeResult]) -> String {
    let mut output = String::from("## Universe Summary\n\n");
    output.push_str(
        "| Code | Exchange | Trades | Win Rate | Total PnL | Largest Win | Largest Loss |\n",
    );
    output.push_str("| --- | --- | --- | --- | --- | --- | --- |\n");

    for cr in code_results {
        output.push_str(&format!(
            "| {} | {} | {} | {:.1}% | {} | {} | {} |\n",
            cr.code,
            cr.exchange,
            cr.total_trades,
            cr.win_rate * 100.0,
            fmt_currency(cr.total_pnl),
            fmt_currency(cr.largest_win),
            fmt_currency(cr.largest_loss),
        ));
    }

    output.push('\n');
    output
}

fn render_per_code_sections(result: &BacktestResult, code_results: &[CodeResult]) -> String {
    let mut output = String::new();

    for cr in code_results {
        output.push_str(&format!("## Per-Code Details: {}\n\n", cr.code));
        output.push_str("| Property | Value |\n| --- | --- |\n");
        output.push_str(&format!("| Total Trades | {} |\n", cr.total_trades));
        output.push_str(&format!("| Win Rate | {:.1}% |\n", cr.win_rate * 100.0));
        output.push_str(&format!("| Total PnL | {} |\n", fmt_currency(cr.total_pnl)));
        output.push_str(&format!(
            "| Largest Win | {} |\n",
            fmt_currency(cr.largest_win)
        ));
        output.push_str(&format!(
            "| Largest Loss | {} |\n\n",
            fmt_currency(cr.largest_loss)
        ));

        let code_trades: Vec<&ClosedTrade> = result
            .portfolio
            .closed_trades
            .iter()
            .filter(|t| t.code == cr.code)
            .collect();

        if !code_trades.is_empty() {
            output.push_str(&format!("### Trade Log for {}\n\n", cr.code));
            output.push_str(
                "| # | Entry Date | Exit Date | Qty | Entry Price | Exit Price | PnL |\n",
            );
            output.push_str("| --- | --- | --- | --- | --- | --- | --- |\n");

            for (i, trade) in code_trades.iter().enumerate() {
                output.push_str(&format!(
                    "| {} | {} | {} | {} | {} | {} | {} |\n",
                    i + 1,
                    trade.entry_date,
                    trade.exit_date,
                    trade.quantity,
                    fmt_currency(trade.entry_price),
                    fmt_currency(trade.exit_price),
                    fmt_currency(trade.pnl),
                ));
            }
            output.push('\n');
        }
    }

    output
}

fn render_trade_log(trades: &[ClosedTrade]) -> String {
    if trades.is_empty() {
        return String::from("## Trade Log\n\nNo trades executed.\n\n");
    }

    let mut output = String::from("## Full Trade Log\n\n");
    output
        .push_str("| # | Code | Entry Date | Exit Date | Qty | Entry Price | Exit Price | PnL |\n");
    output.push_str("| --- | --- | --- | --- | --- | --- | --- | --- |\n");

    for (i, trade) in trades.iter().enumerate() {
        output.push_str(&format!(
            "| {} | {} | {} | {} | {} | {} | {} | {} |\n",
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

    output.push('\n');
    output
}

fn render_monthly_returns(equity_curve: &[EquityPoint]) -> String {
    if equity_curve.len() < 2 {
        return String::new();
    }

    let mut monthly_data: HashMap<(i32, u32), Vec<f64>> = HashMap::new();

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

    let mut sorted_months: Vec<(i32, u32)> = monthly_data.keys().cloned().collect();
    sorted_months.sort();

    let mut output = String::from("## Monthly Returns\n\n");
    output.push_str("| Year | Month | Return |\n");
    output.push_str("| --- | --- | --- |\n");

    for (year, month) in sorted_months {
        let returns = monthly_data.get(&(year, month)).unwrap();
        let monthly_return: f64 = returns.iter().map(|r| (1.0 + r).ln()).sum::<f64>().exp() - 1.0;
        let month_name = month_to_name(month);
        output.push_str(&format!(
            "| {} | {} | {:.2}% |\n",
            year,
            month_name,
            monthly_return * 100.0
        ));
    }

    output.push('\n');
    output
}

fn month_to_name(month: u32) -> &'static str {
    match month {
        1 => "Jan",
        2 => "Feb",
        3 => "Mar",
        4 => "Apr",
        5 => "May",
        6 => "Jun",
        7 => "Jul",
        8 => "Aug",
        9 => "Sep",
        10 => "Oct",
        11 => "Nov",
        12 => "Dec",
        _ => "???",
    }
}

const CHART_WIDTH: f64 = 600.0;
const CHART_HEIGHT: f64 = 300.0;
const MARGIN_LEFT: f64 = 60.0;
const MARGIN_RIGHT: f64 = 20.0;
const MARGIN_TOP: f64 = 30.0;
const MARGIN_BOTTOM: f64 = 40.0;

fn generate_equity_svg(equity_curve: &[EquityPoint]) -> String {
    if equity_curve.is_empty() {
        return String::new();
    }

    let min_equity = equity_curve
        .iter()
        .map(|p| p.equity)
        .fold(f64::INFINITY, f64::min);
    let max_equity = equity_curve
        .iter()
        .map(|p| p.equity)
        .fold(f64::NEG_INFINITY, f64::max);
    let range = (max_equity - min_equity).max(1.0);

    let plot_width = CHART_WIDTH - MARGIN_LEFT - MARGIN_RIGHT;
    let plot_height = CHART_HEIGHT - MARGIN_TOP - MARGIN_BOTTOM;

    let x_scale = |i: usize| -> f64 {
        MARGIN_LEFT + (i as f64 / (equity_curve.len() - 1).max(1) as f64) * plot_width
    };
    let y_scale =
        |v: f64| -> f64 { MARGIN_TOP + plot_height - ((v - min_equity) / range) * plot_height };

    let mut path_data = String::new();
    for (i, point) in equity_curve.iter().enumerate() {
        let x = x_scale(i);
        let y = y_scale(point.equity);
        if i == 0 {
            path_data.push_str(&format!("M {:.1} {:.1}", x, y));
        } else {
            path_data.push_str(&format!(" L {:.1} {:.1}", x, y));
        }
    }

    let start_date = equity_curve.first().unwrap().date;
    let end_date = equity_curve.last().unwrap().date;
    let mid_date = if equity_curve.len() > 1 {
        equity_curve[equity_curve.len() / 2].date
    } else {
        start_date
    };

    let mut svg = String::new();
    svg.push_str(&format!(
        r##"<svg width="{}" height="{}" viewBox="0 0 {} {}" xmlns="http://www.w3.org/2000/svg">"##,
        CHART_WIDTH, CHART_HEIGHT, CHART_WIDTH, CHART_HEIGHT
    ));
    svg.push_str("\n  <rect width=\"100%\" height=\"100%\" fill=\"white\"/>\n");
    svg.push_str(&format!(
        "  <text x=\"{}\" y=\"15\" text-anchor=\"end\" font-size=\"12\" fill=\"#666\">Equity ($)</text>\n",
        CHART_WIDTH
    ));
    svg.push_str(&format!(
        "  <line x1=\"{}\" y1=\"{}\" x2=\"{}\" y2=\"{}\" stroke=\"#ccc\" stroke-width=\"1\"/>\n",
        MARGIN_LEFT,
        MARGIN_TOP,
        MARGIN_LEFT,
        CHART_HEIGHT - MARGIN_BOTTOM
    ));
    svg.push_str(&format!(
        "  <line x1=\"{}\" y1=\"{}\" x2=\"{}\" y2=\"{}\" stroke=\"#ccc\" stroke-width=\"1\"/>\n",
        MARGIN_LEFT,
        CHART_HEIGHT - MARGIN_BOTTOM,
        CHART_WIDTH - MARGIN_RIGHT,
        CHART_HEIGHT - MARGIN_BOTTOM
    ));
    svg.push_str(&format!(
        "  <text x=\"{}\" y=\"{}\" text-anchor=\"end\" font-size=\"10\" fill=\"#666\">{}</text>\n",
        MARGIN_LEFT - 5.0,
        MARGIN_TOP + 5.0,
        fmt_currency(max_equity).replace("$", "$")
    ));
    svg.push_str(&format!(
        "  <text x=\"{}\" y=\"{}\" text-anchor=\"end\" font-size=\"10\" fill=\"#666\">{}</text>\n",
        MARGIN_LEFT - 5.0,
        MARGIN_TOP + plot_height / 2.0,
        fmt_currency((max_equity + min_equity) / 2.0)
    ));
    svg.push_str(&format!(
        "  <text x=\"{}\" y=\"{}\" text-anchor=\"end\" font-size=\"10\" fill=\"#666\">{}</text>\n",
        MARGIN_LEFT - 5.0,
        CHART_HEIGHT - MARGIN_BOTTOM - 5.0,
        fmt_currency(min_equity)
    ));
    svg.push_str(&format!(
        "  <text x=\"{}\" y=\"{}\" text-anchor=\"middle\" font-size=\"10\" fill=\"#666\">{}</text>\n",
        MARGIN_LEFT, CHART_HEIGHT, start_date
    ));
    svg.push_str(&format!(
        "  <text x=\"{}\" y=\"{}\" text-anchor=\"middle\" font-size=\"10\" fill=\"#666\">{}</text>\n",
        MARGIN_LEFT + plot_width / 2.0,
        CHART_HEIGHT,
        mid_date
    ));
    svg.push_str(&format!(
        "  <text x=\"{}\" y=\"{}\" text-anchor=\"middle\" font-size=\"10\" fill=\"#666\">{}</text>\n",
        CHART_WIDTH - MARGIN_RIGHT,
        CHART_HEIGHT,
        end_date
    ));
    svg.push_str(&format!(
        "  <path d=\"{}\" fill=\"none\" stroke=\"#2563eb\" stroke-width=\"2\"/>\n",
        path_data
    ));
    svg.push_str("</svg>");
    svg
}

fn generate_drawdown_svg(equity_curve: &[EquityPoint]) -> String {
    if equity_curve.len() < 2 {
        return String::new();
    }

    let drawdowns: Vec<f64> = compute_drawdown_series(equity_curve);
    let max_dd = drawdowns.iter().cloned().fold(0.0, f64::max).max(0.01);

    let plot_width = CHART_WIDTH - MARGIN_LEFT - MARGIN_RIGHT;
    let plot_height = CHART_HEIGHT - MARGIN_TOP - MARGIN_BOTTOM;

    let x_scale = |i: usize| -> f64 {
        MARGIN_LEFT + (i as f64 / (drawdowns.len() - 1).max(1) as f64) * plot_width
    };
    let y_scale = |dd: f64| -> f64 { MARGIN_TOP + (dd / max_dd) * plot_height };

    let mut path_data = format!("M {:.1} {:.1}", x_scale(0), y_scale(0.0));
    for (i, &dd) in drawdowns.iter().enumerate() {
        if i > 0 {
            path_data.push_str(&format!(" L {:.1} {:.1}", x_scale(i), y_scale(dd)));
        }
    }
    path_data.push_str(&format!(
        " L {:.1} {:.1} L {:.1} {:.1} Z",
        x_scale(drawdowns.len() - 1),
        y_scale(0.0),
        x_scale(0),
        y_scale(0.0)
    ));

    let start_date = equity_curve.first().unwrap().date;
    let end_date = equity_curve.last().unwrap().date;
    let mid_date = if equity_curve.len() > 1 {
        equity_curve[equity_curve.len() / 2].date
    } else {
        start_date
    };

    let mut svg = String::new();
    svg.push_str(&format!(
        r##"<svg width="{}" height="{}" viewBox="0 0 {} {}" xmlns="http://www.w3.org/2000/svg">"##,
        CHART_WIDTH, CHART_HEIGHT, CHART_WIDTH, CHART_HEIGHT
    ));
    svg.push_str("\n  <rect width=\"100%\" height=\"100%\" fill=\"white\"/>\n");
    svg.push_str(&format!(
        "  <text x=\"{}\" y=\"15\" text-anchor=\"end\" font-size=\"12\" fill=\"#666\">Drawdown (%)</text>\n",
        CHART_WIDTH
    ));
    svg.push_str(&format!(
        "  <line x1=\"{}\" y1=\"{}\" x2=\"{}\" y2=\"{}\" stroke=\"#ccc\" stroke-width=\"1\"/>\n",
        MARGIN_LEFT,
        MARGIN_TOP,
        MARGIN_LEFT,
        CHART_HEIGHT - MARGIN_BOTTOM
    ));
    svg.push_str(&format!(
        "  <line x1=\"{}\" y1=\"{}\" x2=\"{}\" y2=\"{}\" stroke=\"#ccc\" stroke-width=\"1\"/>\n",
        MARGIN_LEFT,
        CHART_HEIGHT - MARGIN_BOTTOM,
        CHART_WIDTH - MARGIN_RIGHT,
        CHART_HEIGHT - MARGIN_BOTTOM
    ));
    svg.push_str(&format!(
        "  <text x=\"{}\" y=\"{}\" text-anchor=\"end\" font-size=\"10\" fill=\"#666\">0%</text>\n",
        MARGIN_LEFT - 5.0,
        MARGIN_TOP + 5.0
    ));
    svg.push_str(&format!(
        "  <text x=\"{}\" y=\"{}\" text-anchor=\"end\" font-size=\"10\" fill=\"#666\">-{:.1}%</text>\n",
        MARGIN_LEFT - 5.0,
        MARGIN_TOP + plot_height / 2.0,
        max_dd * 50.0
    ));
    svg.push_str(&format!(
        "  <text x=\"{}\" y=\"{}\" text-anchor=\"end\" font-size=\"10\" fill=\"#666\">-{:.1}%</text>\n",
        MARGIN_LEFT - 5.0,
        CHART_HEIGHT - MARGIN_BOTTOM - 5.0,
        max_dd * 100.0
    ));
    svg.push_str(&format!(
        "  <text x=\"{}\" y=\"{}\" text-anchor=\"middle\" font-size=\"10\" fill=\"#666\">{}</text>\n",
        MARGIN_LEFT, CHART_HEIGHT, start_date
    ));
    svg.push_str(&format!(
        "  <text x=\"{}\" y=\"{}\" text-anchor=\"middle\" font-size=\"10\" fill=\"#666\">{}</text>\n",
        MARGIN_LEFT + plot_width / 2.0,
        CHART_HEIGHT,
        mid_date
    ));
    svg.push_str(&format!(
        "  <text x=\"{}\" y=\"{}\" text-anchor=\"middle\" font-size=\"10\" fill=\"#666\">{}</text>\n",
        CHART_WIDTH - MARGIN_RIGHT,
        CHART_HEIGHT,
        end_date
    ));
    svg.push_str(&format!(
        "  <path d=\"{}\" fill=\"rgba(239,68,68,0.3)\" stroke=\"#dc2626\" stroke-width=\"1\"/>\n",
        path_data
    ));
    svg.push_str("</svg>");
    svg
}

fn compute_drawdown_series(equity_curve: &[EquityPoint]) -> Vec<f64> {
    let mut drawdowns = Vec::with_capacity(equity_curve.len());
    let mut peak = equity_curve[0].equity;

    for point in equity_curve {
        if point.equity > peak {
            peak = point.equity;
        }
        let dd = if peak > 0.0 {
            (peak - point.equity) / peak
        } else {
            0.0
        };
        drawdowns.push(dd);
    }

    drawdowns
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::domain::rule::{Operand, Rule};

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

    fn sample_metrics() -> Metrics {
        Metrics {
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
        }
    }

    fn sample_backtest_result() -> BacktestResult {
        BacktestResult {
            portfolio: crate::domain::portfolio::Portfolio::new(100_000.0),
        }
    }

    #[test]
    fn render_produces_valid_output() {
        let strategy = sample_strategy();
        let metrics = sample_metrics();
        let result = sample_backtest_result();

        let ctx = TemplateContext {
            strategy: &strategy,
            result: &result,
            metrics: &metrics,
            code_results: None,
            start_date: NaiveDate::from_ymd_opt(2020, 1, 1).unwrap(),
            end_date: NaiveDate::from_ymd_opt(2024, 12, 31).unwrap(),
            initial_capital: 100_000.0,
        };

        let output = render(&ctx);
        assert!(output.contains("# Backtest Report"));
        assert!(output.contains("## Strategy Summary"));
        assert!(output.contains("Test Strategy"));
        assert!(output.contains("## Performance Metrics"));
        assert!(output.contains("25.00%"));
    }

    #[test]
    fn render_strategy_summary_includes_all_fields() {
        let strategy = sample_strategy();
        let metrics = sample_metrics();
        let result = sample_backtest_result();

        let ctx = TemplateContext {
            strategy: &strategy,
            result: &result,
            metrics: &metrics,
            code_results: None,
            start_date: NaiveDate::from_ymd_opt(2020, 1, 1).unwrap(),
            end_date: NaiveDate::from_ymd_opt(2024, 12, 31).unwrap(),
            initial_capital: 100_000.0,
        };

        let output = render(&ctx);
        assert!(output.contains("Entry Rule (Long)"));
        assert!(output.contains("close above 100"));
        assert!(output.contains("Position Size"));
        assert!(output.contains("Stop Loss"));
        assert!(output.contains("Take Profit"));
        assert!(output.contains("Max Positions"));
    }

    #[test]
    fn render_universe_summary_shown_for_multi_code() {
        let strategy = sample_strategy();
        let metrics = sample_metrics();
        let result = sample_backtest_result();
        let code_results = vec![crate::domain::metrics::CodeResult {
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

        let ctx = TemplateContext {
            strategy: &strategy,
            result: &result,
            metrics: &metrics,
            code_results: Some(&code_results),
            start_date: NaiveDate::from_ymd_opt(2020, 1, 1).unwrap(),
            end_date: NaiveDate::from_ymd_opt(2024, 12, 31).unwrap(),
            initial_capital: 100_000.0,
        };

        let output = render(&ctx);
        assert!(output.contains("## Universe Summary"));
        assert!(output.contains("BHP"));
    }

    #[test]
    fn render_trade_log_empty() {
        let trades: Vec<ClosedTrade> = vec![];
        let output = render_trade_log(&trades);
        assert!(output.contains("No trades executed"));
    }

    #[test]
    fn render_trade_log_with_trades() {
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

        let output = render_trade_log(&trades);
        assert!(output.contains("BHP"));
        assert!(output.contains("$500.00"));
    }

    #[test]
    fn generate_equity_svg_empty_curve() {
        let curve: Vec<EquityPoint> = vec![];
        let svg = generate_equity_svg(&curve);
        assert!(svg.is_empty());
    }

    #[test]
    fn generate_equity_svg_single_point() {
        let curve = vec![EquityPoint {
            date: NaiveDate::from_ymd_opt(2024, 1, 1).unwrap(),
            equity: 100_000.0,
        }];
        let svg = generate_equity_svg(&curve);
        assert!(svg.contains("<svg"));
        assert!(svg.contains("100000"));
    }

    #[test]
    fn generate_equity_svg_multiple_points() {
        let curve = vec![
            EquityPoint {
                date: NaiveDate::from_ymd_opt(2024, 1, 1).unwrap(),
                equity: 100_000.0,
            },
            EquityPoint {
                date: NaiveDate::from_ymd_opt(2024, 1, 2).unwrap(),
                equity: 105_000.0,
            },
            EquityPoint {
                date: NaiveDate::from_ymd_opt(2024, 1, 3).unwrap(),
                equity: 102_000.0,
            },
        ];
        let svg = generate_equity_svg(&curve);
        assert!(svg.contains("<path"));
        assert!(svg.contains("stroke=\"#2563eb\""));
    }

    #[test]
    fn generate_drawdown_svg_empty_curve() {
        let curve: Vec<EquityPoint> = vec![];
        let svg = generate_drawdown_svg(&curve);
        assert!(svg.is_empty());
    }

    #[test]
    fn generate_drawdown_svg_single_point() {
        let curve = vec![EquityPoint {
            date: NaiveDate::from_ymd_opt(2024, 1, 1).unwrap(),
            equity: 100_000.0,
        }];
        let svg = generate_drawdown_svg(&curve);
        assert!(svg.is_empty());
    }

    #[test]
    fn generate_drawdown_svg_multiple_points() {
        let curve = vec![
            EquityPoint {
                date: NaiveDate::from_ymd_opt(2024, 1, 1).unwrap(),
                equity: 100_000.0,
            },
            EquityPoint {
                date: NaiveDate::from_ymd_opt(2024, 1, 2).unwrap(),
                equity: 95_000.0,
            },
            EquityPoint {
                date: NaiveDate::from_ymd_opt(2024, 1, 3).unwrap(),
                equity: 98_000.0,
            },
        ];
        let svg = generate_drawdown_svg(&curve);
        assert!(svg.contains("<path"));
        assert!(svg.contains("fill=\"rgba(239,68,68,0.3)\""));
    }

    #[test]
    fn compute_drawdown_series_zero_drawdown() {
        let curve = vec![
            EquityPoint {
                date: NaiveDate::from_ymd_opt(2024, 1, 1).unwrap(),
                equity: 100_000.0,
            },
            EquityPoint {
                date: NaiveDate::from_ymd_opt(2024, 1, 2).unwrap(),
                equity: 110_000.0,
            },
            EquityPoint {
                date: NaiveDate::from_ymd_opt(2024, 1, 3).unwrap(),
                equity: 120_000.0,
            },
        ];
        let dd = compute_drawdown_series(&curve);
        assert!(dd.iter().all(|&d| d == 0.0));
    }

    #[test]
    fn compute_drawdown_series_with_drawdown() {
        let curve = vec![
            EquityPoint {
                date: NaiveDate::from_ymd_opt(2024, 1, 1).unwrap(),
                equity: 100_000.0,
            },
            EquityPoint {
                date: NaiveDate::from_ymd_opt(2024, 1, 2).unwrap(),
                equity: 90_000.0,
            },
            EquityPoint {
                date: NaiveDate::from_ymd_opt(2024, 1, 3).unwrap(),
                equity: 95_000.0,
            },
        ];
        let dd = compute_drawdown_series(&curve);
        assert!((dd[0] - 0.0).abs() < 1e-9);
        assert!((dd[1] - 0.10).abs() < 1e-9);
        assert!((dd[2] - 0.05).abs() < 1e-9);
    }

    #[test]
    fn month_to_name_all_months() {
        assert_eq!(month_to_name(1), "Jan");
        assert_eq!(month_to_name(2), "Feb");
        assert_eq!(month_to_name(3), "Mar");
        assert_eq!(month_to_name(4), "Apr");
        assert_eq!(month_to_name(5), "May");
        assert_eq!(month_to_name(6), "Jun");
        assert_eq!(month_to_name(7), "Jul");
        assert_eq!(month_to_name(8), "Aug");
        assert_eq!(month_to_name(9), "Sep");
        assert_eq!(month_to_name(10), "Oct");
        assert_eq!(month_to_name(11), "Nov");
        assert_eq!(month_to_name(12), "Dec");
    }
}
