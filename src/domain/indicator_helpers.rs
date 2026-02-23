//! Shared indicator pre-computation logic and caching (TRD Section 4.5).
//!
//! This module provides:
//! - `IndicatorCache`: A HashMap-based cache for pre-computed indicator series
//! - Calculation functions for all 13 indicator types from TRD Section 4.1

use crate::domain::indicator::{
    calculate_bollinger, calculate_ema, calculate_macd, IndicatorPoint, IndicatorSeries,
    IndicatorType, IndicatorValue,
};
use crate::domain::ohlcv::OhlcvBar;
use chrono::NaiveDate;
use std::collections::HashMap;

pub type IndicatorCache = HashMap<IndicatorType, IndicatorSeries>;

pub fn compute_indicator(bars: &[OhlcvBar], indicator_type: &IndicatorType) -> IndicatorSeries {
    let values = match indicator_type {
        IndicatorType::Sma(period) => compute_sma(bars, *period),
        IndicatorType::Ema(period) => {
            return calculate_ema(bars, *period);
        }
        IndicatorType::Wma(period) => compute_wma(bars, *period),
        IndicatorType::Rsi(period) => compute_rsi(bars, *period),
        IndicatorType::Roc(period) => compute_roc(bars, *period),
        IndicatorType::Atr(period) => compute_atr(bars, *period),
        IndicatorType::Stddev(period) => compute_stddev(bars, *period),
        IndicatorType::Obv => compute_obv(bars),
        IndicatorType::Vwap => compute_vwap(bars),
        IndicatorType::Macd { fast, slow, signal } => {
            return calculate_macd(bars, *fast, *slow, *signal);
        }
        IndicatorType::Stochastic { k_period, d_period } => {
            compute_stochastic(bars, *k_period, *d_period)
        }
        IndicatorType::Bollinger {
            period,
            stddev_mult_x100,
        } => {
            return calculate_bollinger(bars, *period, *stddev_mult_x100);
        }
        IndicatorType::Pivot => compute_pivot(bars),
    };

    IndicatorSeries {
        indicator_type: indicator_type.clone(),
        values,
    }
}

pub fn compute_indicators(bars: &[OhlcvBar], indicator_types: &[IndicatorType]) -> IndicatorCache {
    let mut cache = IndicatorCache::new();
    for it in indicator_types {
        if !cache.contains_key(it) {
            cache.insert(it.clone(), compute_indicator(bars, it));
        }
    }
    cache
}

pub fn get_indicator_value(
    cache: &IndicatorCache,
    indicator_type: &IndicatorType,
    date: NaiveDate,
) -> Option<f64> {
    cache.get(indicator_type).and_then(|series| {
        series
            .values
            .iter()
            .find(|p| p.date == date && p.valid)
            .and_then(|p| extract_simple_value(&p.value))
    })
}

pub fn extract_simple_value(value: &IndicatorValue) -> Option<f64> {
    match value {
        IndicatorValue::Simple(v) => Some(*v),
        _ => None,
    }
}

fn make_point(date: NaiveDate, valid: bool, value: IndicatorValue) -> IndicatorPoint {
    IndicatorPoint { date, valid, value }
}

fn compute_sma(bars: &[OhlcvBar], period: usize) -> Vec<IndicatorPoint> {
    if period == 0 || bars.is_empty() {
        return Vec::new();
    }

    let mut result = Vec::with_capacity(bars.len());
    let mut sum = 0.0;

    for (i, bar) in bars.iter().enumerate() {
        sum += bar.close;

        if i >= period {
            sum -= bars[i - period].close;
        }

        let valid = i >= period - 1;
        let value = if valid { sum / period as f64 } else { 0.0 };

        result.push(make_point(bar.date, valid, IndicatorValue::Simple(value)));
    }

    result
}

fn compute_wma(bars: &[OhlcvBar], period: usize) -> Vec<IndicatorPoint> {
    if period == 0 || bars.is_empty() {
        return Vec::new();
    }

    let mut result = Vec::with_capacity(bars.len());
    let divisor = (period * (period + 1)) as f64 / 2.0;
    let mut weighted_sum = 0.0;
    let mut window_sum = 0.0;

    for (i, bar) in bars.iter().enumerate() {
        if i < period {
            let weight = (i + 1) as f64;
            weighted_sum += weight * bar.close;
            window_sum += bar.close;
        } else {
            // Recurrence: new_weighted = old_weighted + period*new_close - old_window_sum
            // window_sum must be updated AFTER use (it represents the previous window).
            weighted_sum += period as f64 * bar.close - window_sum;
            window_sum += bar.close - bars[i - period].close;
        }

        let valid = i >= period - 1;
        let wma = if valid { weighted_sum / divisor } else { 0.0 };

        result.push(make_point(bar.date, valid, IndicatorValue::Simple(wma)));
    }

    result
}

