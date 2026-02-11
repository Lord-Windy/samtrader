//! OHLCV bar representation (TRD Section 3.1).

use chrono::NaiveDate;

#[derive(Debug, Clone)]
pub struct OhlcvBar {
    pub code: String,
    pub exchange: String,
    pub date: NaiveDate,
    pub open: f64,
    pub high: f64,
    pub low: f64,
    pub close: f64,
    pub volume: i64,
}

impl OhlcvBar {
    /// (high + low + close) / 3
    pub fn typical_price(&self) -> f64 {
        (self.high + self.low + self.close) / 3.0
    }

    /// max(high - low, |high - prev_close|, |low - prev_close|)
    pub fn true_range(&self, prev_close: f64) -> f64 {
        let hl = self.high - self.low;
        let hc = (self.high - prev_close).abs();
        let lc = (self.low - prev_close).abs();
        hl.max(hc).max(lc)
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    fn sample_bar() -> OhlcvBar {
        OhlcvBar {
            code: "BHP".into(),
            exchange: "ASX".into(),
            date: NaiveDate::from_ymd_opt(2024, 1, 15).unwrap(),
            open: 100.0,
            high: 110.0,
            low: 90.0,
            close: 105.0,
            volume: 50_000,
        }
    }

    #[test]
    fn typical_price() {
        let bar = sample_bar();
        // (110 + 90 + 105) / 3 = 101.666...
        let expected = (110.0 + 90.0 + 105.0) / 3.0;
        assert!((bar.typical_price() - expected).abs() < f64::EPSILON);
    }

    #[test]
    fn true_range_hl_dominates() {
        let bar = sample_bar();
        // high-low=20, |high-100|=10, |low-100|=10 → 20
        assert!((bar.true_range(100.0) - 20.0).abs() < f64::EPSILON);
    }

    #[test]
    fn true_range_gap_up() {
        let bar = sample_bar();
        // high-low=20, |110-70|=40, |90-70|=20 → 40
        assert!((bar.true_range(70.0) - 40.0).abs() < f64::EPSILON);
    }

    #[test]
    fn true_range_gap_down() {
        let bar = sample_bar();
        // high-low=20, |110-130|=20, |90-130|=40 → 40
        assert!((bar.true_range(130.0) - 40.0).abs() < f64::EPSILON);
    }
}
