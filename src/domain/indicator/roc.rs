//! ROC (Rate of Change) indicator implementation (TRD §4.4.7).
//!
//! ROC(n)[i] = ((C[i] - C[i-n]) / C[i-n]) * 100
//! If C[i-n] == 0: ROC = 0
//! Warmup: first n bars invalid.

use crate::domain::indicator::{IndicatorPoint, IndicatorSeries, IndicatorType, IndicatorValue};
use crate::domain::ohlcv::OhlcvBar;

pub fn calculate_roc(bars: &[OhlcvBar], period: usize) -> IndicatorSeries {
    let mut values = Vec::with_capacity(bars.len());

    for i in 0..bars.len() {
        let date = bars[i].date;
        let valid = i >= period;

        let value = if valid {
            let prev_close = bars[i - period].close;
            let curr_close = bars[i].close;

            if prev_close == 0.0 {
                0.0
            } else {
                ((curr_close - prev_close) / prev_close) * 100.0
            }
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
        indicator_type: IndicatorType::Roc(period),
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
    fn roc_warmup() {
        let bars = make_bars(&[100.0, 105.0, 110.0, 115.0, 120.0]);
        let series = calculate_roc(&bars, 3);

        assert!(!series.values[0].valid);
        assert!(!series.values[1].valid);
        assert!(!series.values[2].valid);
        assert!(series.values[3].valid);
        assert!(series.values[4].valid);
    }

    #[test]
    fn roc_basic_calculation() {
        let bars = make_bars(&[100.0, 105.0, 110.0, 115.0]);
        let series = calculate_roc(&bars, 2);

        assert!(!series.values[0].valid);
        assert!(!series.values[1].valid);

        if let IndicatorValue::Simple(v) = series.values[2].value {
            let expected = ((110.0 - 100.0) / 100.0) * 100.0;
            assert!((v - expected).abs() < f64::EPSILON);
        } else {
            panic!("Expected Simple value");
        }

        if let IndicatorValue::Simple(v) = series.values[3].value {
            let expected = ((115.0 - 105.0) / 105.0) * 100.0;
            assert!((v - expected).abs() < f64::EPSILON);
        } else {
            panic!("Expected Simple value");
        }
    }

    #[test]
    fn roc_zero_division() {
        let bars = make_bars(&[0.0, 100.0, 110.0]);
        let series = calculate_roc(&bars, 2);

        assert!(series.values[2].valid);
        if let IndicatorValue::Simple(v) = series.values[2].value {
            assert!((v - 0.0).abs() < f64::EPSILON);
        } else {
            panic!("Expected Simple value");
        }
    }

    #[test]
    fn roc_negative_change() {
        let bars = make_bars(&[100.0, 90.0, 80.0]);
        let series = calculate_roc(&bars, 2);

        assert!(series.values[2].valid);
        if let IndicatorValue::Simple(v) = series.values[2].value {
            let expected = ((80.0 - 100.0) / 100.0) * 100.0;
            assert!((v - expected).abs() < f64::EPSILON);
            assert!(v < 0.0);
        } else {
            panic!("Expected Simple value");
        }
    }

    #[test]
    fn roc_indicator_type() {
        let bars = make_bars(&[100.0, 105.0]);
        let series = calculate_roc(&bars, 10);

        assert_eq!(series.indicator_type, IndicatorType::Roc(10));
    }
}
