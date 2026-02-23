//! Technical indicator implementations (TRD Section 3.2).
//!
//! This module provides types for representing indicator values and series:
//! - `IndicatorPoint`: A single point in an indicator time series
//! - `IndicatorValue`: Enum for different indicator output shapes
//! - `IndicatorType`: Enum for indicator identity + parameters (serves as HashMap key)
//! - `IndicatorSeries`: A time series of indicator values

mod obv;
pub mod rsi;
pub mod stddev;

pub use obv::*;
pub use rsi::*;

use crate::domain::ohlcv::OhlcvBar;
use chrono::NaiveDate;
use std::fmt;

#[derive(Debug, Clone)]
pub struct IndicatorPoint {
    pub date: NaiveDate,
    pub valid: bool,
    pub value: IndicatorValue,
}

#[derive(Debug, Clone)]
pub enum IndicatorValue {
    Simple(f64),
    Macd {
        line: f64,
        signal: f64,
        histogram: f64,
    },
    Stochastic {
        k: f64,
        d: f64,
    },
    Bollinger {
        upper: f64,
        middle: f64,
        lower: f64,
    },
    Pivot {
        pivot: f64,
        r1: f64,
        r2: f64,
        r3: f64,
        s1: f64,
        s2: f64,
        s3: f64,
    },
}

#[derive(Debug, Clone, PartialEq, Eq, Hash)]
pub enum IndicatorType {
    Sma(usize),
    Ema(usize),
    Wma(usize),
    Rsi(usize),
    Roc(usize),
    Atr(usize),
    Stddev(usize),
    Obv,
    Vwap,
    Macd {
        fast: usize,
        slow: usize,
        signal: usize,
    },
    Stochastic {
        k_period: usize,
        d_period: usize,
    },
    Bollinger {
        period: usize,
        stddev_mult_x100: u32,
    },
    Pivot,
}

#[derive(Debug, Clone)]
pub struct IndicatorSeries {
    pub indicator_type: IndicatorType,
    pub values: Vec<IndicatorPoint>,
}

pub fn compute_pivot(bars: &[OhlcvBar]) -> IndicatorSeries {
    let mut values = Vec::with_capacity(bars.len());

    for (i, bar) in bars.iter().enumerate() {
        if i == 0 {
            values.push(IndicatorPoint {
                date: bar.date,
                valid: false,
                value: IndicatorValue::Pivot {
                    pivot: 0.0,
                    r1: 0.0,
                    r2: 0.0,
                    r3: 0.0,
                    s1: 0.0,
                    s2: 0.0,
                    s3: 0.0,
                },
            });
        } else {
            let prev = &bars[i - 1];
            let h = prev.high;
            let l = prev.low;
            let c = prev.close;

            let pivot = (h + l + c) / 3.0;
            let r1 = (2.0 * pivot) - l;
            let s1 = (2.0 * pivot) - h;
            let r2 = pivot + (h - l);
            let s2 = pivot - (h - l);
            let r3 = h + 2.0 * (pivot - l);
            let s3 = l - 2.0 * (h - pivot);

            values.push(IndicatorPoint {
                date: bar.date,
                valid: true,
                value: IndicatorValue::Pivot {
                    pivot,
                    r1,
                    r2,
                    r3,
                    s1,
                    s2,
                    s3,
                },
            });
        }
    }

    IndicatorSeries {
        indicator_type: IndicatorType::Pivot,
        values,
    }
}

impl fmt::Display for IndicatorType {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        match self {
            IndicatorType::Sma(period) => write!(f, "SMA({})", period),
            IndicatorType::Ema(period) => write!(f, "EMA({})", period),
            IndicatorType::Wma(period) => write!(f, "WMA({})", period),
            IndicatorType::Rsi(period) => write!(f, "RSI({})", period),
            IndicatorType::Roc(period) => write!(f, "ROC({})", period),
            IndicatorType::Atr(period) => write!(f, "ATR({})", period),
            IndicatorType::Stddev(period) => write!(f, "STDDEV({})", period),
            IndicatorType::Obv => write!(f, "OBV"),
            IndicatorType::Vwap => write!(f, "VWAP"),
            IndicatorType::Macd { fast, slow, signal } => {
                write!(f, "MACD({},{},{})", fast, slow, signal)
            }
            IndicatorType::Stochastic { k_period, d_period } => {
                write!(f, "STOCHASTIC({},{})", k_period, d_period)
            }
            IndicatorType::Bollinger {
                period,
                stddev_mult_x100,
            } => {
                let mult = *stddev_mult_x100 as f64 / 100.0;
                write!(f, "BOLLINGER({},{})", period, mult)
            }
            IndicatorType::Pivot => write!(f, "PIVOT"),
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn indicator_type_display_sma() {
        assert_eq!(IndicatorType::Sma(20).to_string(), "SMA(20)");
    }

