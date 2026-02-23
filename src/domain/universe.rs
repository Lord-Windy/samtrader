//! Universe module for multi-code backtesting (TRD Section 7).
//!
//! Parses code lists from configuration and validates that each code has
//! sufficient data for backtesting.

use crate::domain::error::SamtraderError;
use crate::ports::data_port::DataPort;
use chrono::NaiveDate;
use std::collections::HashSet;

pub const MIN_OHLCV_BARS: usize = 30;

#[derive(Debug, Clone)]
pub struct Universe {
    pub codes: Vec<String>,
    pub exchange: String,
}

impl Universe {
    pub fn count(&self) -> usize {
        self.codes.len()
    }
}

#[derive(Debug, Clone, thiserror::Error)]
pub enum UniverseError {
    #[error("empty token in code list")]
    EmptyToken,

    #[error("duplicate code: {0}")]
    DuplicateCode(String),

    #[error("all codes failed validation")]
    AllCodesFailed,
}

pub fn parse_codes(input: &str) -> Result<Vec<String>, UniverseError> {
    let mut codes = Vec::new();
    let mut seen = HashSet::new();

    for token in input.split(',') {
        let trimmed = token.trim();
        if trimmed.is_empty() {
            return Err(UniverseError::EmptyToken);
        }
        let code = trimmed.to_uppercase();
        if seen.contains(&code) {
            return Err(UniverseError::DuplicateCode(code));
        }
        seen.insert(code.clone());
        codes.push(code);
    }

    Ok(codes)
}

pub struct UniverseValidationResult {
    pub universe: Universe,
    pub skipped: Vec<SkippedCode>,
}

#[derive(Debug, Clone)]
pub struct SkippedCode {
    pub code: String,
    pub reason: SkipReason,
}

#[derive(Debug, Clone)]
pub enum SkipReason {
    NoData,
    InsufficientBars { bars: usize },
}

pub fn validate_universe(
    data_port: &dyn DataPort,
    codes: Vec<String>,
    exchange: &str,
    start_date: NaiveDate,
    end_date: NaiveDate,
) -> Result<UniverseValidationResult, SamtraderError> {
    let mut valid_codes = Vec::new();
    let mut skipped = Vec::new();

    for code in codes {
        let ohlcv = match data_port.fetch_ohlcv(&code, exchange, start_date, end_date) {
            Ok(data) => data,
            Err(e) => {
                eprintln!("Warning: skipping {}.{} ({})", code, exchange, e);
                skipped.push(SkippedCode {
                    code: code.clone(),
                    reason: SkipReason::NoData,
                });
                continue;
            }
        };

        if ohlcv.is_empty() {
            eprintln!("Warning: skipping {}.{} (no data found)", code, exchange);
            skipped.push(SkippedCode {
                code: code.clone(),
                reason: SkipReason::NoData,
            });
            continue;
        }

        if ohlcv.len() < MIN_OHLCV_BARS {
            eprintln!(
                "Warning: skipping {}.{} (only {} bars, minimum {} required)",
                code,
                exchange,
                ohlcv.len(),
                MIN_OHLCV_BARS
            );
            skipped.push(SkippedCode {
                code: code.clone(),
                reason: SkipReason::InsufficientBars { bars: ohlcv.len() },
            });
            continue;
        }

        eprintln!("  {}: {} bars [OK]", code, ohlcv.len());
        valid_codes.push(code);
    }

    if valid_codes.is_empty() {
        return Err(SamtraderError::InsufficientData {
            code: "all".to_string(),
            exchange: exchange.to_string(),
            bars: 0,
            minimum: MIN_OHLCV_BARS,
        });
    }

    if !skipped.is_empty() {
        eprintln!(
            "Backtesting {} of {} codes on {}",
            valid_codes.len(),
            valid_codes.len() + skipped.len(),
            exchange
        );
    }

    Ok(UniverseValidationResult {
        universe: Universe {
            codes: valid_codes,
            exchange: exchange.to_string(),
        },
        skipped,
    })
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_parse_codes_basic() {
        let result = parse_codes("CBA,BHP,WBC,NAB").unwrap();
        assert_eq!(result, vec!["CBA", "BHP", "WBC", "NAB"]);
    }

    #[test]
    fn test_parse_codes_with_whitespace() {
        let result = parse_codes("  CBA , BHP ,WBC,  NAB  ").unwrap();
        assert_eq!(result, vec!["CBA", "BHP", "WBC", "NAB"]);
    }

    #[test]
    fn test_parse_codes_uppercase() {
        let result = parse_codes("cba,bhp,wbc").unwrap();
        assert_eq!(result, vec!["CBA", "BHP", "WBC"]);
    }

    #[test]
    fn test_parse_codes_single() {
        let result = parse_codes("CBA").unwrap();
        assert_eq!(result, vec!["CBA"]);
    }

    #[test]
    fn test_parse_codes_empty_token() {
        let result = parse_codes("CBA,,BHP");
        assert!(matches!(result, Err(UniverseError::EmptyToken)));
    }

    #[test]
    fn test_parse_codes_duplicate() {
        let result = parse_codes("CBA,BHP,CBA");
        assert!(matches!(result, Err(UniverseError::DuplicateCode(s)) if s == "CBA"));
    }

    #[test]
    fn test_universe_count() {
        let universe = Universe {
            codes: vec!["CBA".to_string(), "BHP".to_string()],
            exchange: "ASX".to_string(),
        };
        assert_eq!(universe.count(), 2);
    }
}
