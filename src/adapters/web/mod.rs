//! Web server adapter (TRD Section 4).
//!
//! Provides Axum web server with HTMX-based frontend for running backtests
//! and viewing reports through a browser.

pub mod auth;
mod error;
mod handlers;
pub mod templates;

pub use error::WebError;
pub use handlers::*;
pub use templates::*;

use axum::{
    Router,
    routing::{get, post},
};
use axum_login::{login_required, AuthManagerLayerBuilder};
use axum_login::tower_sessions::{Expiry, SessionManagerLayer, cookie::Key};
use std::sync::Arc;
use tower_http::services::ServeDir;
use tower_sessions_rusqlite_store::RusqliteStore;

use crate::ports::data_port::DataPort;
use crate::ports::config_port::ConfigPort;

pub struct AppState {
    pub data_port: Arc<dyn DataPort + Send + Sync>,
    pub config: Arc<dyn ConfigPort + Send + Sync>,
}

pub async fn build_router(state: AppState) -> Router {
    // Read auth config
    let username = state
        .config
        .get_string("auth", "username")
        .unwrap_or_else(|| "admin".to_string());
    let password_hash = state
        .config
        .get_string("auth", "password_hash")
        .expect("auth.password_hash must be set in config");
    let session_lifetime = state.config.get_int("auth", "session_lifetime", 86400);
    let session_secret = state
        .config
        .get_string("auth", "session_secret")
        .expect("auth.session_secret must be set in config");
    let secret_bytes =
        hex::decode(&session_secret).expect("auth.session_secret must be valid hex");
    let key = Key::from(&secret_bytes);

    // Session store: open a separate tokio-rusqlite connection
    let db_path = state
        .config
        .get_string("database", "sqlite_path")
        .unwrap_or_else(|| "samtrader.db".to_string());
    let session_conn = tower_sessions_rusqlite_store::tokio_rusqlite::Connection::open(&db_path)
        .await
        .expect("failed to open session database");
    let session_store = RusqliteStore::new(session_conn);
    session_store
        .migrate()
        .await
        .expect("failed to migrate session table");

    // Auth backend and layers
    let backend = auth::Backend::new(username, password_hash);
    let session_layer = SessionManagerLayer::new(session_store)
        .with_signed(key)
        .with_secure(true)
        .with_expiry(Expiry::OnInactivity(time::Duration::seconds(
            session_lifetime,
        )));
    let auth_layer = AuthManagerLayerBuilder::new(backend, session_layer).build();

    Router::new()
        // Protected routes (require login)
        .route("/", get(handlers::dashboard))
        .route("/htmx.js", get(handlers::htmx_js))
        .route("/backtest", get(handlers::backtest_form))
        .route("/backtest/run", post(handlers::run_backtest))
        .route("/report/{id}", get(handlers::view_report))
        .route(
            "/report/{id}/equity-chart",
            get(handlers::equity_chart_svg),
        )
        .route(
            "/report/{id}/drawdown-chart",
            get(handlers::drawdown_chart_svg),
        )
        .route("/logout", post(handlers::logout))
        .route_layer(login_required!(auth::Backend, login_url = "/login"))
        // Public routes
        .route("/login", get(handlers::login_form).post(handlers::login))
        .nest_service("/static", ServeDir::new("static"))
        .fallback(handlers::not_found)
        .layer(auth_layer)
        .with_state(Arc::new(state))
}

fn is_htmx_request(headers: &axum::http::HeaderMap) -> bool {
    headers.get("HX-Request").is_some()
}
