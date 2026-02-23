//! Trade execution and fill simulation (TRD Section 9).
//!
//! Implements entry/exit logic with slippage, quantity sizing, commissions,
//! and stop-loss/take-profit trigger checking.

use chrono::NaiveDate;
use std::collections::HashMap;

use super::portfolio::Portfolio;
use super::position::{ClosedTrade, Position};

/// Configuration for backtest execution parameters.
#[derive(Debug, Clone, PartialEq)]
pub struct ExecutionConfig {
    pub commission_per_trade: f64,
    pub commission_pct: f64,
    pub slippage_pct: f64,
    pub allow_shorting: bool,
}

impl Default for ExecutionConfig {
    fn default() -> Self {
        ExecutionConfig {
            commission_per_trade: 0.0,
            commission_pct: 0.0,
            slippage_pct: 0.0,
            allow_shorting: false,
        }
    }
}

/// Strategy parameters needed for execution.
#[derive(Debug, Clone, PartialEq)]
pub struct ExecutionParams {
    pub position_size: f64,
    pub stop_loss_pct: f64,
    pub take_profit_pct: f64,
}

impl Default for ExecutionParams {
    fn default() -> Self {
        ExecutionParams {
            position_size: 0.25,
            stop_loss_pct: 0.0,
            take_profit_pct: 0.0,
        }
    }
}

/// Calculate commission: flat_fee + (trade_value * pct / 100).
pub fn calculate_commission(trade_value: f64, config: &ExecutionConfig) -> f64 {
    config.commission_per_trade + (trade_value * config.commission_pct / 100.0)
}

/// Apply slippage to a price for a long entry.
/// Long entry (buy): execution_price = market_price * (1 + slippage_pct / 100)
pub fn apply_slippage_long_entry(market_price: f64, slippage_pct: f64) -> f64 {
    market_price * (1.0 + slippage_pct / 100.0)
}

/// Apply slippage to a price for a short entry.
/// Short entry (sell short): execution_price = market_price * (1 - slippage_pct / 100)
pub fn apply_slippage_short_entry(market_price: f64, slippage_pct: f64) -> f64 {
    market_price * (1.0 - slippage_pct / 100.0)
}

/// Apply slippage to a price for a long exit (sell).
/// Long exit (sell): execution_price = market_price * (1 - slippage_pct / 100)
pub fn apply_slippage_long_exit(market_price: f64, slippage_pct: f64) -> f64 {
    market_price * (1.0 - slippage_pct / 100.0)
}

/// Apply slippage to a price for a short exit (buy to cover).
/// Short exit (buy to cover): execution_price = market_price * (1 + slippage_pct / 100)
pub fn apply_slippage_short_exit(market_price: f64, slippage_pct: f64) -> f64 {
    market_price * (1.0 + slippage_pct / 100.0)
}

/// Result of an entry attempt.
#[derive(Debug, Clone, PartialEq)]
pub enum EntryResult {
    Entered {
        quantity: i64,
        execution_price: f64,
        cost: f64,
        commission: f64,
    },
    InsufficientCapital,
}

/// Enter a long position.
///
/// Steps per TRD §9.1:
/// 1. Apply slippage to execution price
/// 2. Calculate available capital (cash * position_size)
/// 3. Calculate quantity (whole shares only)
/// 4. If quantity == 0, return InsufficientCapital
/// 5. Calculate cost and commission
/// 6. Deduct cost + commission from portfolio cash
/// 7. Calculate stop-loss/take-profit prices
/// 8. Add position to portfolio
pub fn enter_long(
    portfolio: &mut Portfolio,
    code: &str,
    exchange: &str,
    market_price: f64,
    date: NaiveDate,
    params: &ExecutionParams,
    config: &ExecutionConfig,
) -> EntryResult {
    let execution_price = apply_slippage_long_entry(market_price, config.slippage_pct);

    let available_capital = portfolio.cash * params.position_size;
    let quantity = (available_capital / execution_price).floor() as i64;

    if quantity == 0 {
        return EntryResult::InsufficientCapital;
    }

    let cost = quantity as f64 * execution_price;
    let commission = calculate_commission(cost, config);
    let total_cost = cost + commission;

    if total_cost > portfolio.cash {
        return EntryResult::InsufficientCapital;
    }

    portfolio.cash -= total_cost;

    let stop_loss = if params.stop_loss_pct > 0.0 {
        execution_price * (1.0 - params.stop_loss_pct / 100.0)
    } else {
        0.0
    };

    let take_profit = if params.take_profit_pct > 0.0 {
        execution_price * (1.0 + params.take_profit_pct / 100.0)
    } else {
        0.0
    };

    let position = Position {
        code: code.to_string(),
        exchange: exchange.to_string(),
        quantity,
        entry_price: execution_price,
        entry_date: date,
        stop_loss,
        take_profit,
    };

    portfolio.add_position(position);

    EntryResult::Entered {
        quantity,
        execution_price,
        cost,
        commission,
    }
}