fn compute_rsi(bars: &[OhlcvBar], period: usize) -> Vec<IndicatorPoint> {
    if period == 0 || bars.len() < 2 {
        return bars
            .iter()
            .map(|b| make_point(b.date, false, IndicatorValue::Simple(0.0)))
            .collect();
    }

    let mut result = Vec::with_capacity(bars.len());
    result.push(make_point(bars[0].date, false, IndicatorValue::Simple(0.0)));

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
            result.push(make_point(bar.date, false, IndicatorValue::Simple(0.0)));
        } else if gain_idx == period - 1 {
            avg_gain = gains[..period].iter().sum::<f64>() / period as f64;
            avg_loss = losses[..period].iter().sum::<f64>() / period as f64;
            let rsi = if avg_loss == 0.0 {
                100.0
            } else {
                100.0 - (100.0 / (1.0 + avg_gain / avg_loss))
            };
            result.push(make_point(bar.date, true, IndicatorValue::Simple(rsi)));
        } else {
            avg_gain = (avg_gain * (period - 1) as f64 + gains[gain_idx]) / period as f64;
            avg_loss = (avg_loss * (period - 1) as f64 + losses[gain_idx]) / period as f64;
            let rsi = if avg_loss == 0.0 {
                100.0
            } else {
                100.0 - (100.0 / (1.0 + avg_gain / avg_loss))
            };
            result.push(make_point(bar.date, true, IndicatorValue::Simple(rsi)));
        }
    }

    result
}

fn compute_roc(bars: &[OhlcvBar], period: usize) -> Vec<IndicatorPoint> {
    if period == 0 || bars.is_empty() {
        return Vec::new();
    }

    let mut result = Vec::with_capacity(bars.len());

    for (i, bar) in bars.iter().enumerate() {
        let valid = i >= period;
        let roc = if valid {
            let prev_close = bars[i - period].close;
            if prev_close == 0.0 {
                0.0
            } else {
                ((bar.close - prev_close) / prev_close) * 100.0
            }
        } else {
            0.0
        };
        result.push(make_point(bar.date, valid, IndicatorValue::Simple(roc)));
    }

    result
}

fn compute_atr(bars: &[OhlcvBar], period: usize) -> Vec<IndicatorPoint> {
    if period == 0 || bars.is_empty() {
        return Vec::new();
    }

    let mut result = Vec::with_capacity(bars.len());
    let mut tr_sum = 0.0;
    let mut atr = 0.0;

    for (i, bar) in bars.iter().enumerate() {
        let tr = if i == 0 {
            bar.high - bar.low
        } else {
            bar.true_range(bars[i - 1].close)
        };

        if i < period - 1 {
            tr_sum += tr;
            result.push(make_point(bar.date, false, IndicatorValue::Simple(0.0)));
        } else if i == period - 1 {
            tr_sum += tr;
            atr = tr_sum / period as f64;
            result.push(make_point(bar.date, true, IndicatorValue::Simple(atr)));
        } else {
            atr = (atr * (period - 1) as f64 + tr) / period as f64;
            result.push(make_point(bar.date, true, IndicatorValue::Simple(atr)));
        }
    }

    result
}

fn compute_stddev(bars: &[OhlcvBar], period: usize) -> Vec<IndicatorPoint> {
    if period == 0 || bars.is_empty() {
        return Vec::new();
    }

    let mut result = Vec::with_capacity(bars.len());
    let mut sum = 0.0;
    let mut sum_sq = 0.0;

    for (i, bar) in bars.iter().enumerate() {
        sum += bar.close;
        sum_sq += bar.close * bar.close;

        if i >= period {
            let old_close = bars[i - period].close;
            sum -= old_close;
            sum_sq -= old_close * old_close;
        }

        let valid = i >= period - 1;
        let stddev = if valid {
            let mean = sum / period as f64;
            let variance = sum_sq / period as f64 - mean * mean;
            variance.max(0.0).sqrt()
        } else {
            0.0
        };

        result.push(make_point(bar.date, valid, IndicatorValue::Simple(stddev)));
    }

    result
}

