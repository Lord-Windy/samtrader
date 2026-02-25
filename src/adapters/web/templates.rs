//! HTML templates using Askama.

use askama::Template;
use axum::{
    http::HeaderMap,
    response::{Html, IntoResponse, Response},
};
use chrono::Datelike;
use chrono::NaiveDate;
use std::collections::BTreeMap;

use crate::domain::metrics::{CodeResult, Metrics};
use crate::domain::portfolio::EquityPoint;
use crate::domain::position::ClosedTrade;
use crate::domain::strategy::Strategy;

use super::{is_htmx_request, WebError};

/// Base page wrapper. Renders the full HTML page around pre-rendered content.
#[derive(Template)]
#[template(path = "base.html")]
pub struct BasePage<'a> {
    pub title: &'a str,
    pub content: &'a str,
    pub nav_path: &'a str,
}

/// Render a template as an HTMX fragment or full page depending on headers.
pub fn render_page(
    template: &impl Template,
    title: &str,
    headers: &HeaderMap,
) -> Result<Response, WebError> {
    render_page_with_nav(template, title, headers, "")
}

/// Render a template as an HTMX fragment or full page, highlighting the given nav path.
pub fn render_page_with_nav(
    template: &impl Template,
    title: &str,
    headers: &HeaderMap,
    nav_path: &str,
) -> Result<Response, WebError> {
    let content = template
        .render()
        .map_err(|e| WebError::internal(e.to_string()))?;

    if is_htmx_request(headers) {
        Ok(Html(content).into_response())
    } else {
        let page = BasePage {
            title,
            content: &content,
            nav_path,
        };
        let html = page
            .render()
            .map_err(|e| WebError::internal(e.to_string()))?;
        Ok(Html(html).into_response())
    }
}

#[derive(Template)]
#[template(path = "dashboard.html")]
pub struct DashboardTemplate<'a> {
    pub recent_backtests: &'a [&'a str],
}

#[derive(Template)]
#[template(path = "login.html")]
pub struct LoginTemplate<'a> {
    pub error: Option<&'a str>,
}

#[derive(Template)]
#[template(path = "backtest_form.html")]
pub struct BacktestFormTemplate<'a> {
    pub symbols: &'a [String],
    pub default_start: &'a str,
    pub default_end: &'a str,
}

#[derive(Template)]
#[template(path = "report.html")]
pub struct ReportTemplate<'a> {
    pub strategy: &'a Strategy,
    pub metrics: &'a Metrics,
    pub code_results: Option<&'a [CodeResult]>,
    pub equity_svg: &'a str,
    pub drawdown_svg: &'a str,
    pub trades: &'a [ClosedTrade],
    pub skipped: &'a [SkippedCode<'a>],
    pub start_date: NaiveDate,
    pub end_date: NaiveDate,
    pub initial_capital: f64,
    pub monthly_returns: &'a [MonthlyReturnRow],
}

pub struct SkippedCode<'a> {
    pub code: &'a str,
    pub reason: &'a str,
}

#[derive(Template)]
#[template(path = "error.html")]
pub struct ErrorTemplate<'a> {
    pub message: &'a str,
    pub status: u16,
}

#[derive(Template)]
#[template(path = "report_placeholder.html")]
pub struct ReportPlaceholderTemplate;

pub struct MonthlyReturnRow {
    pub year: i32,
    pub months: Vec<Option<f64>>,
}

pub fn compute_monthly_returns(equity_curve: &[EquityPoint]) -> Vec<MonthlyReturnRow> {
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

#[cfg(test)]
mod tests {
    use super::*;
    use chrono::NaiveDate;

    fn equity(date: &str, equity: f64) -> EquityPoint {
        EquityPoint {
            date: NaiveDate::parse_from_str(date, "%Y-%m-%d").unwrap(),
            equity,
        }
    }

    #[test]
    fn empty_curve_returns_empty() {
        assert!(compute_monthly_returns(&[]).is_empty());
        assert!(compute_monthly_returns(&[equity("2024-01-01", 100.0)]).is_empty());
    }

    #[test]
    fn single_month_positive_return() {
        let curve = vec![
            equity("2024-01-01", 100.0),
            equity("2024-01-02", 110.0),
        ];
        let rows = compute_monthly_returns(&curve);
        assert_eq!(rows.len(), 1);
        assert_eq!(rows[0].year, 2024);
        let jan = rows[0].months[0].unwrap();
        assert!((jan - 10.0).abs() < 0.1, "expected ~10%, got {jan}");
        // Feb-Dec should be None
        for m in &rows[0].months[1..] {
            assert!(m.is_none());
        }
    }

    #[test]
    fn multi_day_compounding_within_month() {
        // Two consecutive 10% gains: 1.1 * 1.1 = 1.21 => 21% return
        let curve = vec![
            equity("2024-03-01", 100.0),
            equity("2024-03-15", 110.0),
            equity("2024-03-31", 121.0),
        ];
        let rows = compute_monthly_returns(&curve);
        assert_eq!(rows.len(), 1);
        let mar = rows[0].months[2].unwrap();
        assert!((mar - 21.0).abs() < 0.1, "expected ~21%, got {mar}");
    }

    #[test]
    fn spans_multiple_years() {
        let curve = vec![
            equity("2023-12-01", 100.0),
            equity("2023-12-31", 105.0),
            equity("2024-01-31", 110.0),
        ];
        let rows = compute_monthly_returns(&curve);
        assert_eq!(rows.len(), 2);
        assert_eq!(rows[0].year, 2023);
        assert_eq!(rows[1].year, 2024);
        assert!(rows[0].months[11].is_some()); // December 2023
        assert!(rows[1].months[0].is_some()); // January 2024
    }

    #[test]
    fn negative_return() {
        let curve = vec![
            equity("2024-06-01", 100.0),
            equity("2024-06-30", 90.0),
        ];
        let rows = compute_monthly_returns(&curve);
        let jun = rows[0].months[5].unwrap();
        assert!(jun < 0.0, "expected negative return, got {jun}");
        assert!((jun - (-10.0)).abs() < 0.1);
    }
}
