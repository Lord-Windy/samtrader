//! Standard Deviation indicator (TRD §4.4.10).
//!
//! Population standard deviation over n closing prices.
//! STDDEV(n)[i] = sqrt(sum((C[i-j] - SMA(n)[i])^2 for j in 0..n-1) / n)
//! Warmup: first (n-1) bars are invalid.

use crate::domain::indicator::{IndicatorPoint, IndicatorSeries, IndicatorType, IndicatorValue};
use crate::domain::ohlcv::OhlcvBar;

pub fn calculate_stddev(bars: &[OhlcvBar], period: usize) -> IndicatorSeries {
    let mut values = Vec::with_capacity(bars.len());
    let warmup = period.saturating_sub(1);

    for i in 0..bars.len() {
        let date = bars[i].date;
        let valid = i >= warmup;

        let value = if valid {
            let start = i + 1 - period;
            let window = &bars[start..=i];

            let sma: f64 = window.iter().map(|b| b.close).sum::<f64>() / period as f64;

            let variance: f64 = window
                .iter()
                .map(|b| {
                    let diff = b.close - sma;
                    diff * diff
                })
                .sum::<f64>()
                / period as f64;

            variance.sqrt()
        } else {
            0.0
        };

        values.push(IndicatorPoint {
            date,
            valid,
            value: IndicatorValue::Simple(value),
        });
    }

    IndicatorSeries {
        indicator_type: IndicatorType::Stddev(period),
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
    fn stddev_warmup() {
        let bars = make_bars(&[10.0, 20.0, 30.0, 40.0, 50.0]);
        let series = calculate_stddev(&bars, 3);

        assert!(!series.values[0].valid);
        assert!(!series.values[1].valid);
        assert!(series.values[2].valid);
        assert!(series.values[3].valid);
        assert!(series.values[4].valid);
    }

    #[test]
    fn stddev_constant_values() {
        let bars = make_bars(&[100.0, 100.0, 100.0, 100.0, 100.0]);
        let series = calculate_stddev(&bars, 3);

        assert!(!series.values[0].valid);
        assert!(!series.values[1].valid);

        if let IndicatorValue::Simple(v) = series.values[2].value {
            assert!((v - 0.0).abs() < f64::EPSILON);
        } else {
            panic!("Expected Simple value");
        }
    }

    #[test]
    fn stddev_basic_calculation() {
        let bars = make_bars(&[10.0, 20.0, 30.0]);
        let series = calculate_stddev(&bars, 3);

        assert!(!series.values[0].valid);
        assert!(!series.values[1].valid);

        if let IndicatorValue::Simple(v) = series.values[2].value {
            let sma: f64 = (10.0 + 20.0 + 30.0) / 3.0;
            let expected: f64 =
                ((10.0 - sma).powi(2) + (20.0 - sma).powi(2) + (30.0 - sma).powi(2)) / 3.0;
            let expected = expected.sqrt();
            assert!((v - expected).abs() < 1e-10);
        } else {
            panic!("Expected Simple value");
        }
    }

    #[test]
    fn stddev_known_values() {
        let bars = make_bars(&[2.0, 4.0, 4.0, 4.0, 5.0, 5.0, 7.0, 9.0]);
        let series = calculate_stddev(&bars, 8);

        assert!(series.values[7].valid);
        if let IndicatorValue::Simple(v) = series.values[7].value {
            let expected: f64 = 2.0;
            assert!((v - expected).abs() < 1e-10);
        } else {
            panic!("Expected Simple value");
        }
    }

    #[test]
    fn stddev_indicator_type() {
        let bars = make_bars(&[10.0, 20.0, 30.0]);
        let series = calculate_stddev(&bars, 5);

        assert_eq!(series.indicator_type, IndicatorType::Stddev(5));
    }
}
