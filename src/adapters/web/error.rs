//! HTTP error responses for web adapter.

use askama::Template;
use axum::{
    http::{HeaderMap, StatusCode},
    response::{Html, IntoResponse, Response},
};

use crate::domain::error::SamtraderError;

use super::is_htmx_request;
use super::templates::{BasePage, ErrorTemplate};

#[derive(Debug)]
pub struct WebError {
    pub status: StatusCode,
    pub message: String,
    headers: Option<HeaderMap>,
}

impl WebError {
    pub fn new(status: StatusCode, message: impl Into<String>) -> Self {
        Self {
            status,
            message: message.into(),
            headers: None,
        }
    }

    pub fn not_found(message: impl Into<String>) -> Self {
        Self::new(StatusCode::NOT_FOUND, message)
    }

    pub fn bad_request(message: impl Into<String>) -> Self {
        Self::new(StatusCode::BAD_REQUEST, message)
    }

    pub fn internal(message: impl Into<String>) -> Self {
        Self::new(StatusCode::INTERNAL_SERVER_ERROR, message)
    }

    /// Attach request headers so the error response can distinguish HTMX vs full-page.
    pub fn with_headers(mut self, headers: HeaderMap) -> Self {
        self.headers = Some(headers);
        self
    }
}

impl From<SamtraderError> for WebError {
    fn from(err: SamtraderError) -> Self {
        let status = match &err {
            SamtraderError::ConfigMissing { .. }
            | SamtraderError::ConfigInvalid { .. }
            | SamtraderError::ConfigParse { .. } => StatusCode::BAD_REQUEST,
            SamtraderError::NoData { .. } | SamtraderError::InsufficientData { .. } => {
                StatusCode::UNPROCESSABLE_ENTITY
            }
            SamtraderError::RuleParse { .. } | SamtraderError::RuleInvalid { .. } => {
                StatusCode::BAD_REQUEST
            }
            _ => StatusCode::INTERNAL_SERVER_ERROR,
        };
        Self::new(status, err.to_string())
    }
}

impl IntoResponse for WebError {
    fn into_response(self) -> Response {
        let template = ErrorTemplate {
            message: &self.message,
            status: self.status.as_u16(),
        };

        let content = match template.render() {
            Ok(html) => html,
            Err(_) => return (self.status, self.message).into_response(),
        };

        let is_htmx = self.headers.as_ref().map_or(false, |h| is_htmx_request(h));

        if is_htmx {
            (self.status, Html(content)).into_response()
        } else {
            let page = BasePage {
                title: "Error",
                content: &content,
                nav_path: "",
            };
            match page.render() {
                Ok(html) => (self.status, Html(html)).into_response(),
                Err(_) => (self.status, Html(content)).into_response(),
            }
        }
    }
}
