//! HTTP error responses for web adapter.

use axum::{
    http::StatusCode,
    response::{IntoResponse, Response},
};

use crate::domain::error::SamtraderError;

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
        (self.status, template).into_response()
    }
}