/// Enter a short position.
///
/// Steps per TRD §9.1:
/// 1. Apply slippage (reduced price for short)
/// 2. Calculate available capital and quantity
/// 3. Calculate cost and commission
/// 4. Deduct cost + commission from portfolio cash (margin escrow)
/// 5. Stop-loss is above entry, take-profit is below entry
/// 6. Add position with negative quantity
pub fn enter_short(
    portfolio: &mut Portfolio,
    code: &str,
    exchange: &str,
    market_price: f64,
    date: NaiveDate,
    params: &ExecutionParams,
    config: &ExecutionConfig,
) -> EntryResult {
    if !config.allow_shorting {
        return EntryResult::InsufficientCapital;
    }

    let execution_price = apply_slippage_short_entry(market_price, config.slippage_pct);

    let available_capital = portfolio.cash * params.position_size;
    let quantity = (available_capital / execution_price).floor() as i64;

    if quantity == 0 {
        return EntryResult::InsufficientCapital;
    }

    let cost = quantity as f64 * execution_price;
    let commission = calculate_commission(cost, config);
    let total_cost = cost + commission;

    if total_cost > portfolio.cash {
        return EntryResult::InsufficientCapital;
    }

    portfolio.cash -= total_cost;

    let stop_loss = if params.stop_loss_pct > 0.0 {
        execution_price * (1.0 + params.stop_loss_pct / 100.0)
    } else {
        0.0
    };

    let take_profit = if params.take_profit_pct > 0.0 {
        execution_price * (1.0 - params.take_profit_pct / 100.0)
    } else {
        0.0
    };

    let position = Position {
        code: code.to_string(),
        exchange: exchange.to_string(),
        quantity: -quantity,
        entry_price: execution_price,
        entry_date: date,
        stop_loss,
        take_profit,
    };

    portfolio.add_position(position);

    EntryResult::Entered {
        quantity: -quantity,
        execution_price,
        cost,
        commission,
    }
}

/// Result of an exit.
#[derive(Debug, Clone, PartialEq)]
pub struct ExitResult {
    pub quantity: i64,
    pub exit_price: f64,
    pub exit_value: f64,
    pub exit_commission: f64,
    pub pnl: f64,
}

