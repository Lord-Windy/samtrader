//! Rule evaluation engine (TRD Section 5.6).
//!
//! Evaluates rules against OHLCV data and pre-computed indicator values.
//!
//! # Evaluation Semantics
//!
//! - Comparison rules: Evaluate at the given bar index
//! - `CROSS_ABOVE`/`CROSS_BELOW`: Require `index >= 1`, return `false` at index 0
//! - `AND`: Short-circuits on first `false`
//! - `OR`: Short-circuits on first `true`
//! - `CONSECUTIVE(rule, N)`: Child must be true for N consecutive bars ending at current
//! - `ANY_OF(rule, N)`: Child must be true at least once in the last N bars

use crate::domain::indicator::{IndicatorSeries, IndicatorType, IndicatorValue};
use crate::domain::ohlcv::OhlcvBar;
use crate::domain::rule::{IndicatorField, IndicatorRef, Operand, Rule};
use std::collections::HashMap;

const EPSILON: f64 = 1e-9;

pub fn evaluate(
    rule: &Rule,
    ohlcv: &[OhlcvBar],
    indicators: &HashMap<IndicatorType, IndicatorSeries>,
    bar_index: usize,
) -> bool {
    match rule {
        Rule::CrossAbove { left, right } => {
            if bar_index == 0 {
                return false;
            }
            let left_curr = resolve_operand(left, ohlcv, indicators, bar_index);
            let right_curr = resolve_operand(right, ohlcv, indicators, bar_index);
            let left_prev = resolve_operand(left, ohlcv, indicators, bar_index - 1);
            let right_prev = resolve_operand(right, ohlcv, indicators, bar_index - 1);

            left_curr > right_curr && left_prev <= right_prev
        }
        Rule::CrossBelow { left, right } => {
            if bar_index == 0 {
                return false;
            }
            let left_curr = resolve_operand(left, ohlcv, indicators, bar_index);
            let right_curr = resolve_operand(right, ohlcv, indicators, bar_index);
            let left_prev = resolve_operand(left, ohlcv, indicators, bar_index - 1);
            let right_prev = resolve_operand(right, ohlcv, indicators, bar_index - 1);

            left_curr < right_curr && left_prev >= right_prev
        }
        Rule::Above { left, right } => {
            let left_val = resolve_operand(left, ohlcv, indicators, bar_index);
            let right_val = resolve_operand(right, ohlcv, indicators, bar_index);
            left_val > right_val
        }
        Rule::Below { left, right } => {
            let left_val = resolve_operand(left, ohlcv, indicators, bar_index);
            let right_val = resolve_operand(right, ohlcv, indicators, bar_index);
            left_val < right_val
        }
        Rule::Between {
            operand,
            lower,
            upper,
        } => {
            let val = resolve_operand(operand, ohlcv, indicators, bar_index);
            val >= *lower && val <= *upper
        }
        Rule::Equals { left, right } => {
            let left_val = resolve_operand(left, ohlcv, indicators, bar_index);
            let right_val = resolve_operand(right, ohlcv, indicators, bar_index);
            (left_val - right_val).abs() < EPSILON
        }
        Rule::And(rules) => {
            for r in rules {
                if !evaluate(r, ohlcv, indicators, bar_index) {
                    return false;
                }
            }
            true
        }
        Rule::Or(rules) => {
            for r in rules {
                if evaluate(r, ohlcv, indicators, bar_index) {
                    return true;
                }
            }
            false
        }
        Rule::Not(rule) => !evaluate(rule, ohlcv, indicators, bar_index),
        Rule::Consecutive { rule, count } => {
            if bar_index + 1 < *count {
                return false;
            }
            for i in (bar_index + 1 - *count)..=bar_index {
                if !evaluate(rule, ohlcv, indicators, i) {
                    return false;
                }
            }
            true
        }
        Rule::AnyOf { rule, count } => {
            let start = bar_index.saturating_sub(*count - 1);
            for i in start..=bar_index {
                if evaluate(rule, ohlcv, indicators, i) {
                    return true;
                }
            }
            false
        }
    }
}

