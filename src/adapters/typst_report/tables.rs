//! Table formatting for reports (TRD Section 3.9, 11.3).

use crate::domain::position::ClosedTrade;

pub fn format_trades_table(trades: &[ClosedTrade]) -> String {
    if trades.is_empty() {
        return "No trades executed.".to_string();
    }

    let mut rows: Vec<String> = vec![
        "| Code | Exchange | Qty | Entry | Exit | Entry Date | Exit Date | P&L |".to_string(),
        "| ---- | -------- | --- | ----- | ---- | ---------- | --------- | --- |".to_string(),
    ];

    for trade in trades {
        rows.push(format!(
            "| {} | {} | {} | {:.2} | {:.2} | {} | {} | {:.2} |",
            trade.code,
            trade.exchange,
            trade.quantity,
            trade.entry_price,
            trade.exit_price,
            trade.entry_date,
            trade.exit_date,
            trade.pnl
        ));
    }

    rows.join("\n")
}

#[cfg(test)]
mod tests {
    use super::*;
    use chrono::NaiveDate;

    fn sample_trade(code: &str, pnl: f64) -> ClosedTrade {
        ClosedTrade {
            code: code.to_string(),
            exchange: "ASX".to_string(),
            quantity: 100,
            entry_price: 50.0,
            exit_price: 55.0,
            entry_date: NaiveDate::from_ymd_opt(2024, 1, 10).unwrap(),
            exit_date: NaiveDate::from_ymd_opt(2024, 1, 15).unwrap(),
            pnl,
        }
    }

    #[test]
    fn format_empty_trades() {
        let result = format_trades_table(&[]);
        assert_eq!(result, "No trades executed.");
    }

    #[test]
    fn format_single_trade() {
        let trades = vec![sample_trade("BHP", 500.0)];
        let result = format_trades_table(&trades);

        assert!(result.contains("| Code |"));
        assert!(result.contains("| BHP |"));
        assert!(result.contains("500.00"));
    }

    #[test]
    fn format_multiple_trades() {
        let trades = vec![sample_trade("BHP", 500.0), sample_trade("CBA", -200.0)];
        let result = format_trades_table(&trades);

        assert!(result.contains("BHP"));
        assert!(result.contains("CBA"));
        assert!(result.contains("-200.00"));
    }

    #[test]
    fn table_has_header_separator() {
        let trades = vec![sample_trade("BHP", 500.0)];
        let result = format_trades_table(&trades);

        assert!(result.contains("| ---- |"));
    }
}