fn compute_obv(bars: &[OhlcvBar]) -> Vec<IndicatorPoint> {
    if bars.is_empty() {
        return Vec::new();
    }

    let mut result = Vec::with_capacity(bars.len());
    let mut obv = bars[0].volume as f64;

    result.push(make_point(bars[0].date, true, IndicatorValue::Simple(obv)));

    for i in 1..bars.len() {
        let change = bars[i].close - bars[i - 1].close;
        if change > 0.0 {
            obv += bars[i].volume as f64;
        } else if change < 0.0 {
            obv -= bars[i].volume as f64;
        }
        result.push(make_point(bars[i].date, true, IndicatorValue::Simple(obv)));
    }

    result
}

fn compute_vwap(bars: &[OhlcvBar]) -> Vec<IndicatorPoint> {
    if bars.is_empty() {
        return Vec::new();
    }

    let mut result = Vec::with_capacity(bars.len());
    let mut cum_tp_vol = 0.0;
    let mut cum_vol = 0.0;

    for bar in bars {
        let tp = bar.typical_price();
        cum_tp_vol += tp * bar.volume as f64;
        cum_vol += bar.volume as f64;

        let vwap = if cum_vol == 0.0 {
            0.0
        } else {
            cum_tp_vol / cum_vol
        };
        result.push(make_point(bar.date, true, IndicatorValue::Simple(vwap)));
    }

    result
}

fn compute_stochastic(bars: &[OhlcvBar], k_period: usize, d_period: usize) -> Vec<IndicatorPoint> {
    if bars.is_empty() || k_period == 0 || d_period == 0 {
        return Vec::new();
    }

    let mut result = Vec::with_capacity(bars.len());
    let mut k_values: Vec<f64> = Vec::with_capacity(bars.len());

    for (i, bar) in bars.iter().enumerate() {
        let valid_k = i >= k_period - 1;

        let k = if valid_k {
            let start = i + 1 - k_period;
            let lowest_low = bars[start..=i]
                .iter()
                .map(|b| b.low)
                .fold(f64::INFINITY, f64::min);
            let highest_high = bars[start..=i]
                .iter()
                .map(|b| b.high)
                .fold(f64::NEG_INFINITY, f64::max);

            if (highest_high - lowest_low).abs() < f64::EPSILON {
                50.0
            } else {
                100.0 * (bar.close - lowest_low) / (highest_high - lowest_low)
            }
        } else {
            0.0
        };

        k_values.push(k);

        let valid = i >= k_period - 1 + d_period - 1;

        let d = if valid {
            let start = k_values.len() - d_period;
            k_values[start..].iter().sum::<f64>() / d_period as f64
        } else {
            0.0
        };

        result.push(make_point(
            bar.date,
            valid,
            IndicatorValue::Stochastic { k, d },
        ));
    }

    result
}

fn compute_pivot(bars: &[OhlcvBar]) -> Vec<IndicatorPoint> {
    if bars.is_empty() {
        return Vec::new();
    }

    let mut result = Vec::with_capacity(bars.len());
    result.push(make_point(
        bars[0].date,
        false,
        IndicatorValue::Pivot {
            pivot: 0.0,
            r1: 0.0,
            r2: 0.0,
            r3: 0.0,
            s1: 0.0,
            s2: 0.0,
            s3: 0.0,
        },
    ));

    for i in 1..bars.len() {
        let prev = &bars[i - 1];
        let h = prev.high;
        let l = prev.low;
        let c = prev.close;

        let pivot = (h + l + c) / 3.0;
        let r1 = 2.0 * pivot - l;
        let s1 = 2.0 * pivot - h;
        let r2 = pivot + (h - l);
        let s2 = pivot - (h - l);
        let r3 = h + 2.0 * (pivot - l);
        let s3 = l - 2.0 * (h - pivot);

        result.push(make_point(
            bars[i].date,
            true,
            IndicatorValue::Pivot {
                pivot,
                r1,
                r2,
                r3,
                s1,
                s2,
                s3,
            },
        ));
    }

    result
}

#[cfg(test)]
mod tests {
    use super::*;

    fn make_bar(date: &str, close: f64, high: f64, low: f64, volume: i64) -> OhlcvBar {
        OhlcvBar {
            code: "TEST".into(),
            exchange: "ASX".into(),
            date: NaiveDate::parse_from_str(date, "%Y-%m-%d").unwrap(),
            open: close,
            high,
            low,
            close,
            volume,
        }
    }

