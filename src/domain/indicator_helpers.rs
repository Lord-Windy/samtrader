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

pub fn calc_atr(bars: &[OhlcvBar], period: usize) -> IndicatorSeries {
    if bars.len() < period || period == 0 {
        return IndicatorSeries {
            indicator_type: IndicatorType::Atr(period),
            values: vec![],
        };
    }

    let mut tr_values: Vec<f64> = Vec::with_capacity(bars.len());
    for (i, bar) in bars.iter().enumerate() {
        let tr = if i == 0 {
            bar.high - bar.low
        } else {
            bar.true_range(bars[i - 1].close)
        };
        tr_values.push(tr);
    }

    let mut results: Vec<IndicatorPoint> = Vec::with_capacity(bars.len());

    for i in 0..bars.len() {
        if i < period - 1 {
            results.push(IndicatorPoint {
                date: bars[i].date,
                valid: false,
                value: IndicatorValue::Simple(0.0),
            });
        } else if i == period - 1 {
            let seed: f64 = tr_values[0..=i].iter().sum::<f64>() / period as f64;
            results.push(IndicatorPoint {
                date: bars[i].date,
                valid: true,
                value: IndicatorValue::Simple(seed),
            });
        } else {
            let prev_atr = match &results[i - 1].value {
                IndicatorValue::Simple(v) => *v,
                _ => 0.0,
            };
            let atr = (prev_atr * (period - 1) as f64 + tr_values[i]) / period as f64;
            results.push(IndicatorPoint {
                date: bars[i].date,
                valid: true,
                value: IndicatorValue::Simple(atr),
            });
        }
    }

    IndicatorSeries {
        indicator_type: IndicatorType::Atr(period),
        values: results,
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use chrono::NaiveDate;

    fn make_bar_with_volume(date: NaiveDate, high: f64, low: f64, close: f64, volume: i64) -> OhlcvBar {
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

    fn make_bar(date: NaiveDate, high: f64, low: f64, close: f64) -> OhlcvBar {
        make_bar_with_volume(date, high, low, close, 1000)
    }

    #[test]
    fn vwap_single_bar() {
        let bars = vec![make_bar_with_volume(
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
            make_bar_with_volume(
                NaiveDate::from_ymd_opt(2024, 1, 1).unwrap(),
                110.0,
                90.0,
                100.0,
                1000,
            ),
            make_bar_with_volume(
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
            make_bar_with_volume(
                NaiveDate::from_ymd_opt(2024, 1, 1).unwrap(),
                110.0,
                90.0,
                100.0,
                0,
            ),
            make_bar_with_volume(
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
            make_bar_with_volume(
                NaiveDate::from_ymd_opt(2024, 1, 1).unwrap(),
                110.0,
                90.0,
                100.0,
                1000,
            ),
            make_bar_with_volume(
                NaiveDate::from_ymd_opt(2024, 1, 2).unwrap(),
                115.0,
                105.0,
                110.0,
                2000,
            ),
            make_bar_with_volume(
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

    #[test]
    fn atr_basic() {
        let bars: Vec<OhlcvBar> = (0..5)
            .map(|i| {
                make_bar(
                    NaiveDate::from_ymd_opt(2024, 1, i + 1).unwrap(),
                    110.0,
                    90.0,
                    100.0,
                )
            })
            .collect();

        let series = calc_atr(&bars, 3);
        assert_eq!(series.values.len(), 5);

        assert!(!series.values[0].valid);
        assert!(!series.values[1].valid);
        assert!(series.values[2].valid);
        assert!(series.values[3].valid);
        assert!(series.values[4].valid);
    }

    #[test]
    fn atr_seed_is_average() {
        let bars: Vec<OhlcvBar> = vec![
            make_bar(
                NaiveDate::from_ymd_opt(2024, 1, 1).unwrap(),
                110.0,
                100.0,
                105.0,
            ),
            make_bar(
                NaiveDate::from_ymd_opt(2024, 1, 2).unwrap(),
                115.0,
                105.0,
                110.0,
            ),
            make_bar(
                NaiveDate::from_ymd_opt(2024, 1, 3).unwrap(),
                120.0,
                110.0,
                115.0,
            ),
        ];

        let series = calc_atr(&bars, 3);
        let seed = match &series.values[2].value {
            IndicatorValue::Simple(v) => *v,
            _ => 0.0,
        };

        let expected = (10.0 + 10.0 + 10.0) / 3.0;
        assert!((seed - expected).abs() < 1e-9);
    }

    #[test]
    fn atr_wilder_smoothing() {
        let bars: Vec<OhlcvBar> = vec![
            make_bar(
                NaiveDate::from_ymd_opt(2024, 1, 1).unwrap(),
                110.0,
                100.0,
                105.0,
            ),
            make_bar(
                NaiveDate::from_ymd_opt(2024, 1, 2).unwrap(),
                115.0,
                105.0,
                110.0,
            ),
            make_bar(
                NaiveDate::from_ymd_opt(2024, 1, 3).unwrap(),
                120.0,
                110.0,
                115.0,
            ),
            make_bar(
                NaiveDate::from_ymd_opt(2024, 1, 4).unwrap(),
                125.0,
                115.0,
                120.0,
            ),
        ];

        let series = calc_atr(&bars, 3);
        let atr3 = match &series.values[3].value {
            IndicatorValue::Simple(v) => *v,
            _ => 0.0,
        };

        let seed = 10.0;
        let expected = (seed * 2.0 + 10.0) / 3.0;
        assert!((atr3 - expected).abs() < 1e-9);
    }

    #[test]
    fn atr_insufficient_bars() {
        let bars: Vec<OhlcvBar> = (0..2)
            .map(|i| {
                make_bar(
                    NaiveDate::from_ymd_opt(2024, 1, i + 1).unwrap(),
                    110.0,
                    90.0,
                    100.0,
                )
            })
            .collect();

        let series = calc_atr(&bars, 5);
        assert!(series.values.is_empty());
    }

    #[test]
    fn atr_handles_gaps() {
        let bars: Vec<OhlcvBar> = vec![
            make_bar(
                NaiveDate::from_ymd_opt(2024, 1, 1).unwrap(),
                110.0,
                100.0,
                105.0,
            ),
            make_bar(
                NaiveDate::from_ymd_opt(2024, 1, 2).unwrap(),
                130.0,
                120.0,
                125.0,
            ),
            make_bar(
                NaiveDate::from_ymd_opt(2024, 1, 3).unwrap(),
                120.0,
                110.0,
                115.0,
            ),
        ];

        let series = calc_atr(&bars, 2);
        assert!(!series.values[0].valid);
        assert!(series.values[1].valid);
        assert!(series.values[2].valid);
    }
}
