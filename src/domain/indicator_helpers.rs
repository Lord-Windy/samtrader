//! Shared helper functions for indicator calculations (TRD Section 3.2).

use crate::domain::indicator::{IndicatorPoint, IndicatorSeries, IndicatorType, IndicatorValue};
use crate::domain::ohlcv::OhlcvBar;

pub fn calculate_vwap(bars: &[OhlcvBar]) -> IndicatorSeries {
    let mut values = Vec::with_capacity(bars.len());
    let mut cumulative_tp_vol: f64 = 0.0;
    let mut cumulative_vol: i64 = 0;

    for bar in bars {
        let typical_price = bar.typical_price();
        let volume = bar.volume;

        cumulative_tp_vol += typical_price * volume as f64;
        cumulative_vol += volume;

        let vwap = if cumulative_vol == 0 {
            0.0
        } else {
            cumulative_tp_vol / cumulative_vol as f64
        };

        values.push(IndicatorPoint {
            date: bar.date,
            valid: true,
            value: IndicatorValue::Simple(vwap),
        });
    }

    IndicatorSeries {
        indicator_type: IndicatorType::Vwap,
        values,
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use chrono::NaiveDate;

    fn make_bar(date: NaiveDate, high: f64, low: f64, close: f64, volume: i64) -> OhlcvBar {
        OhlcvBar {
            code: "TEST".into(),
            exchange: "ASX".into(),
            date,
            open: close,
            high,
            low,
            close,
            volume,
        }
    }

    #[test]
    fn vwap_single_bar() {
        let bars = vec![make_bar(
            NaiveDate::from_ymd_opt(2024, 1, 1).unwrap(),
            110.0,
            90.0,
            100.0,
            1000,
        )];
        let series = calculate_vwap(&bars);

        assert_eq!(series.indicator_type, IndicatorType::Vwap);
        assert_eq!(series.values.len(), 1);
        assert!(series.values[0].valid);

        let tp = (110.0 + 90.0 + 100.0) / 3.0;
        if let IndicatorValue::Simple(vwap) = series.values[0].value {
            assert!((vwap - tp).abs() < f64::EPSILON);
        } else {
            panic!("Expected Simple value");
        }
    }

    #[test]
    fn vwap_multiple_bars() {
        let bars = vec![
            make_bar(
                NaiveDate::from_ymd_opt(2024, 1, 1).unwrap(),
                110.0,
                90.0,
                100.0,
                1000,
            ),
            make_bar(
                NaiveDate::from_ymd_opt(2024, 1, 2).unwrap(),
                115.0,
                105.0,
                110.0,
                2000,
            ),
        ];
        let series = calculate_vwap(&bars);

        assert_eq!(series.values.len(), 2);

        let tp1 = (110.0 + 90.0 + 100.0) / 3.0;
        let tp2 = (115.0 + 105.0 + 110.0) / 3.0;

        let expected_vwap2 = (tp1 * 1000.0 + tp2 * 2000.0) / 3000.0;

        if let IndicatorValue::Simple(vwap) = series.values[1].value {
            assert!((vwap - expected_vwap2).abs() < 1e-10);
        } else {
            panic!("Expected Simple value");
        }
    }

    #[test]
    fn vwap_zero_volume() {
        let bars = vec![
            make_bar(
                NaiveDate::from_ymd_opt(2024, 1, 1).unwrap(),
                110.0,
                90.0,
                100.0,
                0,
            ),
            make_bar(
                NaiveDate::from_ymd_opt(2024, 1, 2).unwrap(),
                115.0,
                105.0,
                110.0,
                1000,
            ),
        ];
        let series = calculate_vwap(&bars);

        if let IndicatorValue::Simple(vwap) = series.values[0].value {
            assert!((vwap - 0.0).abs() < f64::EPSILON);
        }

        if let IndicatorValue::Simple(vwap) = series.values[1].value {
            let tp2 = (115.0 + 105.0 + 110.0) / 3.0;
            assert!((vwap - tp2).abs() < f64::EPSILON);
        }
    }

    #[test]
    fn vwap_all_bars_valid() {
        let bars = vec![
            make_bar(
                NaiveDate::from_ymd_opt(2024, 1, 1).unwrap(),
                110.0,
                90.0,
                100.0,
                1000,
            ),
            make_bar(
                NaiveDate::from_ymd_opt(2024, 1, 2).unwrap(),
                115.0,
                105.0,
                110.0,
                2000,
            ),
            make_bar(
                NaiveDate::from_ymd_opt(2024, 1, 3).unwrap(),
                120.0,
                100.0,
                115.0,
                1500,
            ),
        ];
        let series = calculate_vwap(&bars);

        for point in &series.values {
            assert!(point.valid);
        }
    }
}
