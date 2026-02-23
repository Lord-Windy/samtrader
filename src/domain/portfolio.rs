//! Portfolio state and equity tracking (TRD Section 3.4/3.5).

use chrono::NaiveDate;
use std::collections::HashMap;

use super::position::{ClosedTrade, Position};

#[derive(Debug, Clone, PartialEq)]
pub struct EquityPoint {
    pub date: NaiveDate,
    pub equity: f64,
}

#[derive(Debug, Clone, PartialEq)]
pub struct Portfolio {
    pub cash: f64,
    pub initial_capital: f64,
    pub positions: HashMap<String, Position>,
    pub closed_trades: Vec<ClosedTrade>,
    pub equity_curve: Vec<EquityPoint>,
}

impl Portfolio {
    pub fn new(initial_capital: f64) -> Self {
        Portfolio {
            cash: initial_capital,
            initial_capital,
            positions: HashMap::new(),
            closed_trades: Vec::new(),
            equity_curve: Vec::new(),
        }
    }

    pub fn add_position(&mut self, position: Position) {
        self.positions.insert(position.code.clone(), position);
    }

    pub fn get_position(&self, code: &str) -> Option<&Position> {
        self.positions.get(code)
    }

    pub fn has_position(&self, code: &str) -> bool {
        self.positions.contains_key(code)
    }

    pub fn remove_position(&mut self, code: &str) -> Option<Position> {
        self.positions.remove(code)
    }

    pub fn position_count(&self) -> usize {
        self.positions.len()
    }

    pub fn record_trade(&mut self, trade: ClosedTrade) {
        self.closed_trades.push(trade);
    }

    pub fn record_equity(&mut self, date: NaiveDate, equity: f64) {
        self.equity_curve.push(EquityPoint { date, equity });
    }

    pub fn total_equity(&self, price_map: &HashMap<String, f64>) -> f64 {
        let position_value: f64 = self
            .positions
            .values()
            .filter_map(|pos| {
                price_map
                    .get(&pos.code)
                    .map(|&price| pos.market_value(price))
            })
            .sum();
        self.cash + position_value
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    fn sample_position(code: &str, quantity: i64) -> Position {
        Position {
            code: code.to_string(),
            exchange: "ASX".to_string(),
            quantity,
            entry_price: 100.0,
            entry_date: NaiveDate::from_ymd_opt(2024, 1, 15).unwrap(),
            stop_loss: 0.0,
            take_profit: 0.0,
        }
    }

    #[test]
    fn new_portfolio() {
        let portfolio = Portfolio::new(100000.0);
        assert!((portfolio.cash - 100000.0).abs() < f64::EPSILON);
        assert!((portfolio.initial_capital - 100000.0).abs() < f64::EPSILON);
        assert!(portfolio.positions.is_empty());
        assert!(portfolio.closed_trades.is_empty());
        assert!(portfolio.equity_curve.is_empty());
    }

    #[test]
    fn add_and_get_position() {
        let mut portfolio = Portfolio::new(100000.0);
        let pos = sample_position("BHP", 100);
        portfolio.add_position(pos);

        assert!(portfolio.has_position("BHP"));
        let retrieved = portfolio.get_position("BHP");
        assert!(retrieved.is_some());
        assert_eq!(retrieved.unwrap().quantity, 100);
    }

    #[test]
    fn remove_position() {
        let mut portfolio = Portfolio::new(100000.0);
        portfolio.add_position(sample_position("BHP", 100));

        let removed = portfolio.remove_position("BHP");
        assert!(removed.is_some());
        assert!(!portfolio.has_position("BHP"));
    }

    #[test]
    fn remove_nonexistent_position() {
        let mut portfolio = Portfolio::new(100000.0);
        let removed = portfolio.remove_position("XYZ");
        assert!(removed.is_none());
    }

    #[test]
    fn position_count() {
        let mut portfolio = Portfolio::new(100000.0);
        assert_eq!(portfolio.position_count(), 0);

        portfolio.add_position(sample_position("BHP", 100));
        assert_eq!(portfolio.position_count(), 1);

        portfolio.add_position(sample_position("CBA", 50));
        assert_eq!(portfolio.position_count(), 2);

        portfolio.remove_position("BHP");
        assert_eq!(portfolio.position_count(), 1);
    }

    #[test]
    fn record_trade() {
        let mut portfolio = Portfolio::new(100000.0);
        let trade = ClosedTrade {
            code: "BHP".to_string(),
            exchange: "ASX".to_string(),
            quantity: 100,
            entry_price: 100.0,
            exit_price: 110.0,
            entry_date: NaiveDate::from_ymd_opt(2024, 1, 15).unwrap(),
            exit_date: NaiveDate::from_ymd_opt(2024, 1, 20).unwrap(),
            pnl: 950.0,
        };

        portfolio.record_trade(trade);
        assert_eq!(portfolio.closed_trades.len(), 1);
        assert_eq!(portfolio.closed_trades[0].code, "BHP");
    }

    #[test]
    fn record_equity() {
        let mut portfolio = Portfolio::new(100000.0);
        let date = NaiveDate::from_ymd_opt(2024, 1, 15).unwrap();

        portfolio.record_equity(date, 105000.0);
        assert_eq!(portfolio.equity_curve.len(), 1);
        assert_eq!(portfolio.equity_curve[0].date, date);
        assert!((portfolio.equity_curve[0].equity - 105000.0).abs() < f64::EPSILON);
    }

    #[test]
    fn total_equity_no_positions() {
        let portfolio = Portfolio::new(100000.0);
        let price_map = HashMap::new();
        let equity = portfolio.total_equity(&price_map);
        assert!((equity - 100000.0).abs() < f64::EPSILON);
    }

    #[test]
    fn total_equity_with_positions() {
        let mut portfolio = Portfolio::new(100000.0);
        portfolio.add_position(sample_position("BHP", 100));
        portfolio.cash = 89000.0;

        let mut price_map = HashMap::new();
        price_map.insert("BHP".to_string(), 110.0);

        let equity = portfolio.total_equity(&price_map);
        assert!((equity - 100000.0).abs() < f64::EPSILON);
    }

    #[test]
    fn total_equity_uses_market_value() {
        let mut portfolio = Portfolio::new(50000.0);
        let pos = Position {
            code: "BHP".to_string(),
            exchange: "ASX".to_string(),
            quantity: 100,
            entry_price: 100.0,
            entry_date: NaiveDate::from_ymd_opt(2024, 1, 15).unwrap(),
            stop_loss: 0.0,
            take_profit: 0.0,
        };
        portfolio.add_position(pos);
        portfolio.cash = 40000.0;

        let mut price_map = HashMap::new();
        price_map.insert("BHP".to_string(), 150.0);

        let equity = portfolio.total_equity(&price_map);
        assert!((equity - 55000.0).abs() < f64::EPSILON);
    }
}