fn resolve_operand(
    operand: &Operand,
    ohlcv: &[OhlcvBar],
    indicators: &HashMap<IndicatorType, IndicatorSeries>,
    bar_index: usize,
) -> f64 {
    match operand {
        Operand::Open => ohlcv[bar_index].open,
        Operand::High => ohlcv[bar_index].high,
        Operand::Low => ohlcv[bar_index].low,
        Operand::Close => ohlcv[bar_index].close,
        Operand::Volume => ohlcv[bar_index].volume as f64,
        Operand::Constant(v) => *v,
        Operand::Indicator(ind_ref) => resolve_indicator(ind_ref, indicators, bar_index),
    }
}

fn resolve_indicator(
    ind_ref: &IndicatorRef,
    indicators: &HashMap<IndicatorType, IndicatorSeries>,
    bar_index: usize,
) -> f64 {
    let series = match indicators.get(&ind_ref.indicator_type) {
        Some(s) => s,
        None => return f64::NAN,
    };

    if bar_index >= series.values.len() {
        return f64::NAN;
    }

    let point = &series.values[bar_index];
    if !point.valid {
        return f64::NAN;
    }

    extract_field(&point.value, ind_ref.field)
}

fn extract_field(value: &IndicatorValue, field: IndicatorField) -> f64 {
    match (value, field) {
        (IndicatorValue::Simple(v), IndicatorField::Value) => *v,
        (
            IndicatorValue::Macd {
                line,
                signal: _,
                histogram: _,
            },
            IndicatorField::MacdLine,
        ) => *line,
        (
            IndicatorValue::Macd {
                line: _,
                signal,
                histogram: _,
            },
            IndicatorField::MacdSignal,
        ) => *signal,
        (
            IndicatorValue::Macd {
                line: _,
                signal: _,
                histogram,
            },
            IndicatorField::MacdHistogram,
        ) => *histogram,
        (IndicatorValue::Stochastic { k, d: _ }, IndicatorField::StochasticK) => *k,
        (IndicatorValue::Stochastic { k: _, d }, IndicatorField::StochasticD) => *d,
        (
            IndicatorValue::Bollinger {
                upper,
                middle: _,
                lower: _,
            },
            IndicatorField::BollingerUpper,
        ) => *upper,
        (
            IndicatorValue::Bollinger {
                upper: _,
                middle,
                lower: _,
            },
            IndicatorField::BollingerMiddle,
        ) => *middle,
        (
            IndicatorValue::Bollinger {
                upper: _,
                middle: _,
                lower,
            },
            IndicatorField::BollingerLower,
        ) => *lower,
        (
            IndicatorValue::Pivot {
                pivot,
                r1: _,
                r2: _,
                r3: _,
                s1: _,
                s2: _,
                s3: _,
            },
            IndicatorField::Pivot,
        ) => *pivot,
        (
            IndicatorValue::Pivot {
                pivot: _,
                r1,
                r2: _,
                r3: _,
                s1: _,
                s2: _,
                s3: _,
            },
            IndicatorField::R1,
        ) => *r1,
        (
            IndicatorValue::Pivot {
                pivot: _,
                r1: _,
                r2,
                r3: _,
                s1: _,
                s2: _,
                s3: _,
            },
            IndicatorField::R2,
        ) => *r2,
        (
            IndicatorValue::Pivot {
                pivot: _,
                r1: _,
                r2: _,
                r3,
                s1: _,
                s2: _,
                s3: _,
            },
            IndicatorField::R3,
        ) => *r3,
        (
            IndicatorValue::Pivot {
                pivot: _,
                r1: _,
                r2: _,
                r3: _,
                s1,
                s2: _,
                s3: _,
            },
            IndicatorField::S1,
        ) => *s1,
        (
            IndicatorValue::Pivot {
                pivot: _,
                r1: _,
                r2: _,
                r3: _,
                s1: _,
                s2,
                s3: _,
            },
            IndicatorField::S2,
        ) => *s2,
        (
            IndicatorValue::Pivot {
                pivot: _,
                r1: _,
                r2: _,
                r3: _,
                s1: _,
                s2: _,
                s3,
            },
            IndicatorField::S3,
        ) => *s3,
        _ => f64::NAN,
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::domain::rule::IndicatorRef;
    use chrono::NaiveDate;

    fn make_bar(date: u32, open: f64, high: f64, low: f64, close: f64, volume: i64) -> OhlcvBar {
        OhlcvBar {
            code: "TEST".into(),
            exchange: "ASX".into(),
            date: NaiveDate::from_ymd_opt(2024, 1, date).unwrap(),
            open,
            high,
            low,
            close,
            volume,
        }
    }

    fn make_simple_indicator(
        indicator_type: IndicatorType,
        values: Vec<(u32, bool, f64)>,
    ) -> IndicatorSeries {
        IndicatorSeries {
            indicator_type,
            values: values
                .into_iter()
                .map(
                    |(date, valid, v)| crate::domain::indicator::IndicatorPoint {
                        date: NaiveDate::from_ymd_opt(2024, 1, date).unwrap(),
                        valid,
                        value: IndicatorValue::Simple(v),
                    },
                )
                .collect(),
        }
    }

    fn make_ohlcv(bars: Vec<OhlcvBar>) -> Vec<OhlcvBar> {
        bars
    }

    #[test]
    fn evaluate_above_close_gt_constant() {
        let ohlcv = make_ohlcv(vec![make_bar(1, 100.0, 110.0, 90.0, 105.0, 1000)]);
        let rule = Rule::Above {
            left: Operand::Close,
            right: Operand::Constant(100.0),
        };
        assert!(evaluate(&rule, &ohlcv, &HashMap::new(), 0));
    }

    #[test]
    fn evaluate_above_close_lt_constant() {
        let ohlcv = make_ohlcv(vec![make_bar(1, 100.0, 110.0, 90.0, 95.0, 1000)]);
        let rule = Rule::Above {
            left: Operand::Close,
            right: Operand::Constant(100.0),
        };
        assert!(!evaluate(&rule, &ohlcv, &HashMap::new(), 0));
    }

    #[test]
    fn evaluate_below_close_lt_constant() {
        let ohlcv = make_ohlcv(vec![make_bar(1, 100.0, 110.0, 90.0, 95.0, 1000)]);
        let rule = Rule::Below {
            left: Operand::Close,
            right: Operand::Constant(100.0),
        };
        assert!(evaluate(&rule, &ohlcv, &HashMap::new(), 0));
    }

    #[test]
    fn evaluate_between_in_range() {
        let ohlcv = make_ohlcv(vec![make_bar(1, 100.0, 110.0, 90.0, 75.0, 1000)]);
        let rule = Rule::Between {
            operand: Operand::Close,
            lower: 50.0,
            upper: 100.0,
        };
        assert!(evaluate(&rule, &ohlcv, &HashMap::new(), 0));
    }

    #[test]
    fn evaluate_between_out_of_range() {
        let ohlcv = make_ohlcv(vec![make_bar(1, 100.0, 110.0, 90.0, 150.0, 1000)]);
        let rule = Rule::Between {
            operand: Operand::Close,
            lower: 50.0,
            upper: 100.0,
        };
        assert!(!evaluate(&rule, &ohlcv, &HashMap::new(), 0));
    }

    #[test]
    fn evaluate_between_boundary() {
        let ohlcv = make_ohlcv(vec![
            make_bar(1, 100.0, 110.0, 90.0, 50.0, 1000),
            make_bar(2, 100.0, 110.0, 90.0, 100.0, 1000),
        ]);
        let rule = Rule::Between {
            operand: Operand::Close,
            lower: 50.0,
            upper: 100.0,
        };
        assert!(evaluate(&rule, &ohlcv, &HashMap::new(), 0));
        assert!(evaluate(&rule, &ohlcv, &HashMap::new(), 1));
    }

    #[test]
    fn evaluate_equals_true() {
        let ohlcv = make_ohlcv(vec![make_bar(1, 100.0, 110.0, 90.0, 100.0, 1000)]);
        let rule = Rule::Equals {
            left: Operand::Close,
            right: Operand::Constant(100.0),
        };
        assert!(evaluate(&rule, &ohlcv, &HashMap::new(), 0));
    }

    #[test]
    fn evaluate_equals_false() {
        let ohlcv = make_ohlcv(vec![make_bar(1, 100.0, 110.0, 90.0, 100.01, 1000)]);
        let rule = Rule::Equals {
            left: Operand::Close,
            right: Operand::Constant(100.0),
        };
        assert!(!evaluate(&rule, &ohlcv, &HashMap::new(), 0));
    }

    #[test]
    fn evaluate_cross_above_at_index_0() {
        let ohlcv = make_ohlcv(vec![make_bar(1, 100.0, 110.0, 90.0, 105.0, 1000)]);
        let rule = Rule::CrossAbove {
            left: Operand::Close,
            right: Operand::Constant(100.0),
        };
        assert!(!evaluate(&rule, &ohlcv, &HashMap::new(), 0));
    }

    #[test]
    fn evaluate_cross_above_true() {
        let ohlcv = make_ohlcv(vec![
            make_bar(1, 100.0, 110.0, 90.0, 95.0, 1000),
            make_bar(2, 100.0, 110.0, 90.0, 105.0, 1000),
        ]);
        let rule = Rule::CrossAbove {
            left: Operand::Close,
            right: Operand::Constant(100.0),
        };
        assert!(!evaluate(&rule, &ohlcv, &HashMap::new(), 0));
        assert!(evaluate(&rule, &ohlcv, &HashMap::new(), 1));
    }

    #[test]
    fn evaluate_cross_above_false_no_cross() {
        let ohlcv = make_ohlcv(vec![
            make_bar(1, 100.0, 110.0, 90.0, 105.0, 1000),
            make_bar(2, 100.0, 110.0, 90.0, 110.0, 1000),
        ]);
        let rule = Rule::CrossAbove {
            left: Operand::Close,
            right: Operand::Constant(100.0),
        };
        assert!(!evaluate(&rule, &ohlcv, &HashMap::new(), 1));
    }

    #[test]
    fn evaluate_cross_below_true() {
        let ohlcv = make_ohlcv(vec![
            make_bar(1, 100.0, 110.0, 90.0, 105.0, 1000),
            make_bar(2, 100.0, 110.0, 90.0, 95.0, 1000),
        ]);
        let rule = Rule::CrossBelow {
            left: Operand::Close,
            right: Operand::Constant(100.0),
        };
        assert!(!evaluate(&rule, &ohlcv, &HashMap::new(), 0));
        assert!(evaluate(&rule, &ohlcv, &HashMap::new(), 1));
    }

    #[test]
    fn evaluate_and_short_circuit() {
        let ohlcv = make_ohlcv(vec![make_bar(1, 100.0, 110.0, 90.0, 95.0, 1000)]);
        let rule = Rule::And(vec![
            Rule::Above {
                left: Operand::Close,
                right: Operand::Constant(50.0),
            },
            Rule::Below {
                left: Operand::Close,
                right: Operand::Constant(100.0),
            },
        ]);
        assert!(evaluate(&rule, &ohlcv, &HashMap::new(), 0));
    }

    #[test]
    fn evaluate_and_fails_on_first() {
        let ohlcv = make_ohlcv(vec![make_bar(1, 100.0, 110.0, 90.0, 105.0, 1000)]);
        let rule = Rule::And(vec![
            Rule::Above {
                left: Operand::Close,
                right: Operand::Constant(200.0),
            },
            Rule::Below {
                left: Operand::Close,
                right: Operand::Constant(100.0),
            },
        ]);
        assert!(!evaluate(&rule, &ohlcv, &HashMap::new(), 0));
    }

    #[test]
    fn evaluate_or_short_circuit() {
        let ohlcv = make_ohlcv(vec![make_bar(1, 100.0, 110.0, 90.0, 105.0, 1000)]);
        let rule = Rule::Or(vec![
            Rule::Above {
                left: Operand::Close,
                right: Operand::Constant(200.0),
            },
            Rule::Above {
                left: Operand::Close,
                right: Operand::Constant(100.0),
            },
        ]);
        assert!(evaluate(&rule, &ohlcv, &HashMap::new(), 0));
    }

    #[test]
    fn evaluate_or_all_false() {
        let ohlcv = make_ohlcv(vec![make_bar(1, 100.0, 110.0, 90.0, 95.0, 1000)]);
        let rule = Rule::Or(vec![
            Rule::Above {
                left: Operand::Close,
                right: Operand::Constant(200.0),
            },
            Rule::Above {
                left: Operand::Close,
                right: Operand::Constant(100.0),
            },
        ]);
        assert!(!evaluate(&rule, &ohlcv, &HashMap::new(), 0));
    }

    #[test]
    fn evaluate_not() {
        let ohlcv = make_ohlcv(vec![make_bar(1, 100.0, 110.0, 90.0, 105.0, 1000)]);
        let rule = Rule::Not(Box::new(Rule::Above {
            left: Operand::Close,
            right: Operand::Constant(100.0),
        }));
        assert!(!evaluate(&rule, &ohlcv, &HashMap::new(), 0));

        let rule2 = Rule::Not(Box::new(Rule::Below {
            left: Operand::Close,
            right: Operand::Constant(100.0),
        }));
        assert!(evaluate(&rule2, &ohlcv, &HashMap::new(), 0));
    }

    #[test]
    fn evaluate_consecutive_true() {
        let ohlcv = make_ohlcv(vec![
            make_bar(1, 100.0, 110.0, 90.0, 101.0, 1000),
            make_bar(2, 100.0, 110.0, 90.0, 102.0, 1000),
            make_bar(3, 100.0, 110.0, 90.0, 103.0, 1000),
        ]);
        let rule = Rule::Consecutive {
            rule: Box::new(Rule::Above {
                left: Operand::Close,
                right: Operand::Constant(100.0),
            }),
            count: 3,
        };
        assert!(!evaluate(&rule, &ohlcv, &HashMap::new(), 0));
        assert!(!evaluate(&rule, &ohlcv, &HashMap::new(), 1));
        assert!(evaluate(&rule, &ohlcv, &HashMap::new(), 2));
    }

    #[test]
    fn evaluate_consecutive_insufficient_bars() {
        let ohlcv = make_ohlcv(vec![
            make_bar(1, 100.0, 110.0, 90.0, 101.0, 1000),
            make_bar(2, 100.0, 110.0, 90.0, 102.0, 1000),
        ]);
        let rule = Rule::Consecutive {
            rule: Box::new(Rule::Above {
                left: Operand::Close,
                right: Operand::Constant(100.0),
            }),
            count: 3,
        };
        assert!(!evaluate(&rule, &ohlcv, &HashMap::new(), 0));
        assert!(!evaluate(&rule, &ohlcv, &HashMap::new(), 1));
    }

    #[test]
    fn evaluate_any_of_true() {
        let ohlcv = make_ohlcv(vec![
            make_bar(1, 100.0, 110.0, 90.0, 95.0, 1000),
            make_bar(2, 100.0, 110.0, 90.0, 95.0, 1000),
            make_bar(3, 100.0, 110.0, 90.0, 105.0, 1000),
            make_bar(4, 100.0, 110.0, 90.0, 95.0, 1000),
        ]);
        let rule = Rule::AnyOf {
            rule: Box::new(Rule::Above {
                left: Operand::Close,
                right: Operand::Constant(100.0),
            }),
            count: 3,
        };
        assert!(evaluate(&rule, &ohlcv, &HashMap::new(), 3));
    }

    #[test]
    fn evaluate_any_of_false() {
        let ohlcv = make_ohlcv(vec![
            make_bar(1, 100.0, 110.0, 90.0, 95.0, 1000),
            make_bar(2, 100.0, 110.0, 90.0, 95.0, 1000),
            make_bar(3, 100.0, 110.0, 90.0, 95.0, 1000),
            make_bar(4, 100.0, 110.0, 90.0, 95.0, 1000),
        ]);
        let rule = Rule::AnyOf {
            rule: Box::new(Rule::Above {
                left: Operand::Close,
                right: Operand::Constant(100.0),
            }),
            count: 3,
        };
        assert!(!evaluate(&rule, &ohlcv, &HashMap::new(), 3));
    }

    #[test]
    fn evaluate_with_indicator() {
        let ohlcv = make_ohlcv(vec![
            make_bar(1, 100.0, 110.0, 90.0, 100.0, 1000),
            make_bar(2, 100.0, 110.0, 90.0, 101.0, 1000),
            make_bar(3, 100.0, 110.0, 90.0, 102.0, 1000),
        ]);

        let sma_series = make_simple_indicator(
            IndicatorType::Sma(2),
            vec![(1, false, 0.0), (2, true, 100.5), (3, true, 101.5)],
        );

        let mut indicators = HashMap::new();
        indicators.insert(IndicatorType::Sma(2), sma_series);

        let rule = Rule::Above {
            left: Operand::Indicator(IndicatorRef {
                indicator_type: IndicatorType::Sma(2),
                field: IndicatorField::Value,
            }),
            right: Operand::Constant(100.0),
        };

        assert!(!evaluate(&rule, &ohlcv, &indicators, 0));
        assert!(evaluate(&rule, &ohlcv, &indicators, 1));
        assert!(evaluate(&rule, &ohlcv, &indicators, 2));
    }

    #[test]
    fn evaluate_cross_above_with_indicators() {
        let ohlcv = make_ohlcv(vec![
            make_bar(1, 100.0, 110.0, 90.0, 100.0, 1000),
            make_bar(2, 100.0, 110.0, 90.0, 101.0, 1000),
            make_bar(3, 100.0, 110.0, 90.0, 102.0, 1000),
            make_bar(4, 100.0, 110.0, 90.0, 103.0, 1000),
        ]);

        let sma10 = make_simple_indicator(
            IndicatorType::Sma(10),
            vec![
                (1, false, 0.0),
                (2, false, 0.0),
                (3, true, 99.0),
                (4, true, 102.0),
            ],
        );

        let sma20 = make_simple_indicator(
            IndicatorType::Sma(20),
            vec![
                (1, false, 0.0),
                (2, false, 0.0),
                (3, true, 100.0),
                (4, true, 101.0),
            ],
        );

        let mut indicators = HashMap::new();
        indicators.insert(IndicatorType::Sma(10), sma10);
        indicators.insert(IndicatorType::Sma(20), sma20);

        let rule = Rule::CrossAbove {
            left: Operand::Indicator(IndicatorRef {
                indicator_type: IndicatorType::Sma(10),
                field: IndicatorField::Value,
            }),
            right: Operand::Indicator(IndicatorRef {
                indicator_type: IndicatorType::Sma(20),
                field: IndicatorField::Value,
            }),
        };

        assert!(evaluate(&rule, &ohlcv, &indicators, 3));
        assert!(!evaluate(&rule, &ohlcv, &indicators, 2));
    }

    #[test]
    fn evaluate_operand_volume() {
        let ohlcv = make_ohlcv(vec![make_bar(1, 100.0, 110.0, 90.0, 105.0, 5000)]);
        let rule = Rule::Above {
            left: Operand::Volume,
            right: Operand::Constant(4000.0),
        };
        assert!(evaluate(&rule, &ohlcv, &HashMap::new(), 0));
    }

    #[test]
    fn evaluate_nested_composite() {
        let ohlcv = make_ohlcv(vec![make_bar(1, 100.0, 110.0, 90.0, 105.0, 1000)]);

        let rule = Rule::And(vec![
            Rule::Or(vec![
                Rule::Above {
                    left: Operand::Close,
                    right: Operand::Constant(200.0),
                },
                Rule::Above {
                    left: Operand::Close,
                    right: Operand::Constant(100.0),
                },
            ]),
            Rule::Not(Box::new(Rule::Below {
                left: Operand::Close,
                right: Operand::Constant(100.0),
            })),
        ]);

        assert!(evaluate(&rule, &ohlcv, &HashMap::new(), 0));
    }

    #[test]
    fn evaluate_indicator_missing() {
        let ohlcv = make_ohlcv(vec![make_bar(1, 100.0, 110.0, 90.0, 105.0, 1000)]);

        let rule = Rule::Above {
            left: Operand::Indicator(IndicatorRef {
                indicator_type: IndicatorType::Sma(20),
                field: IndicatorField::Value,
            }),
            right: Operand::Constant(100.0),
        };

        assert!(!evaluate(&rule, &ohlcv, &HashMap::new(), 0));
    }

    #[test]
    fn evaluate_indicator_invalid() {
        let ohlcv = make_ohlcv(vec![make_bar(1, 100.0, 110.0, 90.0, 105.0, 1000)]);

        let sma_series = make_simple_indicator(IndicatorType::Sma(20), vec![(1, false, 0.0)]);

        let mut indicators = HashMap::new();
        indicators.insert(IndicatorType::Sma(20), sma_series);

        let rule = Rule::Above {
            left: Operand::Indicator(IndicatorRef {
                indicator_type: IndicatorType::Sma(20),
                field: IndicatorField::Value,
            }),
            right: Operand::Constant(100.0),
        };

        assert!(!evaluate(&rule, &ohlcv, &indicators, 0));
    }
}
