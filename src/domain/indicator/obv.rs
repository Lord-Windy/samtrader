//! OBV (On-Balance Volume) indicator implementation (TRD Section 4.4.11).

use crate::domain::indicator::{IndicatorPoint, IndicatorSeries, IndicatorType, IndicatorValue};
use crate::domain::ohlcv::OhlcvBar;

/// Calculate OBV (On-Balance Volume) indicator.
///
/// OBV[0] = volume[0]
/// If close[i] > close[i-1]: OBV[i] = OBV[i-1] + volume[i]
/// If close[i] < close[i-1]: OBV[i] = OBV[i-1] - volume[i]
/// If close[i] == close[i-1]: OBV[i] = OBV[i-1]
///
/// No warmup period; all bars are valid.
pub fn calculate_obv(bars: &[OhlcvBar]) -> IndicatorSeries {
    let mut values = Vec::with_capacity(bars.len());
    let mut obv: f64 = 0.0;
    let mut prev_close: f64 = 0.0;

    for (i, bar) in bars.iter().enumerate() {
        if i == 0 {
            obv = bar.volume as f64;
        } else if bar.close > prev_close {
            obv += bar.volume as f64;
        } else if bar.close < prev_close {
            obv -= bar.volume as f64;
        }
        prev_close = bar.close;

        values.push(IndicatorPoint {
            date: bar.date,
            valid: true,
            value: IndicatorValue::Simple(obv),
        });
    }

    IndicatorSeries {
        indicator_type: IndicatorType::Obv,
        values,
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use chrono::NaiveDate;

    fn make_bar(date: &str, close: f64, volume: i64) -> OhlcvBar {
        OhlcvBar {
            code: "TEST".into(),
            exchange: "ASX".into(),
            date: NaiveDate::parse_from_str(date, "%Y-%m-%d").unwrap(),
            open: close,
            high: close,
            low: close,
            close,
            volume,
        }
    }

    #[test]
    fn obv_first_bar_is_volume() {
        let bars = vec![make_bar("2024-01-01", 100.0, 1000)];
        let series = calculate_obv(&bars);
        assert_eq!(series.values.len(), 1);
        assert!(series.values[0].valid);
        if let IndicatorValue::Simple(v) = series.values[0].value {
            assert!((v - 1000.0).abs() < f64::EPSILON);
        } else {
            panic!("Expected Simple value");
        }
    }

    #[test]
    fn obv_adds_volume_on_up_day() {
        let bars = vec![
            make_bar("2024-01-01", 100.0, 1000),
            make_bar("2024-01-02", 105.0, 500),
        ];
        let series = calculate_obv(&bars);
        if let IndicatorValue::Simple(v) = series.values[1].value {
            assert!((v - 1500.0).abs() < f64::EPSILON);
        } else {
            panic!("Expected Simple value");
        }
    }

    #[test]
    fn obv_subtracts_volume_on_down_day() {
        let bars = vec![
            make_bar("2024-01-01", 100.0, 1000),
            make_bar("2024-01-02", 95.0, 300),
        ];
        let series = calculate_obv(&bars);
        if let IndicatorValue::Simple(v) = series.values[1].value {
            assert!((v - 700.0).abs() < f64::EPSILON);
        } else {
            panic!("Expected Simple value");
        }
    }

    #[test]
    fn obv_unchanged_on_flat_day() {
        let bars = vec![
            make_bar("2024-01-01", 100.0, 1000),
            make_bar("2024-01-02", 100.0, 500),
        ];
        let series = calculate_obv(&bars);
        if let IndicatorValue::Simple(v) = series.values[1].value {
            assert!((v - 1000.0).abs() < f64::EPSILON);
        } else {
            panic!("Expected Simple value");
        }
    }

    #[test]
    fn obv_all_bars_valid() {
        let bars = vec![
            make_bar("2024-01-01", 100.0, 1000),
            make_bar("2024-01-02", 105.0, 500),
            make_bar("2024-01-03", 102.0, 200),
        ];
        let series = calculate_obv(&bars);
        for point in &series.values {
            assert!(point.valid);
        }
    }

    #[test]
    fn obv_indicator_type() {
        let bars = vec![make_bar("2024-01-01", 100.0, 1000)];
        let series = calculate_obv(&bars);
        assert_eq!(series.indicator_type, IndicatorType::Obv);
    }
}
