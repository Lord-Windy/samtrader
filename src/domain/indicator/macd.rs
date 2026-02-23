//! MACD (Moving Average Convergence Divergence) indicator (TRD §4.4.5).
//!
//! MACD Line = EMA(fast) - EMA(slow)
//! Signal Line = EMA(signal) of MACD Line
//! Histogram = MACD Line - Signal Line
//!
//! Default parameters: fast=12, slow=26, signal=9
//! Warmup: max(fast, slow) - 1 + signal - 1 bars (i.e., slow - 1 + signal - 1 for defaults)

use crate::domain::indicator::{
    calculate_ema, IndicatorPoint, IndicatorSeries, IndicatorType, IndicatorValue,
};
use crate::domain::ohlcv::OhlcvBar;

pub const DEFAULT_FAST: usize = 12;
pub const DEFAULT_SLOW: usize = 26;
pub const DEFAULT_SIGNAL: usize = 9;

pub fn calculate_macd(
    bars: &[OhlcvBar],
    fast: usize,
    slow: usize,
    signal_period: usize,
) -> IndicatorSeries {
    if bars.is_empty() || fast == 0 || slow == 0 || signal_period == 0 {
        return IndicatorSeries {
            indicator_type: IndicatorType::Macd {
                fast,
                slow,
                signal: signal_period,
            },
            values: Vec::new(),
        };
    }

    let ema_fast = ema_raw_values(bars, fast);
    let ema_slow = ema_raw_values(bars, slow);

    let mut macd_line: Vec<f64> = Vec::with_capacity(bars.len());
    for i in 0..bars.len() {
        macd_line.push(ema_fast[i] - ema_slow[i]);
    }

    let k = 2.0 / (signal_period as f64 + 1.0);
    let mut signal_line: Vec<f64> = vec![0.0; bars.len()];
    let macd_warmup = slow - 1;

    if bars.len() > macd_warmup {
        let mut sum = 0.0;
        let signal_seed_end = (macd_warmup + signal_period).min(bars.len());
        for value in macd_line.iter().take(signal_seed_end).skip(macd_warmup) {
            sum += value;
        }

        if macd_warmup + signal_period <= bars.len() {
            let mut signal_ema = sum / signal_period as f64;
            signal_line[macd_warmup + signal_period - 1] = signal_ema;

            for i in (macd_warmup + signal_period)..bars.len() {
                signal_ema = macd_line[i] * k + signal_ema * (1.0 - k);
                signal_line[i] = signal_ema;
            }
        }
    }

    let signal_warmup = slow - 1 + signal_period - 1;

    let mut values = Vec::with_capacity(bars.len());
    for (i, bar) in bars.iter().enumerate() {
        let valid = i >= signal_warmup;
        let macd = macd_line[i];
        let signal = signal_line[i];
        let histogram = macd - signal;

        values.push(IndicatorPoint {
            date: bar.date,
            valid,
            value: IndicatorValue::Macd {
                line: macd,
                signal,
                histogram,
            },
        });
    }

    IndicatorSeries {
        indicator_type: IndicatorType::Macd {
            fast,
            slow,
            signal: signal_period,
        },
        values,
    }
}

pub fn calculate_macd_default(bars: &[OhlcvBar]) -> IndicatorSeries {
    calculate_macd(bars, DEFAULT_FAST, DEFAULT_SLOW, DEFAULT_SIGNAL)
}