    fn make_simple_bar(date: &str, close: f64) -> OhlcvBar {
        make_bar(date, close, close, close, 1000)
    }

    #[test]
    fn sma_calculation() {
        let bars = vec![
            make_simple_bar("2024-01-01", 10.0),
            make_simple_bar("2024-01-02", 20.0),
            make_simple_bar("2024-01-03", 30.0),
            make_simple_bar("2024-01-04", 40.0),
            make_simple_bar("2024-01-05", 50.0),
        ];

        let series = compute_indicator(&bars, &IndicatorType::Sma(3));

        assert!(!series.values[0].valid);
        assert!(!series.values[1].valid);
        assert!(series.values[2].valid);

        let v2 = extract_simple_value(&series.values[2].value).unwrap();
        assert!((v2 - 20.0).abs() < f64::EPSILON);

        let v3 = extract_simple_value(&series.values[3].value).unwrap();
        assert!((v3 - 30.0).abs() < f64::EPSILON);

        let v4 = extract_simple_value(&series.values[4].value).unwrap();
        assert!((v4 - 40.0).abs() < f64::EPSILON);
    }

    #[test]
    fn ema_calculation() {
        let bars = vec![
            make_simple_bar("2024-01-01", 10.0),
            make_simple_bar("2024-01-02", 20.0),
            make_simple_bar("2024-01-03", 30.0),
        ];

        let series = compute_indicator(&bars, &IndicatorType::Ema(3));

        assert!(!series.values[0].valid);
        assert!(!series.values[1].valid);
        assert!(series.values[2].valid);

        let v2 = extract_simple_value(&series.values[2].value).unwrap();
        assert!((v2 - 20.0).abs() < f64::EPSILON);
    }

    #[test]
    fn wma_calculation() {
        let bars = vec![
            make_simple_bar("2024-01-01", 10.0),
            make_simple_bar("2024-01-02", 20.0),
            make_simple_bar("2024-01-03", 30.0),
            make_simple_bar("2024-01-04", 40.0),
            make_simple_bar("2024-01-05", 50.0),
        ];

        let series = compute_indicator(&bars, &IndicatorType::Wma(3));

        assert!(!series.values[0].valid);
        assert!(!series.values[1].valid);
        assert!(series.values[2].valid);

        // i=2: (1*10 + 2*20 + 3*30) / 6 = 140/6
        let v2 = extract_simple_value(&series.values[2].value).unwrap();
        let expected2 = (1.0 * 10.0 + 2.0 * 20.0 + 3.0 * 30.0) / 6.0;
        assert!((v2 - expected2).abs() < f64::EPSILON);

        // i=3: (1*20 + 2*30 + 3*40) / 6 = 200/6 (sliding window)
        let v3 = extract_simple_value(&series.values[3].value).unwrap();
        let expected3 = (1.0 * 20.0 + 2.0 * 30.0 + 3.0 * 40.0) / 6.0;
        assert!((v3 - expected3).abs() < f64::EPSILON);

        // i=4: (1*30 + 2*40 + 3*50) / 6 = 260/6
        let v4 = extract_simple_value(&series.values[4].value).unwrap();
        let expected4 = (1.0 * 30.0 + 2.0 * 40.0 + 3.0 * 50.0) / 6.0;
        assert!((v4 - expected4).abs() < f64::EPSILON);
    }

    #[test]
    fn rsi_calculation() {
        let bars: Vec<OhlcvBar> = (1..=15)
            .map(|i| {
                let date = format!("2024-01-{:02}", i);
                let close = 100.0 + (i as f64 % 5.0) * 2.0;
                make_simple_bar(&date, close)
            })
            .collect();

        let series = compute_indicator(&bars, &IndicatorType::Rsi(14));

        assert!(series.values.len() == 15);

        for i in 0..14 {
            assert!(!series.values[i].valid);
        }
        assert!(series.values[14].valid);

        let rsi = extract_simple_value(&series.values[14].value).unwrap();
        assert!(rsi >= 0.0 && rsi <= 100.0);
    }

    #[test]
    fn roc_calculation() {
        let bars = vec![
            make_simple_bar("2024-01-01", 100.0),
            make_simple_bar("2024-01-02", 105.0),
            make_simple_bar("2024-01-03", 110.0),
        ];

        let series = compute_indicator(&bars, &IndicatorType::Roc(2));

        assert!(!series.values[0].valid);
        assert!(!series.values[1].valid);
        assert!(series.values[2].valid);

        let roc = extract_simple_value(&series.values[2].value).unwrap();
        let expected = ((110.0 - 100.0) / 100.0) * 100.0;
        assert!((roc - expected).abs() < f64::EPSILON);
    }

