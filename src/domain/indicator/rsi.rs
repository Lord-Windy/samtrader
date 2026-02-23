//! RSI (Relative Strength Index) indicator implementation (TRD §4.4.4).
//!
//! Uses Wilder's smoothing for average gain/loss calculation:
//! - First average: simple mean of gains/losses over first n bars
//! - Subsequent: avg = (prev_avg * (n-1) + current) / n
//!
//! Formula: RSI = 100 - (100 / (1 + avg_gain / avg_loss))
//! If avg_loss == 0: RSI = 100
//!
//! Warmup: first n bars are invalid (need n price changes to compute initial average).

use crate::domain::indicator::{IndicatorPoint, IndicatorSeries, IndicatorType, IndicatorValue};
use crate::domain::ohlcv::OhlcvBar;

pub fn calculate_rsi(bars: &[OhlcvBar], period: usize) -> IndicatorSeries {
    if period == 0 || bars.len() < 2 {
        let values: Vec<IndicatorPoint> = bars
            .iter()
            .map(|b| IndicatorPoint {
                date: b.date,
                valid: false,
                value: IndicatorValue::Simple(0.0),
            })
            .collect();

        return IndicatorSeries {
            indicator_type: IndicatorType::Rsi(period),
            values,
        };
    }

    let mut values = Vec::with_capacity(bars.len());
    values.push(IndicatorPoint {
        date: bars[0].date,
        valid: false,
        value: IndicatorValue::Simple(0.0),
    });

    let mut gains: Vec<f64> = Vec::new();
    let mut losses: Vec<f64> = Vec::new();

    for i in 1..bars.len() {
        let change = bars[i].close - bars[i - 1].close;
        gains.push(if change > 0.0 { change } else { 0.0 });
        losses.push(if change < 0.0 { -change } else { 0.0 });
    }

    let mut avg_gain = 0.0;
    let mut avg_loss = 0.0;

    for (i, bar) in bars.iter().enumerate().skip(1) {
        let gain_idx = i - 1;

        if gain_idx < period - 1 {
            avg_gain = gains[..=gain_idx].iter().sum::<f64>() / (gain_idx + 1) as f64;
            avg_loss = losses[..=gain_idx].iter().sum::<f64>() / (gain_idx + 1) as f64;
            values.push(IndicatorPoint {
                date: bar.date,
                valid: false,
                value: IndicatorValue::Simple(0.0),
            });
        } else if gain_idx == period - 1 {
            avg_gain = gains[..period].iter().sum::<f64>() / period as f64;
            avg_loss = losses[..period].iter().sum::<f64>() / period as f64;
            let rsi = if avg_loss == 0.0 {
                100.0
            } else {
                100.0 - (100.0 / (1.0 + avg_gain / avg_loss))
            };
            values.push(IndicatorPoint {
                date: bar.date,
                valid: true,
                value: IndicatorValue::Simple(rsi),
            });
        } else {
            avg_gain = (avg_gain * (period - 1) as f64 + gains[gain_idx]) / period as f64;
            avg_loss = (avg_loss * (period - 1) as f64 + losses[gain_idx]) / period as f64;
            let rsi = if avg_loss == 0.0 {
                100.0
            } else {
                100.0 - (100.0 / (1.0 + avg_gain / avg_loss))
            };
            values.push(IndicatorPoint {
                date: bar.date,
                valid: true,
                value: IndicatorValue::Simple(rsi),
            });
        }
    }

    IndicatorSeries {
        indicator_type: IndicatorType::Rsi(period),
        values,
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use chrono::NaiveDate;

    fn make_bar(date: &str, close: f64) -> OhlcvBar {
        OhlcvBar {
            code: "TEST".into(),
            exchange: "ASX".into(),
            date: NaiveDate::parse_from_str(date, "%Y-%m-%d").unwrap(),
            open: close,
            high: close,
            low: close,
            close,
            volume: 1000,
        }
    }

    #[test]
    fn rsi_empty_bars() {
        let bars: Vec<OhlcvBar> = vec![];
        let series = calculate_rsi(&bars, 14);
        assert_eq!(series.values.len(), 0);
    }

    #[test]
    fn rsi_single_bar() {
        let bars = vec![make_bar("2024-01-01", 100.0)];
        let series = calculate_rsi(&bars, 14);
        assert_eq!(series.values.len(), 1);
        assert!(!series.values[0].valid);
    }

    #[test]
    fn rsi_warmup_period() {
        let bars: Vec<OhlcvBar> = (1..=15)
            .map(|i| {
                let date = format!("2024-01-{:02}", i);
                make_bar(&date, 100.0 + (i as f64 % 5.0) * 2.0)
            })
            .collect();

        let series = calculate_rsi(&bars, 14);

        assert_eq!(series.values.len(), 15);

        for i in 0..14 {
            assert!(!series.values[i].valid, "Bar {} should be invalid", i);
        }
        assert!(series.values[14].valid, "Bar 14 should be valid");
    }

    #[test]
    fn rsi_all_gains_no_losses() {
        let bars: Vec<OhlcvBar> = (0..15)
            .map(|i| {
                let day = i + 1;
                let date = format!("2024-01-{:02}", day);
                make_bar(&date, 100.0 + i as f64)
            })
            .collect();

        let series = calculate_rsi(&bars, 14);

        if let IndicatorValue::Simple(rsi) = series.values[14].value {
            assert!(
                (rsi - 100.0).abs() < f64::EPSILON,
                "RSI should be 100 when all gains"
            );
        } else {
            panic!("Expected Simple value");
        }
    }

    #[test]
    fn rsi_all_losses_no_gains() {
        let bars: Vec<OhlcvBar> = (0..15)
            .map(|i| {
                let day = i + 1;
                let date = format!("2024-01-{:02}", day);
                make_bar(&date, 100.0 - i as f64)
            })
            .collect();

        let series = calculate_rsi(&bars, 14);

        if let IndicatorValue::Simple(rsi) = series.values[14].value {
            assert!(
                (rsi - 0.0).abs() < f64::EPSILON,
                "RSI should be 0 when all losses"
            );
        } else {
            panic!("Expected Simple value");
        }
    }

    #[test]
    fn rsi_in_range() {
        let bars: Vec<OhlcvBar> = (1..=20)
            .map(|i| {
                let date = format!("2024-01-{:02}", i);
                let close = 100.0 + (i as f64 % 7.0 - 3.0) * 2.0;
                make_bar(&date, close)
            })
            .collect();

        let series = calculate_rsi(&bars, 14);

        for point in &series.values {
            if point.valid {
                if let IndicatorValue::Simple(rsi) = point.value {
                    assert!(rsi >= 0.0 && rsi <= 100.0, "RSI {} out of range", rsi);
                }
            }
        }
    }

    #[test]
    fn rsi_indicator_type() {
        let bars = vec![make_bar("2024-01-01", 100.0)];
        let series = calculate_rsi(&bars, 14);
        assert_eq!(series.indicator_type, IndicatorType::Rsi(14));
    }

    #[test]
    fn rsi_zero_period() {
        let bars = vec![make_bar("2024-01-01", 100.0), make_bar("2024-01-02", 101.0)];
        let series = calculate_rsi(&bars, 0);
        assert_eq!(series.values.len(), 2);
        for point in &series.values {
            assert!(!point.valid);
        }
    }

    #[test]
    fn rsi_known_calculation() {
        let bars: Vec<OhlcvBar> = vec![
            make_bar("2024-01-01", 44.0),
            make_bar("2024-01-02", 44.25),
            make_bar("2024-01-03", 44.50),
            make_bar("2024-01-04", 43.75),
            make_bar("2024-01-05", 44.50),
            make_bar("2024-01-06", 44.25),
            make_bar("2024-01-07", 44.75),
            make_bar("2024-01-08", 45.25),
            make_bar("2024-01-09", 45.50),
            make_bar("2024-01-10", 45.25),
            make_bar("2024-01-11", 45.50),
            make_bar("2024-01-12", 46.0),
            make_bar("2024-01-13", 46.25),
            make_bar("2024-01-14", 46.0),
            make_bar("2024-01-15", 46.50),
        ];

        let series = calculate_rsi(&bars, 14);

        assert!(series.values[14].valid);

        if let IndicatorValue::Simple(rsi) = series.values[14].value {
            assert!(
                rsi > 50.0 && rsi < 100.0,
                "RSI should be in bullish territory"
            );
        } else {
            panic!("Expected Simple value");
        }
    }
}
