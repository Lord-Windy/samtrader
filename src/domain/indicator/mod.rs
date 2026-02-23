//! Technical indicator implementations (TRD Section 3.2).
//!
//! This module provides types for representing indicator values and series:
//! - `IndicatorPoint`: A single point in an indicator time series
//! - `IndicatorValue`: Enum for different indicator output shapes
//! - `IndicatorType`: Enum for indicator identity + parameters (serves as HashMap key)
//! - `IndicatorSeries`: A time series of indicator values

pub mod stddev;

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
}
