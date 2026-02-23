//! Rule AST data structures (TRD Section 5.4).
//!
//! This module defines the abstract syntax tree for trading rules:
//! - `Operand`: What can be compared (price fields, constants, indicators)
//! - `IndicatorRef`: Reference to an indicator with a specific field
//! - `IndicatorField`: Which field of a multi-value indicator to use
//! - `Rule`: The rule AST with comparison, composite, and temporal variants

use crate::domain::indicator::IndicatorType;
use std::collections::HashSet;

#[derive(Debug, Clone, PartialEq)]
pub enum Operand {
    Open,
    High,
    Low,
    Close,
    Volume,
    Constant(f64),
    Indicator(IndicatorRef),
}

#[derive(Debug, Clone, PartialEq)]
pub struct IndicatorRef {
    pub indicator_type: IndicatorType,
    pub field: IndicatorField,
}

#[derive(Debug, Clone, Copy, PartialEq, Eq, Hash)]
pub enum IndicatorField {
    /// For single-value indicators (SMA, EMA, WMA, RSI, ROC, ATR, STDDEV, OBV, VWAP)
    Value,
    /// MACD fields
    MacdLine,
    MacdSignal,
    MacdHistogram,
    /// Stochastic fields
    StochasticK,
    StochasticD,
    /// Bollinger Bands fields
    BollingerUpper,
    BollingerMiddle,
    BollingerLower,
    /// Pivot Point fields
    Pivot,
    R1,
    R2,
    R3,
    S1,
    S2,
    S3,
}

#[derive(Debug, Clone, PartialEq)]
pub enum Rule {
    CrossAbove {
        left: Operand,
        right: Operand,
    },
    CrossBelow {
        left: Operand,
        right: Operand,
    },
    Above {
        left: Operand,
        right: Operand,
    },
    Below {
        left: Operand,
        right: Operand,
    },
    Between {
        operand: Operand,
        lower: f64,
        upper: f64,
    },
    Equals {
        left: Operand,
        right: Operand,
    },
    And(Vec<Rule>),
    Or(Vec<Rule>),
    Not(Box<Rule>),
    Consecutive {
        rule: Box<Rule>,
        count: usize,
    },
    AnyOf {
        rule: Box<Rule>,
        count: usize,
    },
}

pub fn extract_indicators(rule: &Rule) -> HashSet<IndicatorType> {
    let mut indicators = HashSet::new();
    collect_indicators_from_rule(rule, &mut indicators);
    indicators
}

fn collect_indicators_from_rule(rule: &Rule, indicators: &mut HashSet<IndicatorType>) {
    match rule {
        Rule::CrossAbove { left, right }
        | Rule::CrossBelow { left, right }
        | Rule::Above { left, right }
        | Rule::Below { left, right }
        | Rule::Equals { left, right } => {
            collect_indicators_from_operand(left, indicators);
            collect_indicators_from_operand(right, indicators);
        }
        Rule::Between { operand, .. } => {
            collect_indicators_from_operand(operand, indicators);
        }
        Rule::And(rules) | Rule::Or(rules) => {
            for r in rules {
                collect_indicators_from_rule(r, indicators);
            }
        }
        Rule::Not(inner) => {
            collect_indicators_from_rule(inner, indicators);
        }
        Rule::Consecutive { rule: inner, .. } | Rule::AnyOf { rule: inner, .. } => {
            collect_indicators_from_rule(inner, indicators);
        }
    }
}