    #[test]
    fn atr_calculation() {
        let bars = vec![
            make_bar("2024-01-01", 100.0, 105.0, 95.0, 1000),
            make_bar("2024-01-02", 102.0, 108.0, 98.0, 1000),
            make_bar("2024-01-03", 104.0, 110.0, 100.0, 1000),
            make_bar("2024-01-04", 106.0, 112.0, 102.0, 1000),
        ];

        let series = compute_indicator(&bars, &IndicatorType::Atr(3));

        assert!(!series.values[0].valid);
        assert!(!series.values[1].valid);
        assert!(series.values[2].valid);
        assert!(series.values[3].valid);
    }

    #[test]
    fn stddev_calculation() {
        let bars = vec![
            make_simple_bar("2024-01-01", 10.0),
            make_simple_bar("2024-01-02", 20.0),
            make_simple_bar("2024-01-03", 30.0),
            make_simple_bar("2024-01-04", 40.0),
        ];

        let series = compute_indicator(&bars, &IndicatorType::Stddev(3));

        assert!(!series.values[0].valid);
        assert!(!series.values[1].valid);
        assert!(series.values[2].valid);
        assert!(series.values[3].valid);

        let v2 = extract_simple_value(&series.values[2].value).unwrap();
        let mean = 20.0;
        let expected = ((10.0_f64 - mean).powi(2) + (20.0 - mean).powi(2) + (30.0 - mean).powi(2))
            .sqrt()
            / 3.0_f64.sqrt();
        assert!((v2 - expected).abs() < 1e-10);
    }

    #[test]
    fn obv_calculation() {
        let bars = vec![
            make_bar("2024-01-01", 100.0, 100.0, 100.0, 1000),
            make_bar("2024-01-02", 102.0, 102.0, 102.0, 2000),
            make_bar("2024-01-03", 99.0, 99.0, 99.0, 1500),
        ];

        let series = compute_indicator(&bars, &IndicatorType::Obv);

        assert!(series.values[0].valid);
        assert!(series.values[1].valid);
        assert!(series.values[2].valid);

        let v0 = extract_simple_value(&series.values[0].value).unwrap();
        assert!((v0 - 1000.0).abs() < f64::EPSILON);

        let v1 = extract_simple_value(&series.values[1].value).unwrap();
        assert!((v1 - 3000.0).abs() < f64::EPSILON);

        let v2 = extract_simple_value(&series.values[2].value).unwrap();
        assert!((v2 - 1500.0).abs() < f64::EPSILON);
    }

    #[test]
    fn vwap_calculation() {
        let bars = vec![
            make_bar("2024-01-01", 100.0, 102.0, 98.0, 1000),
            make_bar("2024-01-02", 102.0, 104.0, 100.0, 2000),
        ];

        let series = compute_indicator(&bars, &IndicatorType::Vwap);

        assert!(series.values[0].valid);
        assert!(series.values[1].valid);

        let tp0 = (102.0 + 98.0 + 100.0) / 3.0;
        let expected_vwap0 = tp0 * 1000.0 / 1000.0;
        let v0 = extract_simple_value(&series.values[0].value).unwrap();
        assert!((v0 - expected_vwap0).abs() < 1e-10);
    }

    #[test]
    fn macd_calculation() {
        let bars: Vec<OhlcvBar> = (0..35)
            .map(|i| {
                let day = i % 28 + 1;
                let month = i / 28 + 1;
                let date = format!("2024-{:02}-{:02}", month, day);
                make_simple_bar(&date, 100.0 + i as f64)
            })
            .collect();

        let series = compute_indicator(
            &bars,
            &IndicatorType::Macd {
                fast: 12,
                slow: 26,
                signal: 9,
            },
        );

        let warmup = 26 - 1 + 9 - 1;
        for i in 0..warmup {
            assert!(!series.values[i].valid, "Index {} should not be valid", i);
        }
        assert!(series.values[warmup].valid);

        if let IndicatorValue::Macd {
            line,
            signal,
            histogram,
        } = series.values[warmup].value
        {
            assert!((histogram - (line - signal)).abs() < f64::EPSILON);
        } else {
            panic!("Expected Macd value");
        }
    }

