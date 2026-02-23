//! Bollinger Bands indicator (TRD §4.4.8).
//!
//! Bollinger Bands consist of:
//! - Middle: Simple Moving Average (SMA) over n periods
//! - Upper: Middle + (multiplier × StdDev)
//! - Lower: Middle - (multiplier × StdDev)
//!
//! Where StdDev is population standard deviation (divides by N, not N-1).
//!
//! Default parameters: period=20, multiplier=2.0
//! Warmup: first (period-1) bars are invalid.

use crate::domain::indicator::{IndicatorPoint, IndicatorSeries, IndicatorType, IndicatorValue};
use crate::domain::ohlcv::OhlcvBar;

pub fn calculate_bollinger(
    bars: &[OhlcvBar],
    period: usize,
    stddev_mult_x100: u32,
) -> IndicatorSeries {
    let mut values = Vec::with_capacity(bars.len());
    let warmup = period.saturating_sub(1);
    let mult = stddev_mult_x100 as f64 / 100.0;

    for i in 0..bars.len() {
        let date = bars[i].date;
        let valid = i >= warmup;

        let (upper, middle, lower) = if valid {
            let start = i + 1 - period;
            let window = &bars[start..=i];

            let middle_val: f64 = window.iter().map(|b| b.close).sum::<f64>() / period as f64;

            let variance: f64 = window
                .iter()
                .map(|b| {
                    let diff = b.close - middle_val;
                    diff * diff
                })
                .sum::<f64>()
                / period as f64;

            let stddev = variance.sqrt();
            let upper = middle_val + mult * stddev;
            let lower = middle_val - mult * stddev;

            (upper, middle_val, lower)
        } else {
            (0.0, 0.0, 0.0)
        };

        values.push(IndicatorPoint {
            date,
            valid,
            value: IndicatorValue::Bollinger {
                upper,
                middle,
                lower,
            },
        });
    }

    IndicatorSeries {
        indicator_type: IndicatorType::Bollinger {
            period,
            stddev_mult_x100,
        },
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
    fn bollinger_warmup() {
        let bars = make_bars(&[10.0, 20.0, 30.0, 40.0, 50.0]);
        let series = calculate_bollinger(&bars, 3, 200);

        assert!(!series.values[0].valid);
        assert!(!series.values[1].valid);
        assert!(series.values[2].valid);
        assert!(series.values[3].valid);
        assert!(series.values[4].valid);
    }

    #[test]
    fn bollinger_constant_values() {
        let bars = make_bars(&[100.0, 100.0, 100.0, 100.0, 100.0]);
        let series = calculate_bollinger(&bars, 3, 200);

        assert!(series.values[2].valid);
        if let IndicatorValue::Bollinger {
            upper,
            middle,
            lower,
        } = series.values[2].value
        {
            assert!((middle - 100.0).abs() < f64::EPSILON);
            assert!((upper - 100.0).abs() < f64::EPSILON);
            assert!((lower - 100.0).abs() < f64::EPSILON);
        } else {
            panic!("Expected Bollinger value");
        }
    }

    #[test]
    fn bollinger_basic_calculation() {
        let bars = make_bars(&[10.0, 20.0, 30.0]);
        let series = calculate_bollinger(&bars, 3, 200);

        assert!(series.values[2].valid);
        if let IndicatorValue::Bollinger {
            upper,
            middle,
            lower,
        } = series.values[2].value
        {
            let expected_middle: f64 = (10.0 + 20.0 + 30.0) / 3.0;
            let variance: f64 = ((10.0 - expected_middle).powi(2)
                + (20.0 - expected_middle).powi(2)
                + (30.0 - expected_middle).powi(2))
                / 3.0;
            let stddev = variance.sqrt();
            let expected_upper = expected_middle + 2.0 * stddev;
            let expected_lower = expected_middle - 2.0 * stddev;

            assert!((middle - expected_middle).abs() < 1e-10);
            assert!((upper - expected_upper).abs() < 1e-10);
            assert!((lower - expected_lower).abs() < 1e-10);
        } else {
            panic!("Expected Bollinger value");
        }
    }

    #[test]
    fn bollinger_multiplier_variations() {
        let bars = make_bars(&[10.0, 20.0, 30.0]);
        let series = calculate_bollinger(&bars, 3, 100);

        if let IndicatorValue::Bollinger {
            upper,
            middle,
            lower,
        } = series.values[2].value
        {
            let expected_middle: f64 = 20.0;
            let variance: f64 = ((10.0_f64 - 20.0_f64).powi(2)
                + (20.0_f64 - 20.0_f64).powi(2)
                + (30.0_f64 - 20.0_f64).powi(2))
                / 3.0;
            let stddev = variance.sqrt();
            let expected_upper = expected_middle + 1.0 * stddev;
            let expected_lower = expected_middle - 1.0 * stddev;

            assert!((middle - expected_middle).abs() < 1e-10);
            assert!((upper - expected_upper).abs() < 1e-10);
            assert!((lower - expected_lower).abs() < 1e-10);
        } else {
            panic!("Expected Bollinger value");
        }
    }

    #[test]
    fn bollinger_indicator_type() {
        let bars = make_bars(&[10.0, 20.0, 30.0]);
        let series = calculate_bollinger(&bars, 20, 200);

        assert_eq!(
            series.indicator_type,
            IndicatorType::Bollinger {
                period: 20,
                stddev_mult_x100: 200
            }
        );
    }

    #[test]
    fn bollinger_symmetry() {
        let bars = make_bars(&[10.0, 20.0, 30.0]);
        let series = calculate_bollinger(&bars, 3, 200);

        if let IndicatorValue::Bollinger {
            upper,
            middle,
            lower,
        } = series.values[2].value
        {
            let upper_dist = upper - middle;
            let lower_dist = middle - lower;
            assert!((upper_dist - lower_dist).abs() < 1e-10);
        } else {
            panic!("Expected Bollinger value");
        }
    }
}
