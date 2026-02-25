//! HTML report adapter implementing ReportPort (TRD Section 5.2).
//!
//! Generates HTML reports using Askama templates with inline SVG charts.

use std::collections::BTreeMap;
use std::fs;
use std::path::Path;

use crate::domain::backtest::{BacktestResult, MultiCodeResult};
use crate::domain::error::SamtraderError;
use crate::domain::metrics::Metrics;
use crate::domain::portfolio::EquityPoint;
use crate::domain::strategy::Strategy;
use crate::ports::report_port::ReportPort;
use chrono::Datelike;

use askama::Template;

struct MonthlyReturnRow {
    year: i32,
    months: Vec<Option<f64>>,
}

fn compute_monthly_returns(equity_curve: &[EquityPoint]) -> Vec<MonthlyReturnRow> {
    if equity_curve.len() < 2 {
        return Vec::new();
    }

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

    let mut returns: BTreeMap<(i32, u32), f64> = BTreeMap::new();
    for (&key, daily) in &monthly_data {
        let compounded = daily.iter().map(|r| (1.0 + r).ln()).sum::<f64>().exp() - 1.0;
        returns.insert(key, compounded * 100.0);
    }

    let min_year = returns.keys().map(|k| k.0).min().unwrap_or(2020);
    let max_year = returns.keys().map(|k| k.0).max().unwrap_or(2020);

    (min_year..=max_year)
        .map(|year| MonthlyReturnRow {
            year,
            months: (1..=12u32)
                .map(|month| returns.get(&(year, month)).copied())
                .collect(),
        })
        .collect()
}

#[derive(Template)]
#[template(path = "report.html")]
struct ReportTemplate<'a> {
    strategy: &'a Strategy,
    metrics: &'a Metrics,
    code_results: Option<&'a [crate::domain::metrics::CodeResult]>,
    equity_svg: String,
    drawdown_svg: String,
    trades: &'a [crate::domain::position::ClosedTrade],
    skipped: Vec<SkippedCode>,
    start_date: chrono::NaiveDate,
    end_date: chrono::NaiveDate,
    initial_capital: f64,
    monthly_returns: Vec<MonthlyReturnRow>,
}

struct SkippedCode {
    code: String,
    reason: String,
}

pub struct HtmlReportAdapter;

impl HtmlReportAdapter {
    pub fn new() -> Self {
        Self
    }
}

impl Default for HtmlReportAdapter {
    fn default() -> Self {
        Self::new()
    }
}

impl ReportPort for HtmlReportAdapter {
    fn write(
        &self,
        result: &BacktestResult,
        strategy: &Strategy,
        output_path: &str,
    ) -> Result<(), SamtraderError> {
        let metrics = Metrics::compute(&result.portfolio, 0.05);

        let equity_svg = crate::adapters::typst_report::chart_svg::generate_equity_svg(
            &result.portfolio.equity_curve,
        );
        let drawdown_svg = crate::adapters::typst_report::chart_svg::generate_drawdown_svg(
            &result.portfolio.equity_curve,
        );

        let start_date = result
            .portfolio
            .equity_curve
            .first()
            .map(|p| p.date)
            .unwrap_or_else(|| chrono::NaiveDate::from_ymd_opt(2020, 1, 1).unwrap());
        let end_date = result
            .portfolio
            .equity_curve
            .last()
            .map(|p| p.date)
            .unwrap_or_else(|| chrono::NaiveDate::from_ymd_opt(2020, 1, 1).unwrap());
        let initial_capital = result.portfolio.initial_capital;

        let monthly_returns = compute_monthly_returns(&result.portfolio.equity_curve);

        let template = ReportTemplate {
            strategy,
            metrics: &metrics,
            code_results: None,
            equity_svg,
            drawdown_svg,
            trades: &result.portfolio.closed_trades,
            skipped: Vec::new(),
            start_date,
            end_date,
            initial_capital,
            monthly_returns,
        };

        let html = template
            .render()
            .map_err(|e| SamtraderError::Io(std::io::Error::other(e.to_string())))?;

        let path = Path::new(output_path);
        if let Some(parent) = path.parent() {
            fs::create_dir_all(parent).map_err(SamtraderError::Io)?;
        }
        fs::write(path, html).map_err(SamtraderError::Io)?;

        Ok(())
    }