    #[test]
    fn stochastic_calculation() {
        let bars = vec![
            make_bar("2024-01-01", 100.0, 105.0, 95.0, 1000),
            make_bar("2024-01-02", 102.0, 108.0, 98.0, 1000),
            make_bar("2024-01-03", 104.0, 110.0, 100.0, 1000),
            make_bar("2024-01-04", 106.0, 112.0, 102.0, 1000),
            make_bar("2024-01-05", 108.0, 114.0, 104.0, 1000),
        ];

        let series = compute_indicator(
            &bars,
            &IndicatorType::Stochastic {
                k_period: 3,
                d_period: 2,
            },
        );

        assert!(series.values[4].valid);

        if let IndicatorValue::Stochastic { k, d } = series.values[4].value {
            assert!(k >= 0.0 && k <= 100.0);
            assert!(d >= 0.0 && d <= 100.0);
        } else {
            panic!("Expected Stochastic value");
        }
    }

    #[test]
    fn bollinger_calculation() {
        let bars = vec![
            make_simple_bar("2024-01-01", 10.0),
            make_simple_bar("2024-01-02", 20.0),
            make_simple_bar("2024-01-03", 30.0),
            make_simple_bar("2024-01-04", 40.0),
        ];

        let series = compute_indicator(
            &bars,
            &IndicatorType::Bollinger {
                period: 3,
                stddev_mult_x100: 200,
            },
        );

        assert!(series.values[2].valid);

        if let IndicatorValue::Bollinger {
            upper,
            middle,
            lower,
        } = series.values[2].value
        {
            assert!((middle - 20.0).abs() < f64::EPSILON);
            assert!(upper > middle);
            assert!(lower < middle);
        } else {
            panic!("Expected Bollinger value");
        }
    }

    #[test]
    fn pivot_calculation() {
        let bars = vec![
            make_bar("2024-01-01", 100.0, 105.0, 95.0, 1000),
            make_bar("2024-01-02", 102.0, 108.0, 98.0, 1000),
        ];

        let series = compute_indicator(&bars, &IndicatorType::Pivot);

        assert!(!series.values[0].valid);
        assert!(series.values[1].valid);

        if let IndicatorValue::Pivot {
            pivot,
            r1,
            r2,
            r3,
            s1,
            s2,
            s3,
        } = series.values[1].value
        {
            let expected_pivot = (105.0 + 95.0 + 100.0) / 3.0;
            assert!((pivot - expected_pivot).abs() < f64::EPSILON);

            let expected_r1 = 2.0 * pivot - 95.0;
            let expected_s1 = 2.0 * pivot - 105.0;
            assert!((r1 - expected_r1).abs() < f64::EPSILON);
            assert!((s1 - expected_s1).abs() < f64::EPSILON);

            assert!(r1 > pivot);
            assert!(s1 < pivot);
            assert!(r2 > r1);
            assert!(s2 < s1);
            assert!(r3 > r2);
            assert!(s3 < s2);
        } else {
            panic!("Expected Pivot value");
        }
    }

    #[test]
    fn indicator_cache_compute_indicators() {
        let bars = vec![
            make_simple_bar("2024-01-01", 10.0),
            make_simple_bar("2024-01-02", 20.0),
            make_simple_bar("2024-01-03", 30.0),
        ];

        let indicator_types = vec![
            IndicatorType::Sma(2),
            IndicatorType::Sma(3),
            IndicatorType::Ema(2),
        ];

        let cache = compute_indicators(&bars, &indicator_types);

        assert_eq!(cache.len(), 3);
        assert!(cache.contains_key(&IndicatorType::Sma(2)));
        assert!(cache.contains_key(&IndicatorType::Sma(3)));
        assert!(cache.contains_key(&IndicatorType::Ema(2)));
    }

    #[test]
    fn get_indicator_value_from_cache() {
        let bars = vec![
            make_simple_bar("2024-01-01", 10.0),
            make_simple_bar("2024-01-02", 20.0),
            make_simple_bar("2024-01-03", 30.0),
        ];

        let cache = compute_indicators(&bars, &[IndicatorType::Sma(2)]);

        let date = NaiveDate::from_ymd_opt(2024, 1, 2).unwrap();
        let value = get_indicator_value(&cache, &IndicatorType::Sma(2), date);

        assert!(value.is_some());
        assert!((value.unwrap() - 15.0).abs() < f64::EPSILON);
    }
}
