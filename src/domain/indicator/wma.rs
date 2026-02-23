//! Weighted Moving Average indicator (TRD §4.4.3).
//!
//! O(n) sliding window implementation using Diophantine technique.
//! WMA(n) = (1*P[i-n+1] + 2*P[i-n+2] + ... + n*P[i]) / (n*(n+1)/2)
//! Warmup: first (n-1) bars are invalid.

use crate::domain::indicator::{IndicatorPoint, IndicatorSeries, IndicatorType, IndicatorValue};
use crate::domain::ohlcv::OhlcvBar;

pub fn calculate_wma(bars: &[OhlcvBar], period: usize) -> IndicatorSeries {
    if period == 0 || bars.is_empty() {
        return IndicatorSeries {
            indicator_type: IndicatorType::Wma(period),
            values: Vec::new(),
        };
    }

    let mut values = Vec::with_capacity(bars.len());
    let divisor = (period * (period + 1)) as f64 / 2.0;
    let mut weighted_sum: f64 = 0.0;
    let mut window_sum: f64 = 0.0;

    for (i, bar) in bars.iter().enumerate() {
        if i < period {
            let weight = (i + 1) as f64;
            weighted_sum += weight * bar.close;
            window_sum += bar.close;
        } else {
            weighted_sum += period as f64 * bar.close - window_sum;
            window_sum += bar.close - bars[i - period].close;
        }

        let valid = i >= period - 1;
        let wma = if valid { weighted_sum / divisor } else { 0.0 };

        values.push(IndicatorPoint {
            date: bar.date,
            valid,
            value: IndicatorValue::Simple(wma),
        });
    }

    IndicatorSeries {
        indicator_type: IndicatorType::Wma(period),
        values,
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use chrono::NaiveDate;

    fn make_bars(prices: &[f64]) -> Vec<OhlcvBar> {
        prices
            .iter()
            .enumerate()
            .map(|(i, &close)| OhlcvBar {
                code: "TEST".into(),
                exchange: "TEST".into(),
                date: NaiveDate::from_ymd_opt(2024, 1, (i + 1) as u32).unwrap(),
                open: close,
                high: close,
                low: close,
                close,
                volume: 1000,
            })
            .collect()
    }

    #[test]
    fn wma_warmup() {
        let bars = make_bars(&[10.0, 20.0, 30.0, 40.0, 50.0]);
        let series = calculate_wma(&bars, 3);

        assert!(!series.values[0].valid);
        assert!(!series.values[1].valid);
        assert!(series.values[2].valid);
        assert!(series.values[3].valid);
        assert!(series.values[4].valid);
    }

    #[test]
    fn wma_period_1() {
        let bars = make_bars(&[10.0, 20.0, 30.0]);
        let series = calculate_wma(&bars, 1);

        assert!(series.values[0].valid);
        assert!(series.values[1].valid);
        assert!(series.values[2].valid);

        if let IndicatorValue::Simple(v) = series.values[0].value {
            assert!((v - 10.0).abs() < f64::EPSILON);
        }
        if let IndicatorValue::Simple(v) = series.values[1].value {
            assert!((v - 20.0).abs() < f64::EPSILON);
        }
    }

    #[test]
    fn wma_basic_calculation() {
        let bars = make_bars(&[10.0, 20.0, 30.0]);
        let series = calculate_wma(&bars, 3);

        assert!(!series.values[0].valid);
        assert!(!series.values[1].valid);

        if let IndicatorValue::Simple(v) = series.values[2].value {
            let divisor = (3.0 * 4.0) / 2.0;
            let expected = (1.0 * 10.0 + 2.0 * 20.0 + 3.0 * 30.0) / divisor;
            assert!((v - expected).abs() < f64::EPSILON);
        } else {
            panic!("Expected Simple value");
        }
    }

    #[test]
    fn wma_sliding_window() {
        let bars = make_bars(&[10.0, 20.0, 30.0, 40.0]);
        let series = calculate_wma(&bars, 3);

        if let IndicatorValue::Simple(v) = series.values[3].value {
            let divisor = (3.0 * 4.0) / 2.0;
            let expected = (1.0 * 20.0 + 2.0 * 30.0 + 3.0 * 40.0) / divisor;
            assert!((v - expected).abs() < f64::EPSILON);
        } else {
            panic!("Expected Simple value");
        }
    }

    #[test]
    fn wma_equal_prices() {
        let bars = make_bars(&[100.0, 100.0, 100.0]);
        let series = calculate_wma(&bars, 3);

        if let IndicatorValue::Simple(v) = series.values[2].value {
            assert!((v - 100.0).abs() < f64::EPSILON);
        }
    }

    #[test]
    fn wma_indicator_type() {
        let bars = make_bars(&[10.0, 20.0, 30.0]);
        let series = calculate_wma(&bars, 5);

        assert_eq!(series.indicator_type, IndicatorType::Wma(5));
    }

    #[test]
    fn wma_known_values() {
        let bars = make_bars(&[10.0, 20.0, 30.0, 40.0, 50.0]);
        let series = calculate_wma(&bars, 3);

        assert!(!series.values[0].valid);
        assert!(!series.values[1].valid);

        let divisor = (3.0 * 4.0) / 2.0;

        if let IndicatorValue::Simple(v) = series.values[2].value {
            let expected = (1.0 * 10.0 + 2.0 * 20.0 + 3.0 * 30.0) / divisor;
            assert!((v - expected).abs() < f64::EPSILON);
        }

        if let IndicatorValue::Simple(v) = series.values[3].value {
            let expected = (1.0 * 20.0 + 2.0 * 30.0 + 3.0 * 40.0) / divisor;
            assert!((v - expected).abs() < f64::EPSILON);
        }

        if let IndicatorValue::Simple(v) = series.values[4].value {
            let expected = (1.0 * 30.0 + 2.0 * 40.0 + 3.0 * 50.0) / divisor;
            assert!((v - expected).abs() < f64::EPSILON);
        }
    }

    #[test]
    fn wma_empty_bars() {
        let bars: Vec<OhlcvBar> = vec![];
        let series = calculate_wma(&bars, 3);
        assert!(series.values.is_empty());
    }

    #[test]
    fn wma_period_0() {
        let bars = make_bars(&[10.0, 20.0]);
        let series = calculate_wma(&bars, 0);
        assert!(series.values.is_empty());
    }
}
