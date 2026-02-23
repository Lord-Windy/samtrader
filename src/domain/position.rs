//! Position tracking and management (TRD Section 3.2/3.3).

use chrono::NaiveDate;

#[derive(Debug, Clone)]
pub struct Position {
    pub code: String,
    pub exchange: String,
    pub quantity: i64,
    pub entry_price: f64,
    pub entry_date: NaiveDate,
    pub stop_loss: f64,
    pub take_profit: f64,
}

impl Position {
    pub fn is_long(&self) -> bool {
        self.quantity > 0
    }

    pub fn is_short(&self) -> bool {
        self.quantity < 0
    }

    pub fn market_value(&self, price: f64) -> f64 {
        self.quantity.unsigned_abs() as f64 * price
    }

    pub fn unrealized_pnl(&self, price: f64) -> f64 {
        self.quantity as f64 * (price - self.entry_price)
    }

    pub fn should_stop_loss(&self, price: f64) -> bool {
        if self.stop_loss == 0.0 {
            return false;
        }
        if self.is_long() {
            price <= self.stop_loss
        } else {
            price >= self.stop_loss
        }
    }

    pub fn should_take_profit(&self, price: f64) -> bool {
        if self.take_profit == 0.0 {
            return false;
        }
        if self.is_long() {
            price >= self.take_profit
        } else {
            price <= self.take_profit
        }
    }
}

#[derive(Debug, Clone)]
pub struct ClosedTrade {
    pub code: String,
    pub exchange: String,
    pub quantity: i64,
    pub entry_price: f64,
    pub exit_price: f64,
    pub entry_date: NaiveDate,
    pub exit_date: NaiveDate,
    pub pnl: f64,
}

#[cfg(test)]
mod tests {
    use super::*;

    fn sample_long_position() -> Position {
        Position {
            code: "BHP".into(),
            exchange: "ASX".into(),
            quantity: 100,
            entry_price: 50.0,
            entry_date: NaiveDate::from_ymd_opt(2024, 1, 15).unwrap(),
            stop_loss: 45.0,
            take_profit: 60.0,
        }
    }

    fn sample_short_position() -> Position {
        Position {
            code: "CBA".into(),
            exchange: "ASX".into(),
            quantity: -100,
            entry_price: 100.0,
            entry_date: NaiveDate::from_ymd_opt(2024, 1, 15).unwrap(),
            stop_loss: 110.0,
            take_profit: 80.0,
        }
    }

    #[test]
    fn is_long_positive_quantity() {
        let pos = sample_long_position();
        assert!(pos.is_long());
        assert!(!pos.is_short());
    }

    #[test]
    fn is_short_negative_quantity() {
        let pos = sample_short_position();
        assert!(pos.is_short());
        assert!(!pos.is_long());
    }

    #[test]
    fn market_value_long() {
        let pos = sample_long_position();
        assert!((pos.market_value(55.0) - 5500.0).abs() < f64::EPSILON);
    }

    #[test]
    fn market_value_short() {
        let pos = sample_short_position();
        assert!((pos.market_value(95.0) - 9500.0).abs() < f64::EPSILON);
    }

    #[test]
    fn unrealized_pnl_long_profit() {
        let pos = sample_long_position();
        let pnl = pos.unrealized_pnl(55.0);
        assert!((pnl - 500.0).abs() < f64::EPSILON);
    }

    #[test]
    fn unrealized_pnl_long_loss() {
        let pos = sample_long_position();
        let pnl = pos.unrealized_pnl(45.0);
        assert!((pnl - (-500.0)).abs() < f64::EPSILON);
    }

    #[test]
    fn unrealized_pnl_short_profit() {
        let pos = sample_short_position();
        let pnl = pos.unrealized_pnl(90.0);
        assert!((pnl - 1000.0).abs() < f64::EPSILON);
    }

    #[test]
    fn unrealized_pnl_short_loss() {
        let pos = sample_short_position();
        let pnl = pos.unrealized_pnl(110.0);
        assert!((pnl - (-1000.0)).abs() < f64::EPSILON);
    }

    #[test]
    fn stop_loss_long_triggered() {
        let pos = sample_long_position();
        assert!(pos.should_stop_loss(44.0));
        assert!(pos.should_stop_loss(45.0));
        assert!(!pos.should_stop_loss(46.0));
    }

    #[test]
    fn stop_loss_short_triggered() {
        let pos = sample_short_position();
        assert!(pos.should_stop_loss(111.0));
        assert!(pos.should_stop_loss(110.0));
        assert!(!pos.should_stop_loss(109.0));
    }

    #[test]
    fn stop_loss_disabled() {
        let mut pos = sample_long_position();
        pos.stop_loss = 0.0;
        assert!(!pos.should_stop_loss(0.0));
        assert!(!pos.should_stop_loss(1000000.0));
    }

    #[test]
    fn take_profit_long_triggered() {
        let pos = sample_long_position();
        assert!(pos.should_take_profit(61.0));
        assert!(pos.should_take_profit(60.0));
        assert!(!pos.should_take_profit(59.0));
    }

    #[test]
    fn take_profit_short_triggered() {
        let pos = sample_short_position();
        assert!(pos.should_take_profit(79.0));
        assert!(pos.should_take_profit(80.0));
        assert!(!pos.should_take_profit(81.0));
    }

    #[test]
    fn take_profit_disabled() {
        let mut pos = sample_long_position();
        pos.take_profit = 0.0;
        assert!(!pos.should_take_profit(0.0));
        assert!(!pos.should_take_profit(1000000.0));
    }

    #[test]
    fn closed_trade_fields() {
        let trade = ClosedTrade {
            code: "BHP".into(),
            exchange: "ASX".into(),
            quantity: 100,
            entry_price: 50.0,
            exit_price: 55.0,
            entry_date: NaiveDate::from_ymd_opt(2024, 1, 15).unwrap(),
            exit_date: NaiveDate::from_ymd_opt(2024, 1, 20).unwrap(),
            pnl: 485.0,
        };
        assert_eq!(trade.code, "BHP");
        assert_eq!(trade.exchange, "ASX");
        assert_eq!(trade.quantity, 100);
        assert!((trade.entry_price - 50.0).abs() < f64::EPSILON);
        assert!((trade.exit_price - 55.0).abs() < f64::EPSILON);
        assert!((trade.pnl - 485.0).abs() < f64::EPSILON);
    }
}
