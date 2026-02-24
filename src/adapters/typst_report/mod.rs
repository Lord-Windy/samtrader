//! Typst-based PDF report generation (TRD Section 2.2).
//!
//! Orchestrates placeholder resolution: reads a Typst template (either the
//! built-in default or a custom file via `template_path`), resolves all
//! `{{PLACEHOLDER}}` markers by calling helpers from `chart_svg` and `tables`,
//! and writes the final `.typ` file.

pub mod chart_svg;
pub mod default_template;
pub mod tables;

use chrono::NaiveDate;

use crate::domain::backtest::BacktestResult;
use crate::domain::metrics::{CodeResult, Metrics};
use crate::domain::strategy::Strategy;

/// Context for resolving template placeholders.
pub struct ReportContext<'a> {
    pub strategy: &'a Strategy,
    pub result: &'a BacktestResult,
    pub metrics: &'a Metrics,
    pub code_results: Option<&'a [CodeResult]>,
    pub start_date: NaiveDate,
    pub end_date: NaiveDate,
    pub initial_capital: f64,
}

/// Resolve all `{{PLACEHOLDER}}`s in the given template string and return
/// the final Typst markup ready to be written to a `.typ` file.
pub fn resolve(template: &str, ctx: &ReportContext) -> String {
    let mut output = template.to_string();

    // Strategy summary
    let strategy_summary = tables::render_strategy_summary(
        ctx.strategy,
        ctx.start_date,
        ctx.end_date,
        ctx.initial_capital,
    );
    output = output.replace("{{STRATEGY_SUMMARY}}", &strategy_summary);

    // Metrics
    let metrics_table = tables::render_metrics_table(ctx.metrics);
    output = output.replace("{{METRICS_TABLE}}", &metrics_table);

    // Equity curve SVG wrapped in Typst image.decode
    let equity_svg = chart_svg::generate_equity_svg(&ctx.result.portfolio.equity_curve);
    let equity_typst = if equity_svg.is_empty() {
        "_No equity data._".to_string()
    } else {
        format!(
            "#image.decode(\n\"{}\",\n  width: 100%,\n)",
            equity_svg.replace('\\', "\\\\").replace('"', "\\\"")
        )
    };
    output = output.replace("{{EQUITY_CURVE_SVG}}", &equity_typst);

    // Drawdown SVG
    let dd_svg = chart_svg::generate_drawdown_svg(&ctx.result.portfolio.equity_curve);
    let dd_typst = if dd_svg.is_empty() {
        "_No drawdown data._".to_string()
    } else {
        format!(
            "#image.decode(\n\"{}\",\n  width: 100%,\n)",
            dd_svg.replace('\\', "\\\\").replace('"', "\\\"")
        )
    };
    output = output.replace("{{DRAWDOWN_CHART_SVG}}", &dd_typst);

    // Universe summary (multi-code only)
    let universe = ctx
        .code_results
        .filter(|cr| !cr.is_empty())
        .map(tables::render_universe_summary)
        .unwrap_or_default();
    output = output.replace("{{UNIVERSE_SUMMARY}}", &universe);

    // Per-code sections
    let per_code = ctx
        .code_results
        .filter(|cr| !cr.is_empty())
        .map(|cr| tables::render_per_code_sections(ctx.result, cr))
        .unwrap_or_default();
    output = output.replace("{{PER_CODE_SECTIONS}}", &per_code);

    // Trade log
    let trade_log = tables::render_trade_log(&ctx.result.portfolio.closed_trades);
    output = output.replace("{{TRADE_LOG}}", &trade_log);

    // Monthly returns
    let monthly = tables::render_monthly_returns(&ctx.result.portfolio.equity_curve);
    let monthly_typst = if monthly.is_empty() {
        "_Insufficient data for monthly returns._".to_string()
    } else {
        monthly
    };
    output = output.replace("{{MONTHLY_RETURNS}}", &monthly_typst);

    output
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::domain::metrics::CodeResult;
    use crate::domain::portfolio::EquityPoint;
    use crate::domain::position::ClosedTrade;
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
    fn resolve_default_template_no_placeholders_remain() {
        let strategy = sample_strategy();
        let metrics = sample_metrics();
        let result = sample_backtest_result();

        let ctx = ReportContext {
            strategy: &strategy,
            result: &result,
            metrics: &metrics,
            code_results: None,
            start_date: NaiveDate::from_ymd_opt(2020, 1, 1).unwrap(),
            end_date: NaiveDate::from_ymd_opt(2024, 12, 31).unwrap(),
            initial_capital: 100_000.0,
        };

        let output = resolve(default_template::template(), &ctx);
        assert!(
            !output.contains("{{"),
            "unresolved placeholder in output: {output}"
        );
    }

    #[test]
    fn resolve_produces_valid_typst() {
        let strategy = sample_strategy();
        let metrics = sample_metrics();
        let result = sample_backtest_result();

        let ctx = ReportContext {
            strategy: &strategy,
            result: &result,
            metrics: &metrics,
            code_results: None,
            start_date: NaiveDate::from_ymd_opt(2020, 1, 1).unwrap(),
            end_date: NaiveDate::from_ymd_opt(2024, 12, 31).unwrap(),
            initial_capital: 100_000.0,
        };

        let output = resolve(default_template::template(), &ctx);
        assert!(output.contains("#set page("));
        assert!(output.contains("= Backtest Report"));
        assert!(output.contains("#table("));
        assert!(output.contains("Test Strategy"));
        assert!(output.contains("25.00%"));
    }

    #[test]
    fn resolve_with_multi_code() {
        let strategy = sample_strategy();
        let metrics = sample_metrics();
        let mut result = sample_backtest_result();
        result.portfolio.closed_trades.push(ClosedTrade {
            code: "BHP".into(),
            exchange: "ASX".into(),
            quantity: 100,
            entry_price: 50.0,
            exit_price: 55.0,
            entry_date: NaiveDate::from_ymd_opt(2024, 1, 1).unwrap(),
            exit_date: NaiveDate::from_ymd_opt(2024, 1, 5).unwrap(),
            pnl: 500.0,
        });
        result.portfolio.equity_curve.push(EquityPoint {
            date: NaiveDate::from_ymd_opt(2024, 1, 1).unwrap(),
            equity: 100_000.0,
        });
        result.portfolio.equity_curve.push(EquityPoint {
            date: NaiveDate::from_ymd_opt(2024, 1, 5).unwrap(),
            equity: 100_500.0,
        });
        let code_results = vec![CodeResult {
            code: "BHP".into(),
            exchange: "ASX".into(),
            total_trades: 1,
            winning_trades: 1,
            losing_trades: 0,
            total_pnl: 500.0,
            win_rate: 1.0,
            largest_win: 500.0,
            largest_loss: 0.0,
        }];

        let ctx = ReportContext {
            strategy: &strategy,
            result: &result,
            metrics: &metrics,
            code_results: Some(&code_results),
            start_date: NaiveDate::from_ymd_opt(2024, 1, 1).unwrap(),
            end_date: NaiveDate::from_ymd_opt(2024, 12, 31).unwrap(),
            initial_capital: 100_000.0,
        };

        let output = resolve(default_template::template(), &ctx);
        assert!(!output.contains("{{"));
        assert!(output.contains("Universe Summary"));
        assert!(output.contains("BHP"));
        assert!(output.contains("Per-Code Details"));
    }

    #[test]
    fn resolve_custom_template() {
        let strategy = sample_strategy();
        let metrics = sample_metrics();
        let result = sample_backtest_result();

        let ctx = ReportContext {
            strategy: &strategy,
            result: &result,
            metrics: &metrics,
            code_results: None,
            start_date: NaiveDate::from_ymd_opt(2020, 1, 1).unwrap(),
            end_date: NaiveDate::from_ymd_opt(2024, 12, 31).unwrap(),
            initial_capital: 100_000.0,
        };

        // A user-supplied custom template with only some placeholders.
        let custom = "= My Report\n{{STRATEGY_SUMMARY}}\n{{METRICS_TABLE}}";
        let output = resolve(custom, &ctx);
        assert!(output.contains("= My Report"));
        assert!(output.contains("#table("));
        assert!(!output.contains("{{"));
    }
}
