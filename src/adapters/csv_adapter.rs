//! CSV file data adapter (TRD Section 2.2).

use crate::domain::error::SamtraderError;
use crate::domain::ohlcv::OhlcvBar;
use crate::ports::data_port::DataPort;
use chrono::NaiveDate;
use std::fs;
use std::path::PathBuf;

pub struct CsvAdapter {
    base_path: PathBuf,
}

impl CsvAdapter {
    pub fn new(base_path: PathBuf) -> Self {
        Self { base_path }
    }

    fn csv_path(&self, code: &str, exchange: &str) -> PathBuf {
        self.base_path.join(format!("{}_{}.csv", code, exchange))
    }
}

impl DataPort for CsvAdapter {
    fn fetch_ohlcv(
        &self,
        code: &str,
        exchange: &str,
        start_date: NaiveDate,
        end_date: NaiveDate,
    ) -> Result<Vec<OhlcvBar>, SamtraderError> {
        let path = self.csv_path(code, exchange);
        let content = fs::read_to_string(&path).map_err(|e| SamtraderError::Database {
            reason: format!("failed to read {}: {}", path.display(), e),
        })?;

        let mut rdr = csv::Reader::from_reader(content.as_bytes());
        let mut bars = Vec::new();

        for result in rdr.records() {
            let record = result.map_err(|e| SamtraderError::Database {
                reason: format!("CSV parse error: {}", e),
            })?;

            let date_str = record.get(0).ok_or_else(|| SamtraderError::Database {
                reason: "missing date column".into(),
            })?;
            let date = NaiveDate::parse_from_str(date_str, "%Y-%m-%d").map_err(|e| {
                SamtraderError::Database {
                    reason: format!("invalid date format: {}", e),
                }
            })?;

            if date < start_date || date > end_date {
                continue;
            }

            let open: f64 = record
                .get(1)
                .ok_or_else(|| SamtraderError::Database {
                    reason: "missing open column".into(),
                })?
                .parse()
                .map_err(|e| SamtraderError::Database {
                    reason: format!("invalid open value: {}", e),
                })?;

            let high: f64 = record
                .get(2)
                .ok_or_else(|| SamtraderError::Database {
                    reason: "missing high column".into(),
                })?
                .parse()
                .map_err(|e| SamtraderError::Database {
                    reason: format!("invalid high value: {}", e),
                })?;

            let low: f64 = record
                .get(3)
                .ok_or_else(|| SamtraderError::Database {
                    reason: "missing low column".into(),
                })?
                .parse()
                .map_err(|e| SamtraderError::Database {
                    reason: format!("invalid low value: {}", e),
                })?;

            let close: f64 = record
                .get(4)
                .ok_or_else(|| SamtraderError::Database {
                    reason: "missing close column".into(),
                })?
                .parse()
                .map_err(|e| SamtraderError::Database {
                    reason: format!("invalid close value: {}", e),
                })?;

            let volume: i64 = record
                .get(5)
                .ok_or_else(|| SamtraderError::Database {
                    reason: "missing volume column".into(),
                })?
                .parse()
                .map_err(|e| SamtraderError::Database {
                    reason: format!("invalid volume value: {}", e),
                })?;

            bars.push(OhlcvBar {
                code: code.to_string(),
                exchange: exchange.to_string(),
                date,
                open,
                high,
                low,
                close,
                volume,
            });
        }

        bars.sort_by_key(|b| b.date);
        Ok(bars)
    }

    fn list_symbols(&self, exchange: &str) -> Result<Vec<String>, SamtraderError> {
        let entries = fs::read_dir(&self.base_path).map_err(|e| SamtraderError::Database {
            reason: format!(
                "failed to read directory {}: {}",
                self.base_path.display(),
                e
            ),
        })?;

        let suffix = format!("_{}.csv", exchange);
        let mut symbols = Vec::new();

        for entry in entries {
            let entry = entry.map_err(|e| SamtraderError::Database {
                reason: format!("directory entry error: {}", e),
            })?;

            let name = entry.file_name();
            let name_str = name.to_string_lossy();

            if name_str.ends_with(&suffix) {
                let code = &name_str[..name_str.len() - suffix.len()];
                symbols.push(code.to_string());
            }
        }

        symbols.sort();
        Ok(symbols)
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use tempfile::TempDir;

    fn setup_test_data() -> (TempDir, PathBuf) {
        let dir = TempDir::new().unwrap();
        let path = dir.path().to_path_buf();

        let csv_content = "date,open,high,low,close,volume\n\
            2024-01-15,100.0,110.0,90.0,105.0,50000\n\
            2024-01-16,105.0,115.0,100.0,110.0,60000\n\
            2024-01-17,110.0,120.0,105.0,115.0,55000\n";

        fs::write(path.join("BHP_ASX.csv"), csv_content).unwrap();
        fs::write(
            path.join("CBA_ASX.csv"),
            "date,open,high,low,close,volume\n",
        )
        .unwrap();
        fs::write(
            path.join("AAPL_NYSE.csv"),
            "date,open,high,low,close,volume\n",
        )
        .unwrap();

        (dir, path)
    }

    #[test]
    fn fetch_ohlcv_returns_correct_data() {
        let (_dir, path) = setup_test_data();
        let adapter = CsvAdapter::new(path);

        let start = NaiveDate::from_ymd_opt(2024, 1, 15).unwrap();
        let end = NaiveDate::from_ymd_opt(2024, 1, 17).unwrap();
        let bars = adapter.fetch_ohlcv("BHP", "ASX", start, end).unwrap();

        assert_eq!(bars.len(), 3);
        assert_eq!(bars[0].date, NaiveDate::from_ymd_opt(2024, 1, 15).unwrap());
        assert_eq!(bars[0].open, 100.0);
        assert_eq!(bars[0].high, 110.0);
        assert_eq!(bars[0].low, 90.0);
        assert_eq!(bars[0].close, 105.0);
        assert_eq!(bars[0].volume, 50000);
    }

    #[test]
    fn fetch_ohlcv_filters_by_date() {
        let (_dir, path) = setup_test_data();
        let adapter = CsvAdapter::new(path);

        let start = NaiveDate::from_ymd_opt(2024, 1, 16).unwrap();
        let end = NaiveDate::from_ymd_opt(2024, 1, 16).unwrap();
        let bars = adapter.fetch_ohlcv("BHP", "ASX", start, end).unwrap();

        assert_eq!(bars.len(), 1);
        assert_eq!(bars[0].date, NaiveDate::from_ymd_opt(2024, 1, 16).unwrap());
    }

    #[test]
    fn fetch_ohlcv_returns_empty_for_missing_file() {
        let (_dir, path) = setup_test_data();
        let adapter = CsvAdapter::new(path);

        let start = NaiveDate::from_ymd_opt(2024, 1, 1).unwrap();
        let end = NaiveDate::from_ymd_opt(2024, 1, 31).unwrap();
        let result = adapter.fetch_ohlcv("XYZ", "ASX", start, end);

        assert!(result.is_err());
    }

    #[test]
    fn list_symbols_returns_exchange_symbols() {
        let (_dir, path) = setup_test_data();
        let adapter = CsvAdapter::new(path);

        let symbols = adapter.list_symbols("ASX").unwrap();
        assert_eq!(symbols, vec!["BHP", "CBA"]);

        let symbols = adapter.list_symbols("NYSE").unwrap();
        assert_eq!(symbols, vec!["AAPL"]);
    }
}