/// Exit a position.
///
/// Steps per TRD §9.2:
/// 1. Determine direction from position quantity
/// 2. Apply slippage based on direction
/// 3. Calculate exit value and commission
/// 4. Calculate PnL (including round-trip commissions)
/// 5. Update cash: long adds sale proceeds; short returns escrowed
///    entry notional minus buy-to-cover cost
/// 6. Record closed trade
/// 7. Remove position from portfolio
pub fn exit_position(
    portfolio: &mut Portfolio,
    code: &str,
    market_price: f64,
    exit_date: NaiveDate,
    entry_commission: f64,
    config: &ExecutionConfig,
) -> Option<ExitResult> {
    let position = portfolio.remove_position(code)?;

    let exit_price = if position.is_long() {
        apply_slippage_long_exit(market_price, config.slippage_pct)
    } else {
        apply_slippage_short_exit(market_price, config.slippage_pct)
    };

    let qty_abs = position.quantity.unsigned_abs() as f64;
    let exit_value = qty_abs * exit_price;
    let exit_commission = calculate_commission(exit_value, config);

    let price_pnl = position.quantity as f64 * (exit_price - position.entry_price);
    let pnl = price_pnl - entry_commission - exit_commission;

    if position.is_long() {
        // Long exit: sell shares, receive proceeds minus commission
        portfolio.cash += exit_value - exit_commission;
    } else {
        // Short exit: return escrowed entry notional plus profit/loss, minus commission.
        // We escrowed entry_notional on entry; now settle the price difference.
        let entry_notional = qty_abs * position.entry_price;
        let short_profit = entry_notional - exit_value;
        portfolio.cash += entry_notional + short_profit - exit_commission;
    }

    let trade = ClosedTrade {
        code: position.code.clone(),
        exchange: position.exchange.clone(),
        quantity: position.quantity,
        entry_price: position.entry_price,
        exit_price,
        entry_date: position.entry_date,
        exit_date,
        pnl,
    };

    portfolio.record_trade(trade);

    Some(ExitResult {
        quantity: position.quantity,
        exit_price,
        exit_value,
        exit_commission,
        pnl,
    })
}

/// Check stop-loss and take-profit triggers.
///
/// Two-pass approach per TRD §9.3:
/// 1. First collect all triggered codes
/// 2. Then exit each triggered position
///
/// Returns the number of positions exited.
pub fn check_triggers(
    portfolio: &mut Portfolio,
    price_map: &HashMap<String, f64>,
    date: NaiveDate,
    entry_commissions: &HashMap<String, f64>,
    config: &ExecutionConfig,
) -> usize {
    let triggered_codes: Vec<String> = portfolio
        .positions
        .values()
        .filter_map(|pos| {
            let price = price_map.get(&pos.code)?;
            if pos.should_stop_loss(*price) || pos.should_take_profit(*price) {
                Some(pos.code.clone())
            } else {
                None
            }
        })
        .collect();

    let count = triggered_codes.len();

    for code in triggered_codes {
        let price = price_map.get(&code).copied().unwrap_or(0.0);
        let entry_commission = entry_commissions.get(&code).copied().unwrap_or(0.0);
        exit_position(portfolio, &code, price, date, entry_commission, config);
    }

    count
}

#[cfg(test)]
mod tests {
    use super::*;

    fn make_portfolio(cash: f64) -> Portfolio {
        Portfolio::new(cash)
    }

    fn make_config() -> ExecutionConfig {
        ExecutionConfig {
            commission_per_trade: 10.0,
            commission_pct: 0.1,
            slippage_pct: 0.05,
            allow_shorting: true,
        }
    }

    fn make_params() -> ExecutionParams {
        ExecutionParams {
            position_size: 0.25,
            stop_loss_pct: 5.0,
            take_profit_pct: 10.0,
        }
    }

    fn date() -> NaiveDate {
        NaiveDate::from_ymd_opt(2024, 1, 15).unwrap()
    }

    #[test]
    fn calculate_commission_basic() {
        let config = ExecutionConfig {
            commission_per_trade: 10.0,
            commission_pct: 0.1,
            ..Default::default()
        };
        let commission = calculate_commission(10000.0, &config);
        let expected = 10.0 + (10000.0 * 0.1 / 100.0);
        assert!((commission - expected).abs() < f64::EPSILON);
    }

    #[test]
    fn calculate_commission_zero_flat_fee() {
        let config = ExecutionConfig {
            commission_per_trade: 0.0,
            commission_pct: 0.5,
            ..Default::default()
        };
        let commission = calculate_commission(10000.0, &config);
        let expected = 10000.0 * 0.5 / 100.0;
        assert!((commission - expected).abs() < f64::EPSILON);
    }

    #[test]
    fn calculate_commission_zero_pct() {
        let config = ExecutionConfig {
            commission_per_trade: 10.0,
            commission_pct: 0.0,
            ..Default::default()
        };
        let commission = calculate_commission(10000.0, &config);
        assert!((commission - 10.0).abs() < f64::EPSILON);
    }

