#![cfg(feature = "web")]
//! Auth flow integration tests per TRD §14.2.
//!
//! Tests cover:
//! - Login with correct credentials succeeds (redirect to /)
//! - Login with wrong credentials fails (re-renders with error)
//! - Accessing protected route without session redirects to /login
//! - Logout destroys session (subsequent access redirects)

mod common;

use axum::{
    body::Body,
    http::{header, Request, StatusCode},
    Router,
};
use http_body_util::BodyExt;
use samtrader::adapters::web::{build_router, AppState, new_backtest_cache};
use samtrader::ports::config_port::ConfigPort;
use std::sync::{Arc, LazyLock};
use tower::ServiceExt;

use common::*;

const TEST_PASSWORD: &str = "testpass123";
const TEST_USERNAME: &str = "testuser";

static TEST_PASSWORD_HASH: LazyLock<String> = LazyLock::new(|| {
    use argon2::{Algorithm, Argon2, Params, Version, password_hash::SaltString, PasswordHasher};
    let salt = SaltString::from_b64("dGVzdHNhbHR0ZXN0c2FsdA").unwrap();
    let argon2 = Argon2::new(Algorithm::Argon2id, Version::V0x13, Params::default());
    argon2
        .hash_password(TEST_PASSWORD.as_bytes(), &salt)
        .unwrap()
        .to_string()
});

struct AuthMockConfigPort;

impl ConfigPort for AuthMockConfigPort {
    fn get_string(&self, section: &str, key: &str) -> Option<String> {
        match (section, key) {
            ("auth", "username") => Some(TEST_USERNAME.to_string()),
            ("auth", "password_hash") => Some(TEST_PASSWORD_HASH.clone()),
            ("auth", "session_secret") => Some(
                "00000000000000000000000000000001\
                 00000000000000000000000000000001\
                 00000000000000000000000000000001\
                 00000000000000000000000000000001"
                    .to_string(),
            ),
            ("database", "sqlite_path") => Some(":memory:".to_string()),
            _ => None,
        }
    }

    fn get_int(&self, section: &str, key: &str, default: i64) -> i64 {
        match (section, key) {
            ("auth", "session_lifetime") => 86400,
            _ => default,
        }
    }

    fn get_double(&self, _section: &str, _key: &str, default: f64) -> f64 {
        default
    }

    fn get_bool(&self, _section: &str, _key: &str, default: bool) -> bool {
        default
    }
}

async fn create_auth_app() -> Router {
    let bars = generate_bars("BHP", "2024-01-01", 50, 100.0);
    let data_port = MockDataPort::new().with_bars("BHP", bars);
    let state = AppState {
        data_port: Arc::new(data_port),
        config: Arc::new(AuthMockConfigPort),
        backtest_cache: new_backtest_cache(),
    };
    build_router(state).await
}

fn extract_cookies(response: &axum::http::Response<Body>) -> Vec<String> {
    response
        .headers()
        .get_all(header::SET_COOKIE)
        .iter()
        .filter_map(|v| v.to_str().ok())
        .map(|s| s.to_string())
        .collect()
}

fn build_cookie_header(set_cookies: &[String]) -> String {
    set_cookies
        .iter()
        .map(|sc| sc.split(';').next().unwrap_or("").to_string())
        .collect::<Vec<_>>()
        .join("; ")
}

fn login_request(username: &str, password: &str) -> Request<Body> {
    let form_data = format!("username={}&password={}", username, password);
    Request::builder()
        .method("POST")
        .uri("/login")
        .header(header::CONTENT_TYPE, "application/x-www-form-urlencoded")
        .body(Body::from(form_data))
        .unwrap()
}

mod auth_tests {
    use super::*;

    #[tokio::test]
    async fn unauthenticated_access_redirects_to_login() {
        let app = create_auth_app().await;

        let response = app
            .oneshot(Request::builder().uri("/").body(Body::empty()).unwrap())
            .await
            .unwrap();

        // login_required! returns 307 Temporary Redirect with ?next= query param
        assert_eq!(response.status(), StatusCode::TEMPORARY_REDIRECT);
        let location = response
            .headers()
            .get(header::LOCATION)
            .unwrap()
            .to_str()
            .unwrap();
        assert!(
            location.starts_with("/login"),
            "should redirect to /login, got: {location}"
        );
    }

    #[tokio::test]
    async fn login_page_accessible_without_auth() {
        let app = create_auth_app().await;

        let response = app
            .oneshot(
                Request::builder()
                    .uri("/login")
                    .body(Body::empty())
                    .unwrap(),
            )
            .await
            .unwrap();

        assert_eq!(response.status(), StatusCode::OK);
        let body = response.into_body().collect().await.unwrap().to_bytes();
        let html = String::from_utf8_lossy(&body);
        assert!(html.contains("Login"));
    }