    #[test]
    fn indicator_type_display_macd() {
        let macd = IndicatorType::Macd {
            fast: 12,
            slow: 26,
            signal: 9,
        };
        assert_eq!(macd.to_string(), "MACD(12,26,9)");
    }

    #[test]
    fn indicator_type_display_bollinger() {
        let boll = IndicatorType::Bollinger {
            period: 20,
            stddev_mult_x100: 200,
        };
        assert_eq!(boll.to_string(), "BOLLINGER(20,2)");
    }

    #[test]
    fn indicator_type_display_pivot() {
        assert_eq!(IndicatorType::Pivot.to_string(), "PIVOT");
    }

    #[test]
    fn indicator_type_hash_eq() {
        use std::collections::HashMap;

        let mut map = HashMap::new();
        let sma20 = IndicatorType::Sma(20);
        let sma50 = IndicatorType::Sma(50);
        let macd = IndicatorType::Macd {
            fast: 12,
            slow: 26,
            signal: 9,
        };

        map.insert(sma20.clone(), "sma20_series".to_string());
        map.insert(sma50.clone(), "sma50_series".to_string());
        map.insert(macd.clone(), "macd_series".to_string());

        assert_eq!(map.get(&sma20), Some(&"sma20_series".to_string()));
        assert_eq!(map.get(&sma50), Some(&"sma50_series".to_string()));
        assert_eq!(map.get(&macd), Some(&"macd_series".to_string()));
        assert_eq!(
            map.get(&IndicatorType::Sma(20)),
            Some(&"sma20_series".to_string())
        );
    }

    #[test]
    fn pivot_first_bar_invalid() {
        let bars = vec![OhlcvBar {
            code: "TEST".into(),
            exchange: "ASX".into(),
            date: NaiveDate::from_ymd_opt(2024, 1, 1).unwrap(),
            open: 100.0,
            high: 110.0,
            low: 90.0,
            close: 105.0,
            volume: 1000,
        }];

        let series = compute_pivot(&bars);
        assert_eq!(series.indicator_type, IndicatorType::Pivot);
        assert_eq!(series.values.len(), 1);
        assert!(!series.values[0].valid);
    }

    #[test]
    fn pivot_calculation() {
        let bars = vec![
            OhlcvBar {
                code: "TEST".into(),
                exchange: "ASX".into(),
                date: NaiveDate::from_ymd_opt(2024, 1, 1).unwrap(),
                open: 100.0,
                high: 110.0,
                low: 90.0,
                close: 105.0,
                volume: 1000,
            },
            OhlcvBar {
                code: "TEST".into(),
                exchange: "ASX".into(),
                date: NaiveDate::from_ymd_opt(2024, 1, 2).unwrap(),
                open: 105.0,
                high: 115.0,
                low: 100.0,
                close: 110.0,
                volume: 1200,
            },
        ];

        let series = compute_pivot(&bars);
        assert_eq!(series.values.len(), 2);

        assert!(!series.values[0].valid);

        assert!(series.values[1].valid);
        let h = 110.0;
        let l = 90.0;
        let c = 105.0;
        let pivot = (h + l + c) / 3.0;

        match &series.values[1].value {
            IndicatorValue::Pivot {
                pivot: p,
                r1,
                r2,
                r3,
                s1,
                s2,
                s3,
            } => {
                assert!((p - pivot).abs() < f64::EPSILON);
                assert!((r1 - ((2.0 * pivot) - l)).abs() < f64::EPSILON);
                assert!((s1 - ((2.0 * pivot) - h)).abs() < f64::EPSILON);
                assert!((r2 - (pivot + (h - l))).abs() < f64::EPSILON);
                assert!((s2 - (pivot - (h - l))).abs() < f64::EPSILON);
                assert!((r3 - (h + 2.0 * (pivot - l))).abs() < f64::EPSILON);
                assert!((s3 - (l - 2.0 * (h - pivot))).abs() < f64::EPSILON);
            }
            _ => panic!("Expected Pivot value"),
        }
    }
}