    #[test]
    fn slippage_long_entry() {
        let price = apply_slippage_long_entry(100.0, 0.05);
        let expected = 100.0 * 1.0005;
        assert!((price - expected).abs() < f64::EPSILON);
    }

    #[test]
    fn slippage_short_entry() {
        let price = apply_slippage_short_entry(100.0, 0.05);
        let expected = 100.0 * 0.9995;
        assert!((price - expected).abs() < f64::EPSILON);
    }

    #[test]
    fn slippage_long_exit() {
        let price = apply_slippage_long_exit(100.0, 0.05);
        let expected = 100.0 * 0.9995;
        assert!((price - expected).abs() < f64::EPSILON);
    }

    #[test]
    fn slippage_short_exit() {
        let price = apply_slippage_short_exit(100.0, 0.05);
        let expected = 100.0 * 1.0005;
        assert!((price - expected).abs() < f64::EPSILON);
    }

    #[test]
    fn enter_long_basic() {
        let mut portfolio = make_portfolio(100000.0);
        let config = make_config();
        let params = make_params();

        let result = enter_long(
            &mut portfolio,
            "BHP",
            "ASX",
            100.0,
            date(),
            &params,
            &config,
        );

        match result {
            EntryResult::Entered {
                quantity,
                execution_price,
                cost,
                commission,
            } => {
                let expected_price = 100.0 * 1.0005;
                assert!((execution_price - expected_price).abs() < f64::EPSILON);
                let expected_qty = ((100000.0 * 0.25) / expected_price).floor() as i64;
                assert_eq!(quantity, expected_qty);
                assert!((cost - (expected_qty as f64 * expected_price)).abs() < f64::EPSILON);
                let expected_commission = 10.0 + (cost * 0.1 / 100.0);
                assert!((commission - expected_commission).abs() < f64::EPSILON);

                assert!(portfolio.has_position("BHP"));
                let pos = portfolio.get_position("BHP").unwrap();
                assert!(pos.is_long());
                assert!((pos.entry_price - expected_price).abs() < f64::EPSILON);
                assert!(pos.stop_loss > 0.0);
                assert!(pos.take_profit > 0.0);
            }
            EntryResult::InsufficientCapital => panic!("Expected entry to succeed"),
        }
    }

    #[test]
    fn enter_long_insufficient_capital() {
        let mut portfolio = make_portfolio(10.0);
        let config = make_config();
        let params = make_params();

        let result = enter_long(
            &mut portfolio,
            "BHP",
            "ASX",
            100.0,
            date(),
            &params,
            &config,
        );

        assert!(matches!(result, EntryResult::InsufficientCapital));
        assert!(!portfolio.has_position("BHP"));
    }

    #[test]
    fn enter_long_no_stop_loss_or_take_profit() {
        let mut portfolio = make_portfolio(100000.0);
        let config = make_config();
        let params = ExecutionParams {
            position_size: 0.25,
            stop_loss_pct: 0.0,
            take_profit_pct: 0.0,
        };

        enter_long(
            &mut portfolio,
            "BHP",
            "ASX",
            100.0,
            date(),
            &params,
            &config,
        );

        let pos = portfolio.get_position("BHP").unwrap();
        assert!((pos.stop_loss - 0.0).abs() < f64::EPSILON);
        assert!((pos.take_profit - 0.0).abs() < f64::EPSILON);
    }

    #[test]
    fn enter_short_basic() {
        let mut portfolio = make_portfolio(100000.0);
        let config = make_config();
        let params = make_params();

        let result = enter_short(
            &mut portfolio,
            "CBA",
            "ASX",
            100.0,
            date(),
            &params,
            &config,
        );

        match result {
            EntryResult::Entered {
                quantity,
                execution_price,
                cost,
                commission,
            } => {
                assert!(quantity < 0);
                let expected_price = 100.0 * 0.9995;
                assert!((execution_price - expected_price).abs() < f64::EPSILON);

                // Cash should be deducted (cost + commission), not increased
                let expected_cash = 100000.0 - cost - commission;
                assert!((portfolio.cash - expected_cash).abs() < f64::EPSILON);

                assert!(portfolio.has_position("CBA"));
                let pos = portfolio.get_position("CBA").unwrap();
                assert!(pos.is_short());
                assert!(pos.stop_loss > execution_price);
                assert!(pos.take_profit < execution_price);
            }
            EntryResult::InsufficientCapital => panic!("Expected entry to succeed"),
        }
    }

