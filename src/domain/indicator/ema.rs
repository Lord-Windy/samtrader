//! Exponential Moving Average indicator (TRD §4.4.2).
//!
//! k = 2/(n+1), seed with first SMA, then EMA[i] = C[i]*k + EMA[i-1]*(1-k).
//! Warmup: first (n-1) bars are invalid.

use crate::domain::indicator::{IndicatorPoint, IndicatorSeries, IndicatorType, IndicatorValue};
use crate::domain::ohlcv::OhlcvBar;

pub fn calculate_ema(bars: &[OhlcvBar], period: usize) -> IndicatorSeries {
    if period == 0 || bars.is_empty() {
        return IndicatorSeries {
            indicator_type: IndicatorType::Ema(period),
            values: Vec::new(),
        };
    }

    let mut values = Vec::with_capacity(bars.len());
    let k = 2.0 / (period as f64 + 1.0);
    let mut ema = 0.0;
    let mut sum = 0.0;

    for (i, bar) in bars.iter().enumerate() {
        if i < period - 1 {
            sum += bar.close;
            values.push(IndicatorPoint {
                date: bar.date,
                valid: false,
                value: IndicatorValue::Simple(0.0),
            });
        } else if i == period - 1 {
            sum += bar.close;
            ema = sum / period as f64;
            values.push(IndicatorPoint {
                date: bar.date,
                valid: true,
                value: IndicatorValue::Simple(ema),
            });
        } else {
            ema = bar.close * k + ema * (1.0 - k);
            values.push(IndicatorPoint {
                date: bar.date,
                valid: true,
                value: IndicatorValue::Simple(ema),
            });
        }
    }

    IndicatorSeries {
        indicator_type: IndicatorType::Ema(period),
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
    fn ema_warmup() {
        let bars = make_bars(&[10.0, 20.0, 30.0, 40.0, 50.0]);
        let series = calculate_ema(&bars, 3);

        assert!(!series.values[0].valid);
        assert!(!series.values[1].valid);
        assert!(series.values[2].valid);
        assert!(series.values[3].valid);
        assert!(series.values[4].valid);
    }

    #[test]
    fn ema_period_1() {
        let bars = make_bars(&[10.0, 20.0, 30.0]);
        let series = calculate_ema(&bars, 1);

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
    fn ema_seed_is_sma() {
        let bars = make_bars(&[10.0, 20.0, 30.0]);
        let series = calculate_ema(&bars, 3);

        assert!(!series.values[0].valid);
        assert!(!series.values[1].valid);

        if let IndicatorValue::Simple(v) = series.values[2].value {
            let expected_sma = (10.0 + 20.0 + 30.0) / 3.0;
            assert!((v - expected_sma).abs() < f64::EPSILON);
        } else {
            panic!("Expected Simple value");
        }
    }

    #[test]
    fn ema_recursive_calculation() {
        let bars = make_bars(&[10.0, 20.0, 30.0, 40.0, 50.0]);
        let series = calculate_ema(&bars, 3);

        let k = 2.0 / 4.0;
        let sma = (10.0 + 20.0 + 30.0) / 3.0;

        if let IndicatorValue::Simple(v) = series.values[2].value {
            assert!((v - sma).abs() < f64::EPSILON);
        }

        let ema_3 = 40.0 * k + sma * (1.0 - k);
        if let IndicatorValue::Simple(v) = series.values[3].value {
            assert!((v - ema_3).abs() < f64::EPSILON);
        }

        let ema_4 = 50.0 * k + ema_3 * (1.0 - k);
        if let IndicatorValue::Simple(v) = series.values[4].value {
            assert!((v - ema_4).abs() < f64::EPSILON);
        }
    }

    #[test]
    fn ema_equal_prices() {
        let bars = make_bars(&[100.0, 100.0, 100.0, 100.0, 100.0]);
        let series = calculate_ema(&bars, 3);

        for i in 2..5 {
            if let IndicatorValue::Simple(v) = series.values[i].value {
                assert!((v - 100.0).abs() < f64::EPSILON);
            }
        }
    }

    #[test]
    fn ema_indicator_type() {
        let bars = make_bars(&[10.0, 20.0, 30.0]);
        let series = calculate_ema(&bars, 5);

        assert_eq!(series.indicator_type, IndicatorType::Ema(5));
    }

    #[test]
    fn ema_empty_bars() {
        let bars: Vec<OhlcvBar> = vec![];
        let series = calculate_ema(&bars, 3);
        assert!(series.values.is_empty());
    }

    #[test]
    fn ema_period_0() {
        let bars = make_bars(&[10.0, 20.0]);
        let series = calculate_ema(&bars, 0);
        assert!(series.values.is_empty());
    }

    #[test]
    fn ema_smoothing_factor() {
        let period = 10;
        let k = 2.0 / (period as f64 + 1.0);
        assert!((k - 2.0 / 11.0).abs() < f64::EPSILON);
    }
}
