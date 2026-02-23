//! Rule DSL parser (TRD Section 5.3/5.5).
//!
//! Recursive descent parser for the rule grammar. Converts text to AST with
//! meaningful error messages including character offset, expected/found tokens.

use crate::domain::error::ParseError;
use crate::domain::indicator::IndicatorType;
use crate::domain::rule::{IndicatorField, IndicatorRef, Operand, Rule};

struct Parser<'a> {
    input: &'a str,
    pos: usize,
}

impl<'a> Parser<'a> {
    fn new(input: &'a str) -> Self {
        Self { input, pos: 0 }
    }

    fn remaining(&self) -> &'a str {
        &self.input[self.pos..]
    }

    fn peek(&self) -> Option<char> {
        self.remaining().chars().next()
    }

    fn advance(&mut self) -> Option<char> {
        let ch = self.peek()?;
        self.pos += ch.len_utf8();
        Some(ch)
    }

    fn skip_whitespace(&mut self) {
        while let Some(ch) = self.peek() {
            if ch.is_whitespace() {
                self.advance();
            } else {
                break;
            }
        }
    }

    fn expect_char(&mut self, expected: char) -> Result<(), ParseError> {
        self.skip_whitespace();
        match self.peek() {
            Some(ch) if ch == expected => {
                self.advance();
                Ok(())
            }
            Some(ch) => Err(ParseError {
                message: format!("expected '{}', found '{}'", expected, ch),
                position: self.pos,
            }),
            None => Err(ParseError {
                message: format!("expected '{}', found end of input", expected),
                position: self.pos,
            }),
        }
    }

    fn peek_keyword(&self, keyword: &str) -> bool {
        let remaining = self.remaining();
        remaining.starts_with(keyword)
            && (remaining.len() == keyword.len()
                || !remaining[keyword.len()..]
                    .chars()
                    .next()
                    .map(|c| c.is_alphanumeric() || c == '_')
                    .unwrap_or(false))
    }

    fn consume_keyword(&mut self, keyword: &str) -> bool {
        if self.peek_keyword(keyword) {
            self.pos += keyword.len();
            true
        } else {
            false
        }
    }

    fn expect_keyword(&mut self, keyword: &str) -> Result<(), ParseError> {
        if self.consume_keyword(keyword) {
            Ok(())
        } else {
            let found = self.peek_word();
            Err(ParseError {
                message: format!("expected '{}', found '{}'", keyword, found),
                position: self.pos,
            })
        }
    }

    fn peek_word(&self) -> String {
        let mut word = String::new();
        for ch in self.remaining().chars() {
            if ch.is_alphanumeric() || ch == '_' {
                word.push(ch);
            } else {
                break;
            }
        }
        if word.is_empty() {
            self.peek()
                .map(|c| c.to_string())
                .unwrap_or_else(|| "end of input".to_string())
        } else {
            word
        }
    }

    fn parse_number(&mut self) -> Result<f64, ParseError> {
        self.skip_whitespace();
        let start = self.pos;
        let mut has_dot = false;
        let mut digits = 0;

        if self.peek() == Some('-') {
            self.advance();
        }

        while let Some(ch) = self.peek() {
            if ch.is_ascii_digit() {
                digits += 1;
                self.advance();
            } else if ch == '.' && !has_dot {
                has_dot = true;
                self.advance();
            } else {
                break;
            }
        }

        if digits == 0 {
            return Err(ParseError {
                message: "expected number".to_string(),
                position: start,
            });
        }

        let num_str = &self.input[start..self.pos];
        num_str.parse::<f64>().map_err(|_| ParseError {
            message: format!("invalid number: {}", num_str),
            position: start,
        })
    }

    fn parse_integer(&mut self) -> Result<usize, ParseError> {
        self.skip_whitespace();
        let start = self.pos;
        let mut digits = 0;

        while let Some(ch) = self.peek() {
            if ch.is_ascii_digit() {
                digits += 1;
                self.advance();
            } else {
                break;
            }
        }

        if digits == 0 {
            return Err(ParseError {
                message: "expected integer".to_string(),
                position: start,
            });
        }

        let num_str = &self.input[start..self.pos];
        num_str.parse::<usize>().map_err(|_| ParseError {
            message: format!("invalid integer: {}", num_str),
            position: start,
        })
    }

    fn parse_price_field(&mut self) -> Result<Operand, ParseError> {
        self.skip_whitespace();
        let word = self.peek_word();
        let operand = match word.as_str() {
            "open" => Operand::Open,
            "high" => Operand::High,
            "low" => Operand::Low,
            "close" => Operand::Close,
            "volume" => Operand::Volume,
            _ => {
                return Err(ParseError {
                    message: format!(
                        "expected price field (open, high, low, close, volume), found '{}'",
                        word
                    ),
                    position: self.pos,
                });
            }
        };
        self.pos += word.len();
        Ok(operand)
    }

    fn consume_exact(&mut self, s: &str) -> bool {
        if self.remaining().starts_with(s) {
            self.pos += s.len();
            true
        } else {
            false
        }
    }

    fn parse_indicator(&mut self) -> Result<Operand, ParseError> {
        self.skip_whitespace();

        if self.consume_exact("SMA(") {
            let period = self.parse_integer()?;
            self.expect_char(')')?;
            return Ok(Operand::Indicator(IndicatorRef {
                indicator_type: IndicatorType::Sma(period),
                field: IndicatorField::Value,
            }));
        }

        if self.consume_exact("EMA(") {
            let period = self.parse_integer()?;
            self.expect_char(')')?;
            return Ok(Operand::Indicator(IndicatorRef {
                indicator_type: IndicatorType::Ema(period),
                field: IndicatorField::Value,
            }));
        }

        if self.consume_exact("WMA(") {
            let period = self.parse_integer()?;
            self.expect_char(')')?;
            return Ok(Operand::Indicator(IndicatorRef {
                indicator_type: IndicatorType::Wma(period),
                field: IndicatorField::Value,
            }));
        }

        if self.consume_exact("RSI(") {
            let period = self.parse_integer()?;
            self.expect_char(')')?;
            return Ok(Operand::Indicator(IndicatorRef {
                indicator_type: IndicatorType::Rsi(period),
                field: IndicatorField::Value,
            }));
        }

        if self.consume_exact("ROC(") {
            let period = self.parse_integer()?;
            self.expect_char(')')?;
            return Ok(Operand::Indicator(IndicatorRef {
                indicator_type: IndicatorType::Roc(period),
                field: IndicatorField::Value,
            }));
        }

        if self.consume_exact("ATR(") {
            let period = self.parse_integer()?;
            self.expect_char(')')?;
            return Ok(Operand::Indicator(IndicatorRef {
                indicator_type: IndicatorType::Atr(period),
                field: IndicatorField::Value,
            }));
        }

        if self.consume_exact("STDDEV(") {
            let period = self.parse_integer()?;
            self.expect_char(')')?;
            return Ok(Operand::Indicator(IndicatorRef {
                indicator_type: IndicatorType::Stddev(period),
                field: IndicatorField::Value,
            }));
        }

        if self.consume_keyword("OBV") {
            return Ok(Operand::Indicator(IndicatorRef {
                indicator_type: IndicatorType::Obv,
                field: IndicatorField::Value,
            }));
        }

        if self.consume_keyword("VWAP") {
            return Ok(Operand::Indicator(IndicatorRef {
                indicator_type: IndicatorType::Vwap,
                field: IndicatorField::Value,
            }));
        }

        if self.consume_exact("MACD_LINE(") {
            let fast = self.parse_integer()?;
            self.expect_char(',')?;
            let slow = self.parse_integer()?;
            self.expect_char(',')?;
            let signal = self.parse_integer()?;
            self.expect_char(')')?;
            return Ok(Operand::Indicator(IndicatorRef {
                indicator_type: IndicatorType::Macd { fast, slow, signal },
                field: IndicatorField::MacdLine,
            }));
        }

        if self.consume_exact("MACD_SIGNAL(") {
            let fast = self.parse_integer()?;
            self.expect_char(',')?;
            let slow = self.parse_integer()?;
            self.expect_char(',')?;
            let signal = self.parse_integer()?;
            self.expect_char(')')?;
            return Ok(Operand::Indicator(IndicatorRef {
                indicator_type: IndicatorType::Macd { fast, slow, signal },
                field: IndicatorField::MacdSignal,
            }));
        }

        if self.consume_exact("MACD_HISTOGRAM(") {
            let fast = self.parse_integer()?;
            self.expect_char(',')?;
            let slow = self.parse_integer()?;
            self.expect_char(',')?;
            let signal = self.parse_integer()?;
            self.expect_char(')')?;
            return Ok(Operand::Indicator(IndicatorRef {
                indicator_type: IndicatorType::Macd { fast, slow, signal },
                field: IndicatorField::MacdHistogram,
            }));
        }

        if self.consume_exact("STOCHASTIC_K(") {
            let k_period = self.parse_integer()?;
            self.expect_char(',')?;
            let d_period = self.parse_integer()?;
            self.expect_char(')')?;
            return Ok(Operand::Indicator(IndicatorRef {
                indicator_type: IndicatorType::Stochastic { k_period, d_period },
                field: IndicatorField::StochasticK,
            }));
        }

        if self.consume_exact("STOCHASTIC_D(") {
            let k_period = self.parse_integer()?;
            self.expect_char(',')?;
            let d_period = self.parse_integer()?;
            self.expect_char(')')?;
            return Ok(Operand::Indicator(IndicatorRef {
                indicator_type: IndicatorType::Stochastic { k_period, d_period },
                field: IndicatorField::StochasticD,
            }));
        }

        if self.consume_exact("BOLLINGER_UPPER(") {
            let period = self.parse_integer()?;
            self.expect_char(',')?;
            let mult = self.parse_number()?;
            self.expect_char(')')?;
            return Ok(Operand::Indicator(IndicatorRef {
                indicator_type: IndicatorType::Bollinger {
                    period,
                    stddev_mult_x100: (mult * 100.0).round() as u32,
                },
                field: IndicatorField::BollingerUpper,
            }));
        }

        if self.consume_exact("BOLLINGER_MIDDLE(") {
            let period = self.parse_integer()?;
            self.expect_char(',')?;
            let mult = self.parse_number()?;
            self.expect_char(')')?;
            return Ok(Operand::Indicator(IndicatorRef {
                indicator_type: IndicatorType::Bollinger {
                    period,
                    stddev_mult_x100: (mult * 100.0).round() as u32,
                },
                field: IndicatorField::BollingerMiddle,
            }));
        }

        if self.consume_exact("BOLLINGER_LOWER(") {
            let period = self.parse_integer()?;
            self.expect_char(',')?;
            let mult = self.parse_number()?;
            self.expect_char(')')?;
            return Ok(Operand::Indicator(IndicatorRef {
                indicator_type: IndicatorType::Bollinger {
                    period,
                    stddev_mult_x100: (mult * 100.0).round() as u32,
                },
                field: IndicatorField::BollingerLower,
            }));
        }

        if self.consume_keyword("PIVOT_R3") {
            return Ok(Operand::Indicator(IndicatorRef {
                indicator_type: IndicatorType::Pivot,
                field: IndicatorField::R3,
            }));
        }

        if self.consume_keyword("PIVOT_R2") {
            return Ok(Operand::Indicator(IndicatorRef {
                indicator_type: IndicatorType::Pivot,
                field: IndicatorField::R2,
            }));
        }

        if self.consume_keyword("PIVOT_R1") {
            return Ok(Operand::Indicator(IndicatorRef {
                indicator_type: IndicatorType::Pivot,
                field: IndicatorField::R1,
            }));
        }

        if self.consume_keyword("PIVOT_S3") {
            return Ok(Operand::Indicator(IndicatorRef {
                indicator_type: IndicatorType::Pivot,
                field: IndicatorField::S3,
            }));
        }

        if self.consume_keyword("PIVOT_S2") {
            return Ok(Operand::Indicator(IndicatorRef {
                indicator_type: IndicatorType::Pivot,
                field: IndicatorField::S2,
            }));
        }

        if self.consume_keyword("PIVOT_S1") {
            return Ok(Operand::Indicator(IndicatorRef {
                indicator_type: IndicatorType::Pivot,
                field: IndicatorField::S1,
            }));
        }

        if self.consume_keyword("PIVOT") {
            return Ok(Operand::Indicator(IndicatorRef {
                indicator_type: IndicatorType::Pivot,
                field: IndicatorField::Pivot,
            }));
        }

        let word = self.peek_word();
        Err(ParseError {
            message: format!("expected indicator, found '{}'", word),
            position: self.pos,
        })
    }

    fn parse_operand(&mut self) -> Result<Operand, ParseError> {
        self.skip_whitespace();

        if self
            .peek()
            .is_some_and(|ch| ch.is_ascii_digit() || ch == '-' || ch == '.')
        {
            let num = self.parse_number()?;
            return Ok(Operand::Constant(num));
        }

        let word = self.peek_word();
        match word.as_str() {
            "open" | "high" | "low" | "close" | "volume" => self.parse_price_field(),
            _ => self.parse_indicator(),
        }
    }

    fn parse_comparison(&mut self, keyword: &str) -> Result<Rule, ParseError> {
        self.expect_keyword(keyword)?;
        self.expect_char('(')?;

        let left = self.parse_operand()?;
        self.expect_char(',')?;
        let right = self.parse_operand()?;
        self.expect_char(')')?;

        match keyword {
            "CROSS_ABOVE" => Ok(Rule::CrossAbove { left, right }),
            "CROSS_BELOW" => Ok(Rule::CrossBelow { left, right }),
            "ABOVE" => Ok(Rule::Above { left, right }),
            "BELOW" => Ok(Rule::Below { left, right }),
            "EQUALS" => Ok(Rule::Equals { left, right }),
            _ => unreachable!(),
        }
    }

    fn parse_between(&mut self) -> Result<Rule, ParseError> {
        self.expect_keyword("BETWEEN")?;
        self.expect_char('(')?;

        let operand = self.parse_operand()?;
        self.expect_char(',')?;
        let lower = self.parse_number()?;
        self.expect_char(',')?;
        let upper = self.parse_number()?;
        self.expect_char(')')?;

        Ok(Rule::Between {
            operand,
            lower,
            upper,
        })
    }

    fn parse_rule(&mut self) -> Result<Rule, ParseError> {
        self.skip_whitespace();

        if self.peek_keyword("CROSS_ABOVE") {
            return self.parse_comparison("CROSS_ABOVE");
        }
        if self.peek_keyword("CROSS_BELOW") {
            return self.parse_comparison("CROSS_BELOW");
        }
        if self.peek_keyword("ABOVE") {
            return self.parse_comparison("ABOVE");
        }
        if self.peek_keyword("BELOW") {
            return self.parse_comparison("BELOW");
        }
        if self.peek_keyword("EQUALS") {
            return self.parse_comparison("EQUALS");
        }
        if self.peek_keyword("BETWEEN") {
            return self.parse_between();
        }

        if self.peek_keyword("AND") {
            return self.parse_and();
        }
        if self.peek_keyword("OR") {
            return self.parse_or();
        }
        if self.peek_keyword("NOT") {
            return self.parse_not();
        }

        if self.peek_keyword("CONSECUTIVE") {
            return self.parse_consecutive();
        }
        if self.peek_keyword("ANY_OF") {
            return self.parse_any_of();
        }

        let word = self.peek_word();
        Err(ParseError {
            message: format!("expected rule, found '{}'", word),
            position: self.pos,
        })
    }

    fn parse_and(&mut self) -> Result<Rule, ParseError> {
        self.expect_keyword("AND")?;
        self.expect_char('(')?;

        let mut rules = Vec::new();
        rules.push(self.parse_rule()?);

        loop {
            self.skip_whitespace();
            if self.peek() == Some(')') {
                self.advance();
                break;
            }
            self.expect_char(',')?;
            rules.push(self.parse_rule()?);
        }

        if rules.len() < 2 {
            return Err(ParseError {
                message: "AND requires at least 2 rules".to_string(),
                position: self.pos,
            });
        }

        Ok(Rule::And(rules))
    }

    fn parse_or(&mut self) -> Result<Rule, ParseError> {
        self.expect_keyword("OR")?;
        self.expect_char('(')?;

        let mut rules = Vec::new();
        rules.push(self.parse_rule()?);

        loop {
            self.skip_whitespace();
            if self.peek() == Some(')') {
                self.advance();
                break;
            }
            self.expect_char(',')?;
            rules.push(self.parse_rule()?);
        }

        if rules.len() < 2 {
            return Err(ParseError {
                message: "OR requires at least 2 rules".to_string(),
                position: self.pos,
            });
        }

        Ok(Rule::Or(rules))
    }

    fn parse_not(&mut self) -> Result<Rule, ParseError> {
        self.expect_keyword("NOT")?;
        self.expect_char('(')?;
        let rule = self.parse_rule()?;
        self.expect_char(')')?;
        Ok(Rule::Not(Box::new(rule)))
    }

    fn parse_consecutive(&mut self) -> Result<Rule, ParseError> {
        self.expect_keyword("CONSECUTIVE")?;
        self.expect_char('(')?;
        let rule = self.parse_rule()?;
        self.expect_char(',')?;
        let count = self.parse_integer()?;
        self.expect_char(')')?;
        Ok(Rule::Consecutive {
            rule: Box::new(rule),
            count,
        })
    }

    fn parse_any_of(&mut self) -> Result<Rule, ParseError> {
        self.expect_keyword("ANY_OF")?;
        self.expect_char('(')?;
        let rule = self.parse_rule()?;
        self.expect_char(',')?;
        let count = self.parse_integer()?;
        self.expect_char(')')?;
        Ok(Rule::AnyOf {
            rule: Box::new(rule),
            count,
        })
    }

    fn parse(&mut self) -> Result<Rule, ParseError> {
        let rule = self.parse_rule()?;
        self.skip_whitespace();
        if self.pos < self.input.len() {
            return Err(ParseError {
                message: format!("unexpected input after rule: '{}'", self.remaining()),
                position: self.pos,
            });
        }
        Ok(rule)
    }
}

