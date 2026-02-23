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

#[derive(Debug)]
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
    let mut fetch_errors: usize = 0;

    for code in codes {
        let ohlcv = match data_port.fetch_ohlcv(&code, exchange, start_date, end_date) {
            Ok(data) => data,
            Err(e) => {
                eprintln!("Warning: skipping {}.{} ({})", code, exchange, e);
                fetch_errors += 1;
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
        // TRD §14.4: if all failures were DB fetch errors, exit code 3;
        // otherwise exit code 5 (insufficient data).
        if fetch_errors == skipped.len() && fetch_errors > 0 {
            return Err(SamtraderError::Database {
                reason: format!(
                    "failed to fetch data for any code on {}",
                    exchange
                ),
            });
        }
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
    use crate::domain::ohlcv::OhlcvBar;
    use std::collections::HashMap;

    // --- parse_codes tests ---

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

    // --- validate_universe tests ---

    struct MockDataPort {
        data: HashMap<String, Vec<OhlcvBar>>,
        errors: HashMap<String, String>,
    }

    impl MockDataPort {
        fn new() -> Self {
            Self {
                data: HashMap::new(),
                errors: HashMap::new(),
            }
        }

        fn with_bars(mut self, code: &str, count: usize) -> Self {
            let bars: Vec<OhlcvBar> = (0..count)
                .map(|i| OhlcvBar {
                    code: code.to_string(),
                    exchange: "ASX".to_string(),
                    date: NaiveDate::from_ymd_opt(2024, 1, 1).unwrap()
                        + chrono::Duration::days(i as i64),
                    open: 100.0,
                    high: 110.0,
                    low: 90.0,
                    close: 105.0,
                    volume: 1000,
                })
                .collect();
            self.data.insert(code.to_string(), bars);
            self
        }

        fn with_error(mut self, code: &str, reason: &str) -> Self {
            self.errors.insert(code.to_string(), reason.to_string());
            self
        }
    }

    impl DataPort for MockDataPort {
        fn fetch_ohlcv(
            &self,
            code: &str,
            _exchange: &str,
            _start_date: NaiveDate,
            _end_date: NaiveDate,
        ) -> Result<Vec<OhlcvBar>, SamtraderError> {
            if let Some(reason) = self.errors.get(code) {
                return Err(SamtraderError::Database {
                    reason: reason.clone(),
                });
            }
            Ok(self.data.get(code).cloned().unwrap_or_default())
        }

        fn list_symbols(&self, _exchange: &str) -> Result<Vec<String>, SamtraderError> {
            Ok(self.data.keys().cloned().collect())
        }
    }

    fn date(y: i32, m: u32, d: u32) -> NaiveDate {
        NaiveDate::from_ymd_opt(y, m, d).unwrap()
    }

    #[test]
    fn test_validate_all_codes_valid() {
        let port = MockDataPort::new()
            .with_bars("CBA", 50)
            .with_bars("BHP", 40);
        let codes = vec!["CBA".to_string(), "BHP".to_string()];

        let result = validate_universe(&port, codes, "ASX", date(2024, 1, 1), date(2024, 12, 31))
            .unwrap();

        assert_eq!(result.universe.codes, vec!["CBA", "BHP"]);
        assert_eq!(result.universe.exchange, "ASX");
        assert!(result.skipped.is_empty());
    }

    #[test]
    fn test_validate_some_codes_skipped_insufficient_bars() {
        let port = MockDataPort::new()
            .with_bars("CBA", 50)
            .with_bars("XYZ", 10); // below MIN_OHLCV_BARS
        let codes = vec!["CBA".to_string(), "XYZ".to_string()];

        let result = validate_universe(&port, codes, "ASX", date(2024, 1, 1), date(2024, 12, 31))
            .unwrap();

        assert_eq!(result.universe.codes, vec!["CBA"]);
        assert_eq!(result.skipped.len(), 1);
        assert_eq!(result.skipped[0].code, "XYZ");
        assert!(matches!(
            result.skipped[0].reason,
            SkipReason::InsufficientBars { bars: 10 }
        ));
    }

    #[test]
    fn test_validate_some_codes_skipped_no_data() {
        let port = MockDataPort::new().with_bars("CBA", 50);
        // BHP has no data entry so fetch returns empty vec
        let codes = vec!["CBA".to_string(), "BHP".to_string()];

        let result = validate_universe(&port, codes, "ASX", date(2024, 1, 1), date(2024, 12, 31))
            .unwrap();

        assert_eq!(result.universe.codes, vec!["CBA"]);
        assert_eq!(result.skipped.len(), 1);
        assert_eq!(result.skipped[0].code, "BHP");
        assert!(matches!(result.skipped[0].reason, SkipReason::NoData));
    }

    #[test]
    fn test_validate_some_codes_skipped_fetch_error() {
        let port = MockDataPort::new()
            .with_bars("CBA", 50)
            .with_error("BAD", "connection refused");
        let codes = vec!["CBA".to_string(), "BAD".to_string()];

        let result = validate_universe(&port, codes, "ASX", date(2024, 1, 1), date(2024, 12, 31))
            .unwrap();

        assert_eq!(result.universe.codes, vec!["CBA"]);
        assert_eq!(result.skipped.len(), 1);
        assert_eq!(result.skipped[0].code, "BAD");
    }

    #[test]
    fn test_validate_all_codes_fail_insufficient_data_exit_5() {
        let port = MockDataPort::new()
            .with_bars("XYZ", 10)
            .with_bars("FOO", 5);
        let codes = vec!["XYZ".to_string(), "FOO".to_string()];

        let err = validate_universe(&port, codes, "ASX", date(2024, 1, 1), date(2024, 12, 31))
            .unwrap_err();

        assert!(matches!(err, SamtraderError::InsufficientData { .. }));
        let exit_code: std::process::ExitCode = (&err).into();
        assert_eq!(exit_code, std::process::ExitCode::from(5));
    }

    #[test]
    fn test_validate_all_codes_fail_db_errors_exit_3() {
        let port = MockDataPort::new()
            .with_error("CBA", "connection refused")
            .with_error("BHP", "timeout");
        let codes = vec!["CBA".to_string(), "BHP".to_string()];

        let err = validate_universe(&port, codes, "ASX", date(2024, 1, 1), date(2024, 12, 31))
            .unwrap_err();

        assert!(matches!(err, SamtraderError::Database { .. }));
        let exit_code: std::process::ExitCode = (&err).into();
        assert_eq!(exit_code, std::process::ExitCode::from(3));
    }

    #[test]
    fn test_validate_exact_min_bars_is_valid() {
        let port = MockDataPort::new().with_bars("CBA", MIN_OHLCV_BARS);
        let codes = vec!["CBA".to_string()];

        let result = validate_universe(&port, codes, "ASX", date(2024, 1, 1), date(2024, 12, 31))
            .unwrap();

        assert_eq!(result.universe.codes, vec!["CBA"]);
        assert!(result.skipped.is_empty());
    }

    #[test]
    fn test_validate_one_below_min_bars_is_skipped() {
        let port = MockDataPort::new().with_bars("CBA", MIN_OHLCV_BARS - 1);
        let codes = vec!["CBA".to_string()];

        let err = validate_universe(&port, codes, "ASX", date(2024, 1, 1), date(2024, 12, 31))
            .unwrap_err();

        assert!(matches!(err, SamtraderError::InsufficientData { .. }));
    }
}
