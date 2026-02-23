//! Domain error types (TRD Section 2.2).

/// A parse error with position information for rule parsing.
#[derive(Debug, Clone, thiserror::Error)]
#[error("parse error at position {position}: {message}")]
pub struct ParseError {
    pub message: String,
    pub position: usize,
}

impl ParseError {
    /// Format the error with a caret pointing at the error position in the input.
    pub fn display_with_context(&self, input: &str) -> String {
        let caret = " ".repeat(self.position) + "^";
        format!(
            "{input}\n{caret}\n{err}",
            input = input,
            caret = caret,
            err = self
        )
    }
}

/// Top-level error type for samtrader.
#[derive(Debug, thiserror::Error)]
pub enum SamtraderError {
    #[error("database error: {reason}")]
    Database { reason: String },

    #[error("database query error: {reason}")]
    DatabaseQuery { reason: String },

    #[error("config parse error in {file}: {reason}")]
    ConfigParse { file: String, reason: String },

    #[error("missing config key [{section}] {key}")]
    ConfigMissing { section: String, key: String },

    #[error("invalid config value [{section}] {key}: {reason}")]
    ConfigInvalid {
        section: String,
        key: String,
        reason: String,
    },

    #[error(transparent)]
    RuleParse(#[from] ParseError),

    #[error("invalid rule: {reason}")]
    RuleInvalid { reason: String },

    #[error("no data for {code} on {exchange}")]
    NoData { code: String, exchange: String },

    #[error("insufficient data for {code} on {exchange}: have {bars} bars, need {minimum}")]
    InsufficientData {
        code: String,
        exchange: String,
        bars: usize,
        minimum: usize,
    },

    #[error(transparent)]
    Io(#[from] std::io::Error),
}

impl From<&SamtraderError> for std::process::ExitCode {
    fn from(err: &SamtraderError) -> Self {
        let code: u8 = match err {
            SamtraderError::Io(_) => 1,
            SamtraderError::ConfigParse { .. }
            | SamtraderError::ConfigMissing { .. }
            | SamtraderError::ConfigInvalid { .. } => 2,
            SamtraderError::Database { .. } | SamtraderError::DatabaseQuery { .. } => 3,
            SamtraderError::RuleParse(_) | SamtraderError::RuleInvalid { .. } => 4,
            SamtraderError::NoData { .. } | SamtraderError::InsufficientData { .. } => 5,
        };
        std::process::ExitCode::from(code)
    }
}