    #[test]
    fn enter_short_blocked_when_not_allowed() {
        let mut portfolio = make_portfolio(100000.0);
        let mut config = make_config();
        config.allow_shorting = false;
        let params = make_params();

        let result = enter_short(
            &mut portfolio,
            "CBA",
            "ASX",
            100.0,
            date(),
            &params,
            &config,
        );

        assert!(matches!(result, EntryResult::InsufficientCapital));
        assert!(!portfolio.has_position("CBA"));
    }

    #[test]
    fn exit_long_profit() {
        let mut portfolio = make_portfolio(100000.0);
        let config = make_config();
        let params = make_params();

        let entry_result = enter_long(
            &mut portfolio,
            "BHP",
            "ASX",
            100.0,
            date(),
            &params,
            &config,
        );
        let entry_commission = match entry_result {
            EntryResult::Entered { commission, .. } => commission,
            _ => panic!("Expected entry"),
        };

        let initial_cash = portfolio.cash;

        let exit_result = exit_position(
            &mut portfolio,
            "BHP",
            110.0,
            date(),
            entry_commission,
            &config,
        );

        assert!(exit_result.is_some());
        let exit = exit_result.unwrap();

        let exit_price = 110.0 * 0.9995;
        assert!((exit.exit_price - exit_price).abs() < f64::EPSILON);

        assert!(!portfolio.has_position("BHP"));
        assert_eq!(portfolio.closed_trades.len(), 1);

        let trade = &portfolio.closed_trades[0];
        assert!(trade.pnl > 0.0);
        assert!(portfolio.cash > initial_cash);
    }

    #[test]
    fn exit_short_profit() {
        let mut portfolio = make_portfolio(100000.0);
        let config = make_config();
        let params = make_params();

        let entry_result = enter_short(
            &mut portfolio,
            "CBA",
            "ASX",
            100.0,
            date(),
            &params,
            &config,
        );
        let (entry_commission, entry_cost) = match entry_result {
            EntryResult::Entered {
                commission, cost, ..
            } => (commission, cost),
            _ => panic!("Expected entry"),
        };

        let cash_after_entry = portfolio.cash;

        let exit_result = exit_position(
            &mut portfolio,
            "CBA",
            90.0,
            date(),
            entry_commission,
            &config,
        );

        assert!(exit_result.is_some());
        let exit = exit_result.unwrap();

        let exit_price = 90.0 * 1.0005;
        assert!((exit.exit_price - exit_price).abs() < f64::EPSILON);

        assert!(!portfolio.has_position("CBA"));
        assert_eq!(portfolio.closed_trades.len(), 1);

        let trade = &portfolio.closed_trades[0];
        assert!(trade.pnl > 0.0);
        // Short exit returns escrowed entry notional + profit - exit_commission
        let short_profit = entry_cost - exit.exit_value;
        let expected_cash = cash_after_entry + entry_cost + short_profit - exit.exit_commission;
        assert!((portfolio.cash - expected_cash).abs() < 1e-6);
        // Overall should be profitable (more cash than after entry)
        assert!(portfolio.cash > cash_after_entry);
    }

    #[test]
    fn exit_nonexistent_position() {
        let mut portfolio = make_portfolio(100000.0);
        let config = make_config();

        let result = exit_position(&mut portfolio, "XYZ", 100.0, date(), 0.0, &config);
        assert!(result.is_none());
    }