    fn write_multi(
        &self,
        result: &MultiCodeResult,
        strategy: &Strategy,
        output_path: &str,
    ) -> Result<(), SamtraderError> {
        let metrics = Metrics::compute(&result.aggregate.portfolio, 0.05);

        let equity_svg = crate::adapters::typst_report::chart_svg::generate_equity_svg(
            &result.aggregate.portfolio.equity_curve,
        );
        let drawdown_svg = crate::adapters::typst_report::chart_svg::generate_drawdown_svg(
            &result.aggregate.portfolio.equity_curve,
        );

        let start_date = result
            .aggregate
            .portfolio
            .equity_curve
            .first()
            .map(|p| p.date)
            .unwrap_or_else(|| chrono::NaiveDate::from_ymd_opt(2020, 1, 1).unwrap());
        let end_date = result
            .aggregate
            .portfolio
            .equity_curve
            .last()
            .map(|p| p.date)
            .unwrap_or_else(|| chrono::NaiveDate::from_ymd_opt(2020, 1, 1).unwrap());
        let initial_capital = result.aggregate.portfolio.initial_capital;

        let code_results = crate::domain::metrics::CodeResult::compute_per_code(
            &result.aggregate.portfolio.closed_trades,
        );

        let monthly_returns = compute_monthly_returns(&result.aggregate.portfolio.equity_curve);

        let template = ReportTemplate {
            strategy,
            metrics: &metrics,
            code_results: Some(&code_results),
            equity_svg,
            drawdown_svg,
            trades: &result.aggregate.portfolio.closed_trades,
            skipped: Vec::new(),
            start_date,
            end_date,
            initial_capital,
            monthly_returns,
        };

        let html = template
            .render()
            .map_err(|e| SamtraderError::Io(std::io::Error::other(e.to_string())))?;

        let path = Path::new(output_path);
        if let Some(parent) = path.parent() {
            fs::create_dir_all(parent).map_err(SamtraderError::Io)?;
        }
        fs::write(path, html).map_err(SamtraderError::Io)?;

        Ok(())
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::domain::portfolio::{EquityPoint, Portfolio};
    use crate::domain::position::ClosedTrade;
    use crate::domain::rule::{Operand, Rule};
    use chrono::NaiveDate;
    use std::fs;
    use tempfile::tempdir;

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

    fn sample_backtest_result() -> BacktestResult {
        let mut portfolio = Portfolio::new(100_000.0);
        portfolio.equity_curve.push(EquityPoint {
            date: NaiveDate::from_ymd_opt(2024, 1, 1).unwrap(),
            equity: 100_000.0,
        });
        portfolio.equity_curve.push(EquityPoint {
            date: NaiveDate::from_ymd_opt(2024, 1, 2).unwrap(),
            equity: 102_000.0,
        });
        portfolio.equity_curve.push(EquityPoint {
            date: NaiveDate::from_ymd_opt(2024, 1, 3).unwrap(),
            equity: 101_000.0,
        });
        portfolio.closed_trades.push(ClosedTrade {
            code: "BHP".into(),
            exchange: "ASX".into(),
            quantity: 100,
            entry_price: 50.0,
            exit_price: 55.0,
            entry_date: NaiveDate::from_ymd_opt(2024, 1, 1).unwrap(),
            exit_date: NaiveDate::from_ymd_opt(2024, 1, 2).unwrap(),
            pnl: 500.0,
        });
        BacktestResult { portfolio }
    }

    #[test]
    fn html_report_adapter_write_creates_file() {
        let dir = tempdir().unwrap();
        let output_path = dir.path().join("report.html");
        let output_str = output_path.to_str().unwrap();

        let adapter = HtmlReportAdapter::new();
        let result = sample_backtest_result();
        let strategy = sample_strategy();

        adapter.write(&result, &strategy, output_str).unwrap();

        assert!(output_path.exists());
        let contents = fs::read_to_string(&output_path).unwrap();
        assert!(contents.contains("Backtest Report"));
        assert!(contents.contains("Test Strategy"));
        assert!(contents.contains("<svg"));
    }

    #[test]
    fn html_report_adapter_includes_strategy_params() {
        let dir = tempdir().unwrap();
        let output_path = dir.path().join("report.html");
        let output_str = output_path.to_str().unwrap();

        let adapter = HtmlReportAdapter::new();
        let result = sample_backtest_result();
        let strategy = sample_strategy();

        adapter.write(&result, &strategy, output_str).unwrap();

        let contents = fs::read_to_string(&output_path).unwrap();
        assert!(contents.contains("Position Size"));
        assert!(contents.contains("25.0%"));
        assert!(contents.contains("Max Positions"));
        assert!(contents.contains("5"));
    }

    #[test]
    fn html_report_adapter_includes_metrics() {
        let dir = tempdir().unwrap();
        let output_path = dir.path().join("report.html");
        let output_str = output_path.to_str().unwrap();

        let adapter = HtmlReportAdapter::new();
        let result = sample_backtest_result();
        let strategy = sample_strategy();

        adapter.write(&result, &strategy, output_str).unwrap();

        let contents = fs::read_to_string(&output_path).unwrap();
        assert!(contents.contains("Total Return"));
        assert!(contents.contains("Sharpe Ratio"));
        assert!(contents.contains("Max Drawdown"));
    }

    #[test]
    fn html_report_adapter_includes_equity_chart() {
        let dir = tempdir().unwrap();
        let output_path = dir.path().join("report.html");
        let output_str = output_path.to_str().unwrap();

        let adapter = HtmlReportAdapter::new();
        let result = sample_backtest_result();
        let strategy = sample_strategy();

        adapter.write(&result, &strategy, output_str).unwrap();

        let contents = fs::read_to_string(&output_path).unwrap();
        assert!(contents.contains("Equity Chart"));
        assert!(contents.contains("stroke=\"#2563eb\""));
    }

    #[test]
    fn html_report_adapter_includes_drawdown_chart() {
        let dir = tempdir().unwrap();
        let output_path = dir.path().join("report.html");
        let output_str = output_path.to_str().unwrap();

        let adapter = HtmlReportAdapter::new();
        let result = sample_backtest_result();
        let strategy = sample_strategy();

        adapter.write(&result, &strategy, output_str).unwrap();

        let contents = fs::read_to_string(&output_path).unwrap();
        assert!(contents.contains("Drawdown Chart"));
        assert!(contents.contains("fill=\"rgba(239,68,68,0.3)\""));
    }

    #[test]
    fn html_report_adapter_includes_trade_log() {
        let dir = tempdir().unwrap();
        let output_path = dir.path().join("report.html");
        let output_str = output_path.to_str().unwrap();

        let adapter = HtmlReportAdapter::new();
        let result = sample_backtest_result();
        let strategy = sample_strategy();

        adapter.write(&result, &strategy, output_str).unwrap();

        let contents = fs::read_to_string(&output_path).unwrap();
        assert!(contents.contains("Trade Log"));
        assert!(contents.contains("BHP"));
        assert!(contents.contains("500"));
    }

    #[test]
    fn html_report_adapter_write_multi_includes_per_code_results() {
        let dir = tempdir().unwrap();
        let output_path = dir.path().join("report.html");
        let output_str = output_path.to_str().unwrap();

        let adapter = HtmlReportAdapter::new();
        let strategy = sample_strategy();

        let mut portfolio = Portfolio::new(100_000.0);
        portfolio.equity_curve.push(EquityPoint {
            date: NaiveDate::from_ymd_opt(2024, 1, 1).unwrap(),
            equity: 100_000.0,
        });
        portfolio.equity_curve.push(EquityPoint {
            date: NaiveDate::from_ymd_opt(2024, 1, 3).unwrap(),
            equity: 103_000.0,
        });
        portfolio.closed_trades.push(ClosedTrade {
            code: "BHP".into(),
            exchange: "ASX".into(),
            quantity: 100,
            entry_price: 50.0,
            exit_price: 55.0,
            entry_date: NaiveDate::from_ymd_opt(2024, 1, 1).unwrap(),
            exit_date: NaiveDate::from_ymd_opt(2024, 1, 2).unwrap(),
            pnl: 500.0,
        });
        portfolio.closed_trades.push(ClosedTrade {
            code: "CBA".into(),
            exchange: "ASX".into(),
            quantity: 50,
            entry_price: 80.0,
            exit_price: 85.0,
            entry_date: NaiveDate::from_ymd_opt(2024, 1, 1).unwrap(),
            exit_date: NaiveDate::from_ymd_opt(2024, 1, 3).unwrap(),
            pnl: 250.0,
        });

        let multi = MultiCodeResult {
            aggregate: BacktestResult { portfolio },
            code_results: Vec::new(),
        };

        adapter.write_multi(&multi, &strategy, output_str).unwrap();

        let contents = fs::read_to_string(&output_path).unwrap();
        assert!(contents.contains("Universe Summary"));
        assert!(contents.contains("BHP"));
        assert!(contents.contains("CBA"));
        assert!(contents.contains("<svg"));
    }

    #[test]
    fn html_report_adapter_creates_parent_directories() {
        let dir = tempdir().unwrap();
        let output_path = dir.path().join("nested/deep/path/report.html");
        let output_str = output_path.to_str().unwrap();

        let adapter = HtmlReportAdapter::new();
        let result = sample_backtest_result();
        let strategy = sample_strategy();

        adapter.write(&result, &strategy, output_str).unwrap();

        assert!(output_path.exists());
    }
}
