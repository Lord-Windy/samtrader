//! Web handler integration tests per TRD §14.2.
//!
//! Tests cover:
//! - Dashboard renders with expected content
//! - Backtest form renders with form fields
//! - Backtest submission returns report with expected sections
//! - Report page contains equity chart, metrics table, trade log
//! - HTMX fragment vs full page responses

mod common;

use axum::{
    body::Body,
    http::{header, Request, StatusCode},
    Router,
};
use http_body_util::BodyExt;
use samtrader::adapters::web::{build_router, AppState};
use samtrader::ports::config_port::ConfigPort;
use samtrader::domain::ohlcv::OhlcvBar;
use std::sync::Arc;
use tower::ServiceExt;

use common::*;

struct MockConfigPort;

impl ConfigPort for MockConfigPort {
    fn get_string(&self, _section: &str, _key: &str) -> Option<String> {
        None
    }

    fn get_int(&self, _section: &str, _key: &str, default: i64) -> i64 {
        default
    }

    fn get_double(&self, _section: &str, _key: &str, default: f64) -> f64 {
        default
    }

    fn get_bool(&self, _section: &str, _key: &str, default: bool) -> bool {
        default
    }
}

fn create_test_app() -> Router {
    let bars = generate_bars("BHP", "2024-01-01", 50, 100.0);
    let data_port = MockDataPort::new().with_bars("BHP", bars);
    
    let state = AppState {
        data_port: Arc::new(data_port),
        config: Arc::new(MockConfigPort),
    };
    
    build_router(state)
}

fn create_test_app_with_data(codes: &[(&str, Vec<OhlcvBar>)]) -> Router {
    let mut port = MockDataPort::new();
    for (code, bars) in codes {
        port = port.with_bars(code, bars.clone());
    }
    
    let state = AppState {
        data_port: Arc::new(port),
        config: Arc::new(MockConfigPort),
    };
    
    build_router(state)
}

mod dashboard_tests {
    use super::*;

    #[tokio::test]
    async fn dashboard_renders_with_ok_status() {
        let app = create_test_app();
        
        let response = app
            .oneshot(Request::builder().uri("/").body(Body::empty()).unwrap())
            .await
            .unwrap();
        
        assert_eq!(response.status(), StatusCode::OK);
    }

    #[tokio::test]
    async fn dashboard_contains_title() {
        let app = create_test_app();
        
        let response = app
            .oneshot(Request::builder().uri("/").body(Body::empty()).unwrap())
            .await
            .unwrap();
        
        let body = response.into_body().collect().await.unwrap().to_bytes();
        let html = String::from_utf8_lossy(&body);
        
        assert!(html.contains("Dashboard"));
    }

    #[tokio::test]
    async fn dashboard_htmx_fragment_excludes_html_wrapper() {
        let app = create_test_app();
        
        let response = app
            .oneshot(
                Request::builder()
                    .uri("/")
                    .header("HX-Request", "true")
                    .body(Body::empty())
                    .unwrap(),
            )
            .await
            .unwrap();
        
        let body = response.into_body().collect().await.unwrap().to_bytes();
        let html = String::from_utf8_lossy(&body);
        
        assert!(html.contains("<div id=\"content\">"));
    }
}

mod backtest_form_tests {
    use super::*;

    #[tokio::test]
    async fn backtest_form_renders_with_ok_status() {
        let app = create_test_app();
        
        let response = app
            .oneshot(Request::builder().uri("/backtest").body(Body::empty()).unwrap())
            .await
            .unwrap();
        
        assert_eq!(response.status(), StatusCode::OK);
    }

    #[tokio::test]
    async fn backtest_form_contains_required_fields() {
        let app = create_test_app();
        
        let response = app
            .oneshot(Request::builder().uri("/backtest").body(Body::empty()).unwrap())
            .await
            .unwrap();
        
        let body = response.into_body().collect().await.unwrap().to_bytes();
        let html = String::from_utf8_lossy(&body);
        
        assert!(html.contains("name=\"codes\""));
        assert!(html.contains("name=\"start_date\""));
        assert!(html.contains("name=\"end_date\""));
        assert!(html.contains("name=\"entry_rule\""));
        assert!(html.contains("name=\"exit_rule\""));
    }

    #[tokio::test]
    async fn backtest_form_contains_htmx_attributes() {
        let app = create_test_app();
        
        let response = app
            .oneshot(Request::builder().uri("/backtest").body(Body::empty()).unwrap())
            .await
            .unwrap();
        
        let body = response.into_body().collect().await.unwrap().to_bytes();
        let html = String::from_utf8_lossy(&body);
        
        assert!(html.contains("hx-post"));
        assert!(html.contains("hx-target"));
    }
}