    #[test]
    fn check_triggers_stop_loss() {
        let mut portfolio = make_portfolio(100000.0);
        let config = make_config();
        let params = make_params();

        let entry_result = enter_long(
            &mut portfolio,
            "BHP",
            "ASX",
            100.0,
            date(),
            &params,
            &config,
        );
        let entry_commission = match entry_result {
            EntryResult::Entered { commission, .. } => commission,
            _ => panic!("Expected entry"),
        };

        let pos = portfolio.get_position("BHP").unwrap();
        let stop_loss_price = pos.stop_loss;

        let mut entry_commissions = HashMap::new();
        entry_commissions.insert("BHP".to_string(), entry_commission);

        let mut price_map = HashMap::new();
        price_map.insert("BHP".to_string(), stop_loss_price - 1.0);

        let exited = check_triggers(
            &mut portfolio,
            &price_map,
            date(),
            &entry_commissions,
            &config,
        );

        assert_eq!(exited, 1);
        assert!(!portfolio.has_position("BHP"));
        assert_eq!(portfolio.closed_trades.len(), 1);
    }

    #[test]
    fn check_triggers_take_profit() {
        let mut portfolio = make_portfolio(100000.0);
        let config = make_config();
        let params = make_params();

        let entry_result = enter_long(
            &mut portfolio,
            "BHP",
            "ASX",
            100.0,
            date(),
            &params,
            &config,
        );
        let entry_commission = match entry_result {
            EntryResult::Entered { commission, .. } => commission,
            _ => panic!("Expected entry"),
        };

        let pos = portfolio.get_position("BHP").unwrap();
        let take_profit_price = pos.take_profit;

        let mut entry_commissions = HashMap::new();
        entry_commissions.insert("BHP".to_string(), entry_commission);

        let mut price_map = HashMap::new();
        price_map.insert("BHP".to_string(), take_profit_price + 1.0);

        let exited = check_triggers(
            &mut portfolio,
            &price_map,
            date(),
            &entry_commissions,
            &config,
        );

        assert_eq!(exited, 1);
        assert!(!portfolio.has_position("BHP"));
        assert_eq!(portfolio.closed_trades.len(), 1);
    }

    #[test]
    fn check_triggers_no_trigger() {
        let mut portfolio = make_portfolio(100000.0);
        let config = make_config();
        let params = make_params();

        enter_long(
            &mut portfolio,
            "BHP",
            "ASX",
            100.0,
            date(),
            &params,
            &config,
        );

        let mut price_map = HashMap::new();
        price_map.insert("BHP".to_string(), 100.0);

        let entry_commissions = HashMap::new();

        let exited = check_triggers(
            &mut portfolio,
            &price_map,
            date(),
            &entry_commissions,
            &config,
        );

        assert_eq!(exited, 0);
        assert!(portfolio.has_position("BHP"));
        assert_eq!(portfolio.closed_trades.len(), 0);
    }

    #[test]
    fn check_triggers_multiple_positions() {
        let mut portfolio = make_portfolio(200000.0);
        let config = make_config();
        let params = ExecutionParams {
            position_size: 0.1,
            stop_loss_pct: 5.0,
            take_profit_pct: 10.0,
        };

        let entry1 = enter_long(
            &mut portfolio,
            "BHP",
            "ASX",
            100.0,
            date(),
            &params,
            &config,
        );
        let entry2 = enter_long(
            &mut portfolio,
            "CBA",
            "ASX",
            100.0,
            date(),
            &params,
            &config,
        );

        let mut entry_commissions = HashMap::new();
        if let EntryResult::Entered { commission, .. } = entry1 {
            entry_commissions.insert("BHP".to_string(), commission);
        }
        if let EntryResult::Entered { commission, .. } = entry2 {
            entry_commissions.insert("CBA".to_string(), commission);
        }

        let pos_bhp = portfolio.get_position("BHP").unwrap();
        let stop_loss_bhp = pos_bhp.stop_loss;

        let mut price_map = HashMap::new();
        price_map.insert("BHP".to_string(), stop_loss_bhp - 1.0);
        price_map.insert("CBA".to_string(), 100.0);

        let exited = check_triggers(
            &mut portfolio,
            &price_map,
            date(),
            &entry_commissions,
            &config,
        );

        assert_eq!(exited, 1);
        assert!(!portfolio.has_position("BHP"));
        assert!(portfolio.has_position("CBA"));
    }

