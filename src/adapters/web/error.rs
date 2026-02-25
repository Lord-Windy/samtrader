//! HTTP error responses for web adapter.

use askama::Template;
use axum::{
    http::{HeaderMap, StatusCode},
    response::{Html, IntoResponse, Response},
};

use crate::domain::error::SamtraderError;

use super::is_htmx_request;

#[derive(Debug)]
pub struct WebError {
    pub status: StatusCode,
    pub message: String,
}

impl WebError {
    pub fn new(status: StatusCode, message: impl Into<String>) -> Self {
        Self {
            status,
            message: message.into(),
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
        let template = super::templates::ErrorTemplate {
            message: &self.message,
            status: self.status.as_u16(),
        };
        match template.render() {
            Ok(html) => (self.status, Html(html)).into_response(),
            Err(_) => (self.status, self.message).into_response(),
        }
    }
}

pub fn status_from_error(err: &SamtraderError) -> StatusCode {
    match err {
        SamtraderError::ConfigMissing { .. }
        | SamtraderError::ConfigInvalid { .. }
        | SamtraderError::ConfigParse { .. } => StatusCode::BAD_REQUEST,
        SamtraderError::NoData { .. } | SamtraderError::InsufficientData { .. } => {
            StatusCode::UNPROCESSABLE_ENTITY
        }
        SamtraderError::RuleParse(_) | SamtraderError::RuleInvalid { .. } => {
            StatusCode::BAD_REQUEST
        }
        SamtraderError::Database { .. }
        | SamtraderError::DatabaseQuery { .. }
        | SamtraderError::Io(_) => StatusCode::INTERNAL_SERVER_ERROR,
    }
}

#[derive(Template)]
#[template(path = "base.html")]
struct BasePage<'a> {
    title: &'a str,
    content: &'a str,
}

pub async fn handle_error(err: SamtraderError, headers: &HeaderMap) -> Response {
    let status = status_from_error(&err);
    let template = super::templates::ErrorTemplate {
        message: &err.to_string(),
        status: status.as_u16(),
    };

    let content = match template.render() {
        Ok(html) => html,
        Err(_) => return (status, err.to_string()).into_response(),
    };

    if is_htmx_request(headers) {
        (status, Html(content)).into_response()
    } else {
        let page = BasePage {
            title: "Error",
            content: &content,
        };
        match page.render() {
            Ok(html) => (status, Html(html)).into_response(),
            Err(_) => (status, content).into_response(),
        }
    }
}
