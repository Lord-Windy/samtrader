//! HTML templates using Askama.

use askama::Template;
use axum::{
    http::HeaderMap,
    response::{Html, IntoResponse, Response},
};
use chrono::NaiveDate;

use crate::domain::metrics::{CodeResult, Metrics};
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
