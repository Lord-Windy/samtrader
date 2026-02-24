//! HTTP request handlers for web adapter.

use axum::{
    extract::State,
    http::HeaderMap,
    response::{Html, IntoResponse, Response},
    Form,
};
use std::sync::Arc;

use crate::domain::backtest::{run_backtest as run_backtest_engine, BacktestConfig};
use crate::domain::code_data::{build_unified_timeline, CodeData};
use crate::domain::indicator_helpers::compute_indicators;
use crate::domain::metrics::{CodeResult, Metrics};
use crate::domain::rule::extract_indicators;
use crate::domain::strategy::Strategy;
use crate::domain::universe::{validate_universe, SkipReason};

use super::{is_htmx_request, AppState, WebError};

pub async fn dashboard(
    State(_state): State<Arc<AppState>>,
    headers: HeaderMap,
) -> Result<Response, WebError> {
    let template = super::templates::DashboardTemplate {
        recent_backtests: &[],
    };
    
    if is_htmx_request(&headers) {
        Ok(Html(template.fragment()).into_response())
    } else {
        Ok(template.into_response())
    }
}

pub async fn backtest_form(
    State(state): State<Arc<AppState>>,
    headers: HeaderMap,
) -> Result<Response, WebError> {
    let symbols = state.data_port.list_symbols("ASX").unwrap_or_default();
    let template = super::templates::BacktestFormTemplate {
        symbols: &symbols,
        default_start: "2020-01-01",
        default_end: "2024-12-31",
    };
    
    if is_htmx_request(&headers) {
        Ok(Html(template.fragment()).into_response())
    } else {
        Ok(template.into_response())
    }
}

#[derive(Debug, serde::Deserialize)]
pub struct BacktestFormData {
    pub codes: String,
    pub start_date: String,
    pub end_date: String,
    pub initial_capital: String,
    pub entry_rule: String,
    pub exit_rule: String,
    pub position_size: String,
    pub max_positions: String,
}

pub async fn run_backtest(
    State(state): State<Arc<AppState>>,
    headers: HeaderMap,
    Form(form): Form<BacktestFormData>,
) -> Result<Response, WebError> {
    let start_date = chrono::NaiveDate::parse_from_str(&form.start_date, "%Y-%m-%d")
        .map_err(|_| WebError::bad_request("Invalid start date format"))?;
    let end_date = chrono::NaiveDate::parse_from_str(&form.end_date, "%Y-%m-%d")
        .map_err(|_| WebError::bad_request("Invalid end date format"))?;
    
    let codes: Vec<String> = form.codes
        .split(',')
        .map(|s| s.trim().to_uppercase())
        .filter(|s| !s.is_empty())
        .collect();
    
    if codes.is_empty() {
        return Err(WebError::bad_request("No codes specified"));
    }
    
    let initial_capital: f64 = form.initial_capital.parse()
        .map_err(|_| WebError::bad_request("Invalid initial capital"))?;
    let position_size: f64 = form.position_size.parse()
        .map_err(|_| WebError::bad_request("Invalid position size"))?;
    let max_positions: usize = form.max_positions.parse()
        .map_err(|_| WebError::bad_request("Invalid max positions"))?;
    
    let entry_long = crate::domain::rule_parser::parse(&form.entry_rule)
        .map_err(|e| WebError::bad_request(format!("Entry rule parse error: {}", e)))?;
    let exit_long = crate::domain::rule_parser::parse(&form.exit_rule)
        .map_err(|e| WebError::bad_request(format!("Exit rule parse error: {}", e)))?;
    
    let strategy = Strategy {
        name: "Web Backtest".to_string(),
        description: "Submitted via web form".to_string(),
        entry_long,
        exit_long,
        entry_short: None,
        exit_short: None,
        position_size,
        stop_loss_pct: 0.0,
        take_profit_pct: 0.0,
        max_positions,
    };
    
    let bt_config = BacktestConfig {
        start_date,
        end_date,
        initial_capital,
        commission_per_trade: 0.0,
        commission_pct: 0.0,
        slippage_pct: 0.0,
        allow_shorting: false,
        risk_free_rate: 0.05,
    };
    
    let validation = validate_universe(
        &*state.data_port,
        codes.clone(),
        "ASX",
        start_date,
        end_date,
    ).map_err(|e| WebError::bad_request(e.to_string()))?;
    
    let valid_codes = &validation.universe.codes;
    let indicator_types = extract_indicators(&strategy.entry_long)
        .into_iter()
        .chain(extract_indicators(&strategy.exit_long))
        .collect::<Vec<_>>();
    
    let mut code_data_vec: Vec<CodeData> = Vec::with_capacity(valid_codes.len());
    
    for code in valid_codes {
        let ohlcv = state.data_port.fetch_ohlcv(
            code,
            "ASX",
            start_date,
            end_date,
        ).map_err(|e| WebError::internal(e.to_string()))?;
        
        let indicators = compute_indicators(&ohlcv, &indicator_types);
        let mut cd = CodeData::new(code.to_string(), "ASX".to_string(), ohlcv);
        cd.indicators = indicators;
        code_data_vec.push(cd);
    }
    
    if code_data_vec.is_empty() {
        return Err(WebError::bad_request("No valid codes with data"));
    }
    
    let timeline = build_unified_timeline(&code_data_vec);
    let result = run_backtest_engine(&code_data_vec, &timeline, &strategy, &bt_config);
    let metrics = Metrics::compute(&result.portfolio, bt_config.risk_free_rate);
    let code_results = CodeResult::compute_per_code(&result.portfolio.closed_trades);
    
    let equity_svg = crate::adapters::typst_report::chart_svg::generate_equity_svg(
        &result.portfolio.equity_curve,
    );
    let drawdown_svg = crate::adapters::typst_report::chart_svg::generate_drawdown_svg(
        &result.portfolio.equity_curve,
    );
    
    let skipped: Vec<super::templates::SkippedCode> = validation.skipped
        .iter()
        .map(|s| super::templates::SkippedCode {
            code: &s.code,
            reason: match &s.reason {
                SkipReason::NoData => "No data available",
                SkipReason::InsufficientBars { .. } => "Insufficient bars",
            },
        })
        .collect();
    
    let template = super::templates::ReportTemplate {
        strategy: &strategy,
        metrics: &metrics,
        code_results: if code_results.is_empty() { None } else { Some(&code_results) },
        equity_svg: &equity_svg,
        drawdown_svg: &drawdown_svg,
        trades: &result.portfolio.closed_trades,
        skipped: &skipped,
        start_date,
        end_date,
        initial_capital,
    };
    
    if is_htmx_request(&headers) {
        Ok(Html(template.fragment()).into_response())
    } else {
        Ok(template.into_response())
    }
}

pub async fn view_report(
    State(_state): State<Arc<AppState>>,
    headers: HeaderMap,
) -> Result<Response, WebError> {
    let template = super::templates::ReportPlaceholderTemplate;
    
    if is_htmx_request(&headers) {
        Ok(Html(template.fragment()).into_response())
    } else {
        Ok(template.into_response())
    }
}