fn collect_indicators_from_operand(operand: &Operand, indicators: &mut HashSet<IndicatorType>) {
    if let Operand::Indicator(ind_ref) = operand {
        indicators.insert(ind_ref.indicator_type.clone());
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn operand_price_fields() {
        assert_eq!(Operand::Open, Operand::Open);
        assert_eq!(Operand::High, Operand::High);
        assert_eq!(Operand::Low, Operand::Low);
        assert_eq!(Operand::Close, Operand::Close);
        assert_eq!(Operand::Volume, Operand::Volume);
    }

    #[test]
    fn operand_constant() {
        let c = Operand::Constant(100.5);
        assert_eq!(c, Operand::Constant(100.5));
        assert_ne!(c, Operand::Constant(99.0));
    }

    #[test]
    fn operand_indicator() {
        let ind = Operand::Indicator(IndicatorRef {
            indicator_type: IndicatorType::Sma(20),
            field: IndicatorField::Value,
        });
        assert!(matches!(ind, Operand::Indicator(_)));
    }

    #[test]
    fn indicator_ref() {
        let iref = IndicatorRef {
            indicator_type: IndicatorType::Macd {
                fast: 12,
                slow: 26,
                signal: 9,
            },
            field: IndicatorField::MacdHistogram,
        };
        assert_eq!(iref.field, IndicatorField::MacdHistogram);
        assert!(matches!(iref.indicator_type, IndicatorType::Macd { .. }));
    }

    #[test]
    fn indicator_field_variants() {
        assert_eq!(IndicatorField::Value, IndicatorField::Value);
        assert_ne!(IndicatorField::MacdLine, IndicatorField::MacdSignal);
        assert_ne!(IndicatorField::R1, IndicatorField::R2);
    }

    #[test]
    fn rule_comparison_variants() {
        let cross_above = Rule::CrossAbove {
            left: Operand::Indicator(IndicatorRef {
                indicator_type: IndicatorType::Sma(20),
                field: IndicatorField::Value,
            }),
            right: Operand::Indicator(IndicatorRef {
                indicator_type: IndicatorType::Sma(50),
                field: IndicatorField::Value,
            }),
        };
        assert!(matches!(cross_above, Rule::CrossAbove { .. }));

        let above = Rule::Above {
            left: Operand::Close,
            right: Operand::Constant(100.0),
        };
        assert!(matches!(above, Rule::Above { .. }));

        let between = Rule::Between {
            operand: Operand::Close,
            lower: 50.0,
            upper: 150.0,
        };
        assert!(matches!(between, Rule::Between { .. }));
    }

    #[test]
    fn rule_composite_variants() {
        let and_rule = Rule::And(vec![
            Rule::Above {
                left: Operand::Close,
                right: Operand::Constant(100.0),
            },
            Rule::Below {
                left: Operand::Close,
                right: Operand::Constant(150.0),
            },
        ]);
        assert!(matches!(and_rule, Rule::And(_)));

        let or_rule = Rule::Or(vec![
            Rule::Above {
                left: Operand::Close,
                right: Operand::Constant(100.0),
            },
            Rule::Below {
                left: Operand::Close,
                right: Operand::Constant(50.0),
            },
        ]);
        assert!(matches!(or_rule, Rule::Or(_)));

        let not_rule = Rule::Not(Box::new(Rule::Above {
            left: Operand::Close,
            right: Operand::Constant(100.0),
        }));
        assert!(matches!(not_rule, Rule::Not(_)));
    }

    #[test]
    fn rule_temporal_variants() {
        let consecutive = Rule::Consecutive {
            rule: Box::new(Rule::Above {
                left: Operand::Close,
                right: Operand::Constant(100.0),
            }),
            count: 3,
        };
        assert!(matches!(consecutive, Rule::Consecutive { .. }));

        let any_of = Rule::AnyOf {
            rule: Box::new(Rule::Above {
                left: Operand::Close,
                right: Operand::Constant(100.0),
            }),
            count: 5,
        };
        assert!(matches!(any_of, Rule::AnyOf { .. }));
    }

    #[test]
    fn nested_rules() {
        let nested = Rule::And(vec![
            Rule::Or(vec![
                Rule::Above {
                    left: Operand::Close,
                    right: Operand::Constant(100.0),
                },
                Rule::Below {
                    left: Operand::Close,
                    right: Operand::Constant(50.0),
                },
            ]),
            Rule::Not(Box::new(Rule::Equals {
                left: Operand::Volume,
                right: Operand::Constant(0.0),
            })),
        ]);
        assert!(matches!(nested, Rule::And(_)));
    }
}
