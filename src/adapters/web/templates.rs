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
struct BasePage<'a> {
    title: &'a str,
    content: &'a str,
}

/// Render a template as an HTMX fragment or full page depending on headers.
pub fn render_page(
    template: &impl Template,
    title: &str,
    headers: &HeaderMap,
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
    pub monthly_returns: &'a str,
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

pub fn render_monthly_returns_html(equity_curve: &[EquityPoint]) -> String {
    if equity_curve.len() < 2 {
        return String::new();
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
        returns.insert(key, compounded);
    }

    let min_year = returns.keys().map(|k| k.0).min().unwrap_or(2020);
    let max_year = returns.keys().map(|k| k.0).max().unwrap_or(2020);

    let mut out = String::from(
        r#"<table class="monthly-returns">
<thead>
<tr><th>Year</th><th>Jan</th><th>Feb</th><th>Mar</th><th>Apr</th><th>May</th><th>Jun</th><th>Jul</th><th>Aug</th><th>Sep</th><th>Oct</th><th>Nov</th><th>Dec</th></tr>
</thead>
<tbody>
"#,
    );

    for year in min_year..=max_year {
        out.push_str("<tr>");
        out.push_str(&format!("<td><strong>{}</strong></td>", year));
        for month in 1..=12u32 {
            if let Some(&ret) = returns.get(&(year, month)) {
                let pct = ret * 100.0;
                let class = if pct > 0.0 {
                    "positive"
                } else if pct < 0.0 {
                    "negative"
                } else {
                    "neutral"
                };
                out.push_str(&format!(r#"<td class="{}">{:.1}%</td>"#, class, pct));
            } else {
                out.push_str(r#"<td class="neutral">-</td>"#);
            }
        }
        out.push_str("</tr>\n");
    }

    out.push_str("</tbody>\n</table>");
    out
}
