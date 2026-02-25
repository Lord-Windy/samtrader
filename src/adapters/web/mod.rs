//! Web server adapter (TRD Section 4).
//!
//! Provides Axum web server with HTMX-based frontend for running backtests
//! and viewing reports through a browser.

mod error;
mod handlers;
mod templates;

pub use error::WebError;
pub use handlers::*;
pub use templates::*;

use axum::{
    Router,
    routing::{get, post},
};
use std::sync::Arc;
use tower_http::services::ServeDir;

use crate::ports::data_port::DataPort;
use crate::ports::config_port::ConfigPort;

pub struct AppState {
    pub data_port: Arc<dyn DataPort + Send + Sync>,
    pub config: Arc<dyn ConfigPort + Send + Sync>,
}

pub fn build_router(state: AppState) -> Router {
    Router::new()
        .route("/", get(handlers::dashboard))
        .route("/login", get(handlers::login_form).post(handlers::login))
        .route("/logout", post(handlers::logout))
        .route("/backtest", get(handlers::backtest_form))
        .route("/backtest/run", post(handlers::run_backtest))
        .route("/report/{id}", get(handlers::view_report))
        .route("/report/{id}/equity-chart", get(handlers::equity_chart_svg))
        .route("/report/{id}/drawdown-chart", get(handlers::drawdown_chart_svg))
        .nest_service("/static", ServeDir::new("static"))
        .fallback(handlers::not_found)
        .with_state(Arc::new(state))
}

fn is_htmx_request(headers: &axum::http::HeaderMap) -> bool {
    headers.get("HX-Request").is_some()
}