    #[test]
    fn round_trip_pnl_calculation() {
        let mut portfolio = make_portfolio(100000.0);
        let config = ExecutionConfig {
            commission_per_trade: 10.0,
            commission_pct: 0.0,
            slippage_pct: 0.0,
            ..Default::default()
        };
        let params = ExecutionParams {
            position_size: 0.5,
            stop_loss_pct: 0.0,
            take_profit_pct: 0.0,
        };

        let entry_result = enter_long(
            &mut portfolio,
            "BHP",
            "ASX",
            100.0,
            date(),
            &params,
            &config,
        );

        let entry_commission = match entry_result {
            EntryResult::Entered {
                quantity,
                commission,
                ..
            } => {
                assert!(quantity > 0);
                commission
            }
            EntryResult::InsufficientCapital => panic!("Entry should have succeeded"),
        };

        assert!(
            portfolio.has_position("BHP"),
            "Position should have been entered"
        );

        let pos = portfolio.get_position("BHP").unwrap();
        let qty = pos.quantity;

        let result = exit_position(
            &mut portfolio,
            "BHP",
            110.0,
            date(),
            entry_commission,
            &config,
        );
        assert!(result.is_some(), "Exit should succeed");

        let trade = &portfolio.closed_trades[0];

        let expected_price_pnl = qty as f64 * (110.0 - 100.0);
        let expected_pnl = expected_price_pnl - entry_commission - 10.0;
        assert!((trade.pnl - expected_pnl).abs() < f64::EPSILON);
    }

    #[test]
    fn exit_long_loss() {
        let mut portfolio = make_portfolio(100000.0);
        let config = make_config();
        let params = ExecutionParams {
            position_size: 0.25,
            stop_loss_pct: 0.0,
            take_profit_pct: 0.0,
        };

        let entry_result = enter_long(
            &mut portfolio,
            "BHP",
            "ASX",
            100.0,
            date(),
            &params,
            &config,
        );
        let entry_commission = match entry_result {
            EntryResult::Entered { commission, .. } => commission,
            _ => panic!("Expected entry"),
        };

        let initial_cash = portfolio.cash;

        let exit_result = exit_position(
            &mut portfolio,
            "BHP",
            90.0, // price dropped
            date(),
            entry_commission,
            &config,
        );

        assert!(exit_result.is_some());
        let exit = exit_result.unwrap();
        assert!(exit.pnl < 0.0, "Long exit at lower price should be a loss");
        assert!(
            portfolio.cash < initial_cash + 25000.0,
            "Should recover less than entry cost"
        );

        let trade = &portfolio.closed_trades[0];
        assert!(trade.pnl < 0.0);
    }

    #[test]
    fn exit_short_loss() {
        let mut portfolio = make_portfolio(100000.0);
        let config = make_config();
        let params = ExecutionParams {
            position_size: 0.25,
            stop_loss_pct: 0.0,
            take_profit_pct: 0.0,
        };

        let entry_result = enter_short(
            &mut portfolio,
            "CBA",
            "ASX",
            100.0,
            date(),
            &params,
            &config,
        );
        let entry_commission = match entry_result {
            EntryResult::Entered { commission, .. } => commission,
            _ => panic!("Expected entry"),
        };

        let exit_result = exit_position(
            &mut portfolio,
            "CBA",
            110.0, // price went up — bad for short
            date(),
            entry_commission,
            &config,
        );

        assert!(exit_result.is_some());
        let exit = exit_result.unwrap();
        assert!(exit.pnl < 0.0, "Short exit at higher price should be a loss");

        let trade = &portfolio.closed_trades[0];
        assert!(trade.pnl < 0.0);
        // Net cash after full round-trip should be less than initial capital
        // (we lost money on the trade plus paid commissions)
        assert!(
            portfolio.cash < 100000.0,
            "Should have less than starting capital after a losing short, got {}",
            portfolio.cash,
        );
    }

