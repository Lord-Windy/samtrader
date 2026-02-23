//! Strategy configuration and composition (TRD Section 3.6).

use crate::domain::rule::Rule;

#[derive(Debug, Clone)]
pub struct Strategy {
    pub name: String,
    pub description: String,
    pub entry_long: Rule,
    pub exit_long: Rule,
    pub entry_short: Option<Rule>,
    pub exit_short: Option<Rule>,
    pub position_size: f64,
    pub stop_loss_pct: f64,
    pub take_profit_pct: f64,
    pub max_positions: usize,
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::domain::rule::Operand;

    fn sample_strategy() -> Strategy {
        Strategy {
            name: "SMA Crossover".into(),
            description: "Simple moving average crossover strategy".into(),
            entry_long: Rule::Above {
                left: Operand::Close,
                right: Operand::Constant(100.0),
            },
            exit_long: Rule::Below {
                left: Operand::Close,
                right: Operand::Constant(100.0),
            },
            entry_short: None,
            exit_short: None,
            position_size: 0.25,
            stop_loss_pct: 0.0,
            take_profit_pct: 0.0,
            max_positions: 1,
        }
    }

    #[test]
    fn strategy_fields() {
        let s = sample_strategy();
        assert_eq!(s.name, "SMA Crossover");
        assert_eq!(s.description, "Simple moving average crossover strategy");
        assert_eq!(s.position_size, 0.25);
        assert_eq!(s.stop_loss_pct, 0.0);
        assert_eq!(s.take_profit_pct, 0.0);
        assert_eq!(s.max_positions, 1);
    }

    #[test]
    fn long_only_strategy() {
        let s = sample_strategy();
        assert!(s.entry_short.is_none());
        assert!(s.exit_short.is_none());
    }

    #[test]
    fn short_capable_strategy() {
        let mut s = sample_strategy();
        s.entry_short = Some(Rule::Below {
            left: Operand::Close,
            right: Operand::Constant(50.0),
        });
        s.exit_short = Some(Rule::Above {
            left: Operand::Close,
            right: Operand::Constant(50.0),
        });
        assert!(s.entry_short.is_some());
        assert!(s.exit_short.is_some());
    }
}