mod backtest_submission_tests {
    use super::*;

    fn create_backtest_request() -> Request<Body> {
        let form_data = "codes=BHP&start_date=2024-01-01&end_date=2024-01-31&initial_capital=100000&entry_rule=ABOVE(close%2C%20100)&exit_rule=BELOW(close%2C%20100)&position_size=0.25&max_positions=1";
        
        Request::builder()
            .method("POST")
            .uri("/backtest/run")
            .header(header::CONTENT_TYPE, "application/x-www-form-urlencoded")
            .body(Body::from(form_data))
            .unwrap()
    }

    #[tokio::test]
    async fn backtest_submission_returns_ok_status() {
        let bars = generate_bars("BHP", "2024-01-01", 50, 100.0);
        let app = create_test_app_with_data(&[("BHP", bars)]);
        
        let response = app.oneshot(create_backtest_request()).await.unwrap();
        let status = response.status();
        let body = response.into_body().collect().await.unwrap().to_bytes();
        let html = String::from_utf8_lossy(&body);
        
        assert_eq!(status, StatusCode::OK, "Response body: {}", html);
    }

    #[tokio::test]
    async fn backtest_submission_returns_report_content() {
        let bars = generate_bars("BHP", "2024-01-01", 50, 100.0);
        let app = create_test_app_with_data(&[("BHP", bars)]);
        
        let response = app.oneshot(create_backtest_request()).await.unwrap();
        
        let body = response.into_body().collect().await.unwrap().to_bytes();
        let html = String::from_utf8_lossy(&body);
        
        assert!(html.contains("Backtest Report"));
    }

    #[tokio::test]
    async fn backtest_submission_returns_metrics_table() {
        let bars = generate_bars("BHP", "2024-01-01", 50, 100.0);
        let app = create_test_app_with_data(&[("BHP", bars)]);
        
        let response = app.oneshot(create_backtest_request()).await.unwrap();
        
        let body = response.into_body().collect().await.unwrap().to_bytes();
        let html = String::from_utf8_lossy(&body);
        
        assert!(html.contains("Total Return"));
        assert!(html.contains("Sharpe Ratio"));
        assert!(html.contains("Max Drawdown"));
        assert!(html.contains("Win Rate"));
    }

    #[tokio::test]
    async fn backtest_submission_returns_equity_chart() {
        let bars = generate_bars("BHP", "2024-01-01", 50, 100.0);
        let app = create_test_app_with_data(&[("BHP", bars)]);
        
        let response = app.oneshot(create_backtest_request()).await.unwrap();
        
        let body = response.into_body().collect().await.unwrap().to_bytes();
        let html = String::from_utf8_lossy(&body);
        
        assert!(html.contains("Equity Chart"));
        assert!(html.contains("<svg"));
    }

    #[tokio::test]
    async fn backtest_submission_returns_drawdown_chart() {
        let bars = generate_bars("BHP", "2024-01-01", 50, 100.0);
        let app = create_test_app_with_data(&[("BHP", bars)]);
        
        let response = app.oneshot(create_backtest_request()).await.unwrap();
        
        let body = response.into_body().collect().await.unwrap().to_bytes();
        let html = String::from_utf8_lossy(&body);
        
        assert!(html.contains("Drawdown Chart"));
    }

    #[tokio::test]
    async fn backtest_submission_shows_strategy_section() {
        let bars = generate_bars("BHP", "2024-01-01", 50, 100.0);
        let app = create_test_app_with_data(&[("BHP", bars)]);
        
        let form_data = "codes=BHP&start_date=2024-01-01&end_date=2024-01-31&initial_capital=100000&entry_rule=ABOVE(close%2C%20100)&exit_rule=BELOW(close%2C%20100)&position_size=0.25&max_positions=1";
        
        let response = app
            .oneshot(
                Request::builder()
                    .method("POST")
                    .uri("/backtest/run")
                    .header(header::CONTENT_TYPE, "application/x-www-form-urlencoded")
                    .body(Body::from(form_data))
                    .unwrap(),
            )
            .await
            .unwrap();
        
        let body = response.into_body().collect().await.unwrap().to_bytes();
        let html = String::from_utf8_lossy(&body);
        
        assert!(html.contains("Strategy"));
        assert!(html.contains("Position Size"));
    }