    #[test]
    fn enter_long_cost_plus_commission_exceeds_cash() {
        // Edge case: quantity > 0 but cost + commission > cash
        // Use high commission rate so the commission tips it over
        let mut portfolio = make_portfolio(100.0);
        let config = ExecutionConfig {
            commission_per_trade: 0.0,
            commission_pct: 50.0, // 50% commission rate
            slippage_pct: 0.0,
            allow_shorting: false,
        };
        let params = ExecutionParams {
            position_size: 1.0, // use all capital
            stop_loss_pct: 0.0,
            take_profit_pct: 0.0,
        };

        // 100 / 10 = 10 shares, cost = 100, commission = 50, total = 150 > 100
        let result = enter_long(
            &mut portfolio,
            "BHP",
            "ASX",
            10.0,
            date(),
            &params,
            &config,
        );

        assert!(matches!(result, EntryResult::InsufficientCapital));
        assert!(!portfolio.has_position("BHP"));
        assert!((portfolio.cash - 100.0).abs() < f64::EPSILON, "Cash should be unchanged");
    }

    #[test]
    fn check_triggers_short_stop_loss() {
        let mut portfolio = make_portfolio(100000.0);
        let config = make_config();
        let params = make_params();

        let entry_result = enter_short(
            &mut portfolio,
            "CBA",
            "ASX",
            100.0,
            date(),
            &params,
            &config,
        );
        let entry_commission = match entry_result {
            EntryResult::Entered { commission, .. } => commission,
            _ => panic!("Expected entry"),
        };

        let pos = portfolio.get_position("CBA").unwrap();
        let stop_loss_price = pos.stop_loss; // above entry for short

        let mut entry_commissions = HashMap::new();
        entry_commissions.insert("CBA".to_string(), entry_commission);

        let mut price_map = HashMap::new();
        price_map.insert("CBA".to_string(), stop_loss_price + 1.0); // above stop loss

        let exited = check_triggers(
            &mut portfolio,
            &price_map,
            date(),
            &entry_commissions,
            &config,
        );

        assert_eq!(exited, 1);
        assert!(!portfolio.has_position("CBA"));
        assert_eq!(portfolio.closed_trades.len(), 1);
        // Short stopped out at a loss
        assert!(portfolio.closed_trades[0].pnl < 0.0);
    }

    #[test]
    fn check_triggers_short_take_profit() {
        let mut portfolio = make_portfolio(100000.0);
        let config = make_config();
        let params = make_params();

        let entry_result = enter_short(
            &mut portfolio,
            "CBA",
            "ASX",
            100.0,
            date(),
            &params,
            &config,
        );
        let entry_commission = match entry_result {
            EntryResult::Entered { commission, .. } => commission,
            _ => panic!("Expected entry"),
        };

        let pos = portfolio.get_position("CBA").unwrap();
        let take_profit_price = pos.take_profit; // below entry for short

        let mut entry_commissions = HashMap::new();
        entry_commissions.insert("CBA".to_string(), entry_commission);

        let mut price_map = HashMap::new();
        price_map.insert("CBA".to_string(), take_profit_price - 1.0); // below take profit

        let exited = check_triggers(
            &mut portfolio,
            &price_map,
            date(),
            &entry_commissions,
            &config,
        );

        assert_eq!(exited, 1);
        assert!(!portfolio.has_position("CBA"));
        assert_eq!(portfolio.closed_trades.len(), 1);
    }

    #[test]
    fn short_round_trip_cash_conservation() {
        // Verify that a short entry + exit with no slippage and no commissions
        // at the same price returns cash to exactly the starting amount
        let mut portfolio = make_portfolio(100000.0);
        let config = ExecutionConfig {
            commission_per_trade: 0.0,
            commission_pct: 0.0,
            slippage_pct: 0.0,
            allow_shorting: true,
        };
        let params = ExecutionParams {
            position_size: 0.25,
            stop_loss_pct: 0.0,
            take_profit_pct: 0.0,
        };

        enter_short(
            &mut portfolio,
            "CBA",
            "ASX",
            100.0,
            date(),
            &params,
            &config,
        );

        exit_position(&mut portfolio, "CBA", 100.0, date(), 0.0, &config);

        assert!(
            (portfolio.cash - 100000.0).abs() < f64::EPSILON,
            "Cash should be exactly restored after flat short round-trip, got {}",
            portfolio.cash,
        );
    }
}