pub fn parse(input: &str) -> Result<Rule, ParseError> {
    let mut parser = Parser::new(input);
    parser.parse()
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn parse_above() {
        let rule = parse("ABOVE(close, 100)").unwrap();
        assert!(matches!(
            rule,
            Rule::Above {
                left: Operand::Close,
                right: Operand::Constant(100.0)
            }
        ));
    }

    #[test]
    fn parse_below_with_indicator() {
        let rule = parse("BELOW(SMA(20), SMA(50))").unwrap();
        match rule {
            Rule::Below { left, right } => {
                assert!(matches!(
                    left,
                    Operand::Indicator(IndicatorRef {
                        indicator_type: IndicatorType::Sma(20),
                        field: IndicatorField::Value
                    })
                ));
                assert!(matches!(
                    right,
                    Operand::Indicator(IndicatorRef {
                        indicator_type: IndicatorType::Sma(50),
                        field: IndicatorField::Value
                    })
                ));
            }
            _ => panic!("expected Below rule"),
        }
    }

    #[test]
    fn parse_cross_above() {
        let rule = parse("CROSS_ABOVE(SMA(20), SMA(50))").unwrap();
        assert!(matches!(rule, Rule::CrossAbove { .. }));
    }

    #[test]
    fn parse_cross_below() {
        let rule = parse("CROSS_BELOW(close, EMA(200))").unwrap();
        assert!(matches!(rule, Rule::CrossBelow { .. }));
    }

    #[test]
    fn parse_between() {
        let rule = parse("BETWEEN(close, 50, 150)").unwrap();
        match rule {
            Rule::Between {
                operand,
                lower,
                upper,
            } => {
                assert!(matches!(operand, Operand::Close));
                assert!((lower - 50.0).abs() < f64::EPSILON);
                assert!((upper - 150.0).abs() < f64::EPSILON);
            }
            _ => panic!("expected Between rule"),
        }
    }

    #[test]
    fn parse_equals() {
        let rule = parse("EQUALS(close, 100.5)").unwrap();
        assert!(matches!(rule, Rule::Equals { .. }));
    }

    #[test]
    fn parse_and() {
        let rule = parse("AND(ABOVE(close, 100), BELOW(close, 150))").unwrap();
        match rule {
            Rule::And(rules) => {
                assert_eq!(rules.len(), 2);
            }
            _ => panic!("expected And rule"),
        }
    }

    #[test]
    fn parse_or() {
        let rule = parse("OR(ABOVE(close, 100), BELOW(close, 50))").unwrap();
        match rule {
            Rule::Or(rules) => {
                assert_eq!(rules.len(), 2);
            }
            _ => panic!("expected Or rule"),
        }
    }

    #[test]
    fn parse_not() {
        let rule = parse("NOT(ABOVE(close, 100))").unwrap();
        assert!(matches!(rule, Rule::Not(_)));
    }

    #[test]
    fn parse_consecutive() {
        let rule = parse("CONSECUTIVE(ABOVE(close, 100), 3)").unwrap();
        match rule {
            Rule::Consecutive { count, .. } => {
                assert_eq!(count, 3);
            }
            _ => panic!("expected Consecutive rule"),
        }
    }

    #[test]
    fn parse_any_of() {
        let rule = parse("ANY_OF(ABOVE(close, 100), 5)").unwrap();
        match rule {
            Rule::AnyOf { count, .. } => {
                assert_eq!(count, 5);
            }
            _ => panic!("expected AnyOf rule"),
        }
    }

    #[test]
    fn parse_nested_and_or() {
        let rule =
            parse("AND(OR(ABOVE(close, 100), BELOW(close, 50)), NOT(EQUALS(volume, 0)))").unwrap();
        assert!(matches!(rule, Rule::And(_)));
    }

    #[test]
    fn parse_whitespace_handling() {
        let rule = parse("  ABOVE  (  close  ,  100  )  ").unwrap();
        assert!(matches!(rule, Rule::Above { .. }));
    }

    #[test]
    fn parse_price_fields() {
        for (input, expected) in [
            ("ABOVE(open, 100)", Operand::Open),
            ("ABOVE(high, 100)", Operand::High),
            ("ABOVE(low, 100)", Operand::Low),
            ("ABOVE(close, 100)", Operand::Close),
            ("ABOVE(volume, 100)", Operand::Volume),
        ] {
            let rule = parse(input).unwrap();
            match rule {
                Rule::Above { left, .. } => assert_eq!(left, expected),
                _ => panic!("expected Above rule"),
            }
        }
    }

    #[test]
    fn parse_all_indicators() {
        parse("ABOVE(SMA(20), 100)").unwrap();
        parse("ABOVE(EMA(20), 100)").unwrap();
        parse("ABOVE(WMA(20), 100)").unwrap();
        parse("ABOVE(RSI(14), 50)").unwrap();
        parse("ABOVE(ROC(10), 0)").unwrap();
        parse("ABOVE(ATR(14), 1)").unwrap();
        parse("ABOVE(STDDEV(20), 2)").unwrap();
        parse("ABOVE(OBV, 0)").unwrap();
        parse("ABOVE(VWAP, 100)").unwrap();
        parse("ABOVE(MACD_LINE(12,26,9), 0)").unwrap();
        parse("ABOVE(MACD_SIGNAL(12,26,9), 0)").unwrap();
        parse("ABOVE(MACD_HISTOGRAM(12,26,9), 0)").unwrap();
        parse("ABOVE(STOCHASTIC_K(14,3), 50)").unwrap();
        parse("ABOVE(STOCHASTIC_D(14,3), 50)").unwrap();
        parse("ABOVE(BOLLINGER_UPPER(20,2), 100)").unwrap();
        parse("ABOVE(BOLLINGER_MIDDLE(20,2), 100)").unwrap();
        parse("ABOVE(BOLLINGER_LOWER(20,2), 100)").unwrap();
        parse("ABOVE(PIVOT, 100)").unwrap();
        parse("ABOVE(PIVOT_R1, 100)").unwrap();
        parse("ABOVE(PIVOT_R2, 100)").unwrap();
        parse("ABOVE(PIVOT_R3, 100)").unwrap();
        parse("ABOVE(PIVOT_S1, 100)").unwrap();
        parse("ABOVE(PIVOT_S2, 100)").unwrap();
        parse("ABOVE(PIVOT_S3, 100)").unwrap();
    }

    #[test]
    fn parse_negative_numbers() {
        let rule = parse("ABOVE(close, -100.5)").unwrap();
        match rule {
            Rule::Above {
                right: Operand::Constant(v),
                ..
            } => {
                assert!((v - (-100.5)).abs() < f64::EPSILON);
            }
            _ => panic!("expected Above rule"),
        }
    }

    #[test]
    fn parse_float_numbers() {
        let rule = parse("BETWEEN(close, 10.5, 99.99)").unwrap();
        match rule {
            Rule::Between { lower, upper, .. } => {
                assert!((lower - 10.5).abs() < f64::EPSILON);
                assert!((upper - 99.99).abs() < f64::EPSILON);
            }
            _ => panic!("expected Between rule"),
        }
    }

    #[test]
    fn error_unexpected_token() {
        let err = parse("ABOVE(close, )").unwrap_err();
        assert!(err.message.contains("expected"));
        assert_eq!(err.position, 13);
    }

    #[test]
    fn error_missing_paren() {
        let err = parse("ABOVE(close, 100").unwrap_err();
        assert!(err.message.contains("expected ')'"));
    }

    #[test]
    fn error_invalid_rule() {
        let err = parse("INVALID(close, 100)").unwrap_err();
        assert!(err.message.contains("expected rule"));
    }

    #[test]
    fn error_trailing_input() {
        let err = parse("ABOVE(close, 100) garbage").unwrap_err();
        assert!(err.message.contains("unexpected input"));
    }

    #[test]
    fn error_missing_comma() {
        let err = parse("ABOVE(close 100)").unwrap_err();
        assert!(err.message.contains("expected ','"));
    }

    #[test]
    fn error_empty_and() {
        let err = parse("AND(ABOVE(close, 100))").unwrap_err();
        assert!(err.message.contains("AND requires at least 2 rules"));
    }

    #[test]
    fn error_empty_or() {
        let err = parse("OR(ABOVE(close, 100))").unwrap_err();
        assert!(err.message.contains("OR requires at least 2 rules"));
    }

    #[test]
    fn error_display_with_context() {
        let err = parse("CROSS_ABOVE(SMA(20), , SMA(50))").unwrap_err();
        let ctx = err.display_with_context("CROSS_ABOVE(SMA(20), , SMA(50))");
        assert!(ctx.contains("^"));
        assert!(ctx.contains("position"));
    }

    #[test]
    fn parse_variadic_and() {
        let rule = parse("AND(ABOVE(close, 100), BELOW(close, 150), ABOVE(volume, 0))").unwrap();
        match rule {
            Rule::And(rules) => {
                assert_eq!(rules.len(), 3);
            }
            _ => panic!("expected And rule"),
        }
    }

    #[test]
    fn parse_variadic_or() {
        let rule = parse("OR(ABOVE(close, 150), BELOW(close, 50), EQUALS(close, 100))").unwrap();
        match rule {
            Rule::Or(rules) => {
                assert_eq!(rules.len(), 3);
            }
            _ => panic!("expected Or rule"),
        }
    }

    #[test]
    fn parse_deeply_nested() {
        let rule = parse("NOT(AND(OR(ABOVE(close, 100), BELOW(close, 50)), CONSECUTIVE(ABOVE(volume, 1000), 3)))").unwrap();
        assert!(matches!(rule, Rule::Not(_)));
    }

    #[test]
    fn case_sensitive_keywords() {
        let err = parse("above(close, 100)").unwrap_err();
        assert!(err.message.contains("expected rule"));
    }

    #[test]
    fn parse_bollinger_with_float_multiplier() {
        let rule = parse("ABOVE(BOLLINGER_UPPER(20, 2.5), 100)").unwrap();
        match rule {
            Rule::Above {
                left: Operand::Indicator(ind_ref),
                ..
            } => match ind_ref.indicator_type {
                IndicatorType::Bollinger {
                    period,
                    stddev_mult_x100,
                } => {
                    assert_eq!(period, 20);
                    assert_eq!(stddev_mult_x100, 250);
                }
                _ => panic!("expected Bollinger indicator"),
            },
            _ => panic!("expected Above rule"),
        }
    }

    #[test]
    fn error_empty_input() {
        let err = parse("").unwrap_err();
        assert!(err.message.contains("expected rule"));
        assert_eq!(err.position, 0);
    }

    #[test]
    fn error_whitespace_only() {
        let err = parse("   ").unwrap_err();
        assert!(err.message.contains("expected rule"));
    }
}