    #[tokio::test]
    async fn backtest_submission_htmx_fragment() {
        let bars = generate_bars("BHP", "2024-01-01", 50, 100.0);
        let app = create_test_app_with_data(&[("BHP", bars)]);
        
        let form_data = "codes=BHP&start_date=2024-01-01&end_date=2024-01-31&initial_capital=100000&entry_rule=ABOVE(close%2C%20100)&exit_rule=BELOW(close%2C%20100)&position_size=0.25&max_positions=1";
        
        let response = app
            .oneshot(
                Request::builder()
                    .method("POST")
                    .uri("/backtest/run")
                    .header(header::CONTENT_TYPE, "application/x-www-form-urlencoded")
                    .header("HX-Request", "true")
                    .body(Body::from(form_data))
                    .unwrap(),
            )
            .await
            .unwrap();
        
        let body = response.into_body().collect().await.unwrap().to_bytes();
        let html = String::from_utf8_lossy(&body);
        
        assert!(html.contains("<div id=\"report-content\">"));
    }
}

mod error_handling_tests {
    use super::*;

    #[tokio::test]
    async fn backtest_with_invalid_date_returns_error() {
        let app = create_test_app();
        
        let form_data = "codes=BHP&start_date=invalid&end_date=2024-02-20&initial_capital=100000&entry_rule=ABOVE(close%2C%20100)&exit_rule=BELOW(close%2C%20100)&position_size=0.25&max_positions=1";
        
        let response = app
            .oneshot(
                Request::builder()
                    .method("POST")
                    .uri("/backtest/run")
                    .header(header::CONTENT_TYPE, "application/x-www-form-urlencoded")
                    .body(Body::from(form_data))
                    .unwrap(),
            )
            .await
            .unwrap();
        
        assert_eq!(response.status(), StatusCode::BAD_REQUEST);
    }

    #[tokio::test]
    async fn backtest_with_empty_codes_returns_error() {
        let app = create_test_app();
        
        let form_data = "codes=&start_date=2024-01-01&end_date=2024-02-20&initial_capital=100000&entry_rule=ABOVE(close%2C%20100)&exit_rule=BELOW(close%2C%20100)&position_size=0.25&max_positions=1";
        
        let response = app
            .oneshot(
                Request::builder()
                    .method("POST")
                    .uri("/backtest/run")
                    .header(header::CONTENT_TYPE, "application/x-www-form-urlencoded")
                    .body(Body::from(form_data))
                    .unwrap(),
            )
            .await
            .unwrap();
        
        assert_eq!(response.status(), StatusCode::BAD_REQUEST);
    }

    #[tokio::test]
    async fn backtest_with_invalid_rule_returns_error() {
        let app = create_test_app();
        
        let form_data = "codes=BHP&start_date=2024-01-01&end_date=2024-02-20&initial_capital=100000&entry_rule=invalid%20rule&exit_rule=BELOW(close%2C%20100)&position_size=0.25&max_positions=1";
        
        let response = app
            .oneshot(
                Request::builder()
                    .method("POST")
                    .uri("/backtest/run")
                    .header(header::CONTENT_TYPE, "application/x-www-form-urlencoded")
                    .body(Body::from(form_data))
                    .unwrap(),
            )
            .await
            .unwrap();
        
        assert_eq!(response.status(), StatusCode::BAD_REQUEST);
    }
}

mod multi_code_backtest_tests {
    use super::*;

    #[tokio::test]
    async fn multi_code_backtest_returns_ok() {
        let bhp_bars = generate_bars("BHP", "2024-01-01", 50, 100.0);
        let cba_bars = generate_bars("CBA", "2024-01-01", 50, 50.0);
        let app = create_test_app_with_data(&[("BHP", bhp_bars), ("CBA", cba_bars)]);
        
        let form_data = "codes=BHP,CBA&start_date=2024-01-01&end_date=2024-01-31&initial_capital=100000&entry_rule=ABOVE(close%2C%2050)&exit_rule=BELOW(close%2C%2050)&position_size=0.25&max_positions=2";
        
        let response = app
            .oneshot(
                Request::builder()
                    .method("POST")
                    .uri("/backtest/run")
                    .header(header::CONTENT_TYPE, "application/x-www-form-urlencoded")
                    .body(Body::from(form_data))
                    .unwrap(),
            )
            .await
            .unwrap();
        
        let status = response.status();
        let body = response.into_body().collect().await.unwrap().to_bytes();
        let html = String::from_utf8_lossy(&body);
        
        assert_eq!(status, StatusCode::OK, "HTML: {}", html);
        assert!(html.contains("Backtest Report"));
    }
}