    #[tokio::test]
    async fn login_with_correct_credentials_redirects_to_dashboard() {
        let app = create_auth_app().await;

        let response = app
            .oneshot(login_request(TEST_USERNAME, TEST_PASSWORD))
            .await
            .unwrap();

        assert_eq!(response.status(), StatusCode::SEE_OTHER);
        let location = response
            .headers()
            .get(header::LOCATION)
            .unwrap()
            .to_str()
            .unwrap();
        assert_eq!(location, "/");

        let cookies = extract_cookies(&response);
        assert!(!cookies.is_empty(), "login should set a session cookie");
    }

    #[tokio::test]
    async fn login_with_wrong_password_shows_error() {
        let app = create_auth_app().await;

        let response = app
            .oneshot(login_request(TEST_USERNAME, "wrongpassword"))
            .await
            .unwrap();

        assert_eq!(response.status(), StatusCode::OK);
        let body = response.into_body().collect().await.unwrap().to_bytes();
        let html = String::from_utf8_lossy(&body);
        assert!(
            html.contains("Invalid username or password"),
            "should show error message"
        );
    }

    #[tokio::test]
    async fn login_with_wrong_username_shows_error() {
        let app = create_auth_app().await;

        let response = app
            .oneshot(login_request("wronguser", TEST_PASSWORD))
            .await
            .unwrap();

        assert_eq!(response.status(), StatusCode::OK);
        let body = response.into_body().collect().await.unwrap().to_bytes();
        let html = String::from_utf8_lossy(&body);
        assert!(html.contains("Invalid username or password"));
    }

    #[tokio::test]
    async fn authenticated_user_can_access_protected_route() {
        let app = create_auth_app().await;

        // Login
        let login_resp = app
            .clone()
            .oneshot(login_request(TEST_USERNAME, TEST_PASSWORD))
            .await
            .unwrap();
        assert_eq!(login_resp.status(), StatusCode::SEE_OTHER);
        let cookies = extract_cookies(&login_resp);
        let cookie_header = build_cookie_header(&cookies);

        // Access protected route with session cookie
        let response = app
            .oneshot(
                Request::builder()
                    .uri("/")
                    .header(header::COOKIE, &cookie_header)
                    .body(Body::empty())
                    .unwrap(),
            )
            .await
            .unwrap();

        assert_eq!(response.status(), StatusCode::OK);
        let body = response.into_body().collect().await.unwrap().to_bytes();
        let html = String::from_utf8_lossy(&body);
        assert!(html.contains("Dashboard"));
    }

    #[tokio::test]
    async fn logout_redirects_to_login() {
        let app = create_auth_app().await;

        // Login
        let login_resp = app
            .clone()
            .oneshot(login_request(TEST_USERNAME, TEST_PASSWORD))
            .await
            .unwrap();
        let cookies = extract_cookies(&login_resp);
        let cookie_header = build_cookie_header(&cookies);

        // Logout
        let response = app
            .oneshot(
                Request::builder()
                    .method("POST")
                    .uri("/logout")
                    .header(header::COOKIE, &cookie_header)
                    .body(Body::empty())
                    .unwrap(),
            )
            .await
            .unwrap();

        assert_eq!(response.status(), StatusCode::SEE_OTHER);
        let location = response
            .headers()
            .get(header::LOCATION)
            .unwrap()
            .to_str()
            .unwrap();
        assert_eq!(location, "/login");
    }

    #[tokio::test]
    async fn full_flow_login_access_logout_denied() {
        let app = create_auth_app().await;

        // 1. Login with correct credentials
        let login_resp = app
            .clone()
            .oneshot(login_request(TEST_USERNAME, TEST_PASSWORD))
            .await
            .unwrap();
        assert_eq!(login_resp.status(), StatusCode::SEE_OTHER);
        let cookies = extract_cookies(&login_resp);
        let cookie_header = build_cookie_header(&cookies);
        assert!(!cookie_header.is_empty());

        // 2. Access protected route succeeds
        let dash_resp = app
            .clone()
            .oneshot(
                Request::builder()
                    .uri("/")
                    .header(header::COOKIE, &cookie_header)
                    .body(Body::empty())
                    .unwrap(),
            )
            .await
            .unwrap();
        assert_eq!(dash_resp.status(), StatusCode::OK);

        // 3. Logout
        let logout_resp = app
            .clone()
            .oneshot(
                Request::builder()
                    .method("POST")
                    .uri("/logout")
                    .header(header::COOKIE, &cookie_header)
                    .body(Body::empty())
                    .unwrap(),
            )
            .await
            .unwrap();
        assert_eq!(logout_resp.status(), StatusCode::SEE_OTHER);

        // 4. Access protected route denied after logout (307 from login_required!)
        let denied_resp = app
            .oneshot(
                Request::builder()
                    .uri("/")
                    .header(header::COOKIE, &cookie_header)
                    .body(Body::empty())
                    .unwrap(),
            )
            .await
            .unwrap();
        assert_eq!(denied_resp.status(), StatusCode::TEMPORARY_REDIRECT);
        let location = denied_resp
            .headers()
            .get(header::LOCATION)
            .unwrap()
            .to_str()
            .unwrap();
        assert!(
            location.starts_with("/login"),
            "should redirect to /login, got: {location}"
        );
    }
}