/// Extract raw f64 values from the EMA module, using 0.0 for warmup bars.
fn ema_raw_values(bars: &[OhlcvBar], period: usize) -> Vec<f64> {
    let series = calculate_ema(bars, period);
    series
        .values
        .iter()
        .map(|p| match p.value {
            IndicatorValue::Simple(v) => v,
            _ => 0.0,
        })
        .collect()
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
    fn macd_warmup_default() {
        let bars: Vec<OhlcvBar> = (0..40)
            .map(|i| {
                let month = i / 28 + 1;
                let day = i % 28 + 1;
                OhlcvBar {
                    code: "TEST".into(),
                    exchange: "TEST".into(),
                    date: NaiveDate::from_ymd_opt(2024, month as u32, day as u32).unwrap(),
                    open: 100.0 + i as f64,
                    high: 100.0 + i as f64,
                    low: 100.0 + i as f64,
                    close: 100.0 + i as f64,
                    volume: 1000,
                }
            })
            .collect();

        let series = calculate_macd_default(&bars);

        let warmup = DEFAULT_SLOW - 1 + DEFAULT_SIGNAL - 1;
        for i in 0..warmup {
            assert!(!series.values[i].valid, "Index {} should not be valid", i);
        }
        assert!(
            series.values[warmup].valid,
            "Index {} should be valid",
            warmup
        );
    }

    #[test]
    fn macd_histogram_equals_line_minus_signal() {
        let bars: Vec<OhlcvBar> = (0..40)
            .map(|i| {
                let month = i / 28 + 1;
                let day = i % 28 + 1;
                OhlcvBar {
                    code: "TEST".into(),
                    exchange: "TEST".into(),
                    date: NaiveDate::from_ymd_opt(2024, month as u32, day as u32).unwrap(),
                    open: 100.0 + i as f64,
                    high: 100.0 + i as f64,
                    low: 100.0 + i as f64,
                    close: 100.0 + i as f64,
                    volume: 1000,
                }
            })
            .collect();

        let series = calculate_macd_default(&bars);

        for point in &series.values {
            if point.valid {
                if let IndicatorValue::Macd {
                    line,
                    signal,
                    histogram,
                } = point.value
                {
                    assert!((histogram - (line - signal)).abs() < f64::EPSILON);
                }
            }
        }
    }

    #[test]
    fn macd_indicator_type() {
        let bars = make_bars(&[100.0, 101.0, 102.0]);
        let series = calculate_macd(&bars, 5, 10, 3);

        assert_eq!(
            series.indicator_type,
            IndicatorType::Macd {
                fast: 5,
                slow: 10,
                signal: 3
            }
        );
    }

    #[test]
    fn macd_empty_bars() {
        let bars: Vec<OhlcvBar> = vec![];
        let series = calculate_macd_default(&bars);
        assert!(series.values.is_empty());
    }

    #[test]
    fn macd_zero_period() {
        let bars = make_bars(&[100.0, 101.0, 102.0]);

        let series = calculate_macd(&bars, 0, 26, 9);
        assert!(series.values.is_empty());

        let series = calculate_macd(&bars, 12, 0, 9);
        assert!(series.values.is_empty());

        let series = calculate_macd(&bars, 12, 26, 0);
        assert!(series.values.is_empty());
    }

    #[test]
    fn macd_line_is_ema_fast_minus_ema_slow() {
        let bars = make_bars(&[10.0, 20.0, 30.0, 40.0, 50.0, 60.0, 70.0, 80.0, 90.0, 100.0]);
        let series = calculate_macd(&bars, 3, 5, 2);

        let ema_fast = ema_raw_values(&bars, 3);
        let ema_slow = ema_raw_values(&bars, 5);

        for (i, point) in series.values.iter().enumerate() {
            if let IndicatorValue::Macd { line, .. } = point.value {
                let expected_line = ema_fast[i] - ema_slow[i];
                assert!(
                    (line - expected_line).abs() < f64::EPSILON,
                    "MACD line mismatch at index {}",
                    i
                );
            }
        }
    }

    #[test]
    fn macd_default_constants() {
        assert_eq!(DEFAULT_FAST, 12);
        assert_eq!(DEFAULT_SLOW, 26);
        assert_eq!(DEFAULT_SIGNAL, 9);
    }

    #[test]
    fn macd_calculate_default_uses_defaults() {
        let bars: Vec<OhlcvBar> = (0..40)
            .map(|i| {
                let month = i / 28 + 1;
                let day = i % 28 + 1;
                OhlcvBar {
                    code: "TEST".into(),
                    exchange: "TEST".into(),
                    date: NaiveDate::from_ymd_opt(2024, month as u32, day as u32).unwrap(),
                    open: 100.0 + i as f64,
                    high: 100.0 + i as f64,
                    low: 100.0 + i as f64,
                    close: 100.0 + i as f64,
                    volume: 1000,
                }
            })
            .collect();

        let series_default = calculate_macd_default(&bars);
        let series_explicit = calculate_macd(&bars, DEFAULT_FAST, DEFAULT_SLOW, DEFAULT_SIGNAL);

        assert_eq!(
            series_default.indicator_type,
            series_explicit.indicator_type
        );
        assert_eq!(series_default.values.len(), series_explicit.values.len());
    }

    #[test]
    fn macd_custom_parameters() {
        let bars: Vec<OhlcvBar> = (0..20)
            .map(|i| OhlcvBar {
                code: "TEST".into(),
                exchange: "TEST".into(),
                date: NaiveDate::from_ymd_opt(2024, 1, (i + 1) as u32).unwrap(),
                open: 100.0 + i as f64,
                high: 100.0 + i as f64,
                low: 100.0 + i as f64,
                close: 100.0 + i as f64,
                volume: 1000,
            })
            .collect();

        let series = calculate_macd(&bars, 5, 10, 3);

        let warmup = 10 - 1 + 3 - 1;
        assert!(!series.values[warmup - 1].valid);
        assert!(series.values[warmup].valid);
    }
}
