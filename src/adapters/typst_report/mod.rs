//! Typst-based PDF report generation (TRD Section 2.2, 11.3, 12).

pub mod chart_svg;
pub mod default_template;
pub mod tables;

use crate::domain::backtest::{BacktestResult, MultiCodeResult};
use crate::domain::error::SamtraderError;
use crate::domain::strategy::Strategy;
use crate::ports::config_port::ConfigPort;
use crate::ports::report_port::ReportPort;
use std::collections::HashMap;
use std::fs;
use std::path::PathBuf;

pub struct TypstReportAdapter {
    template_path: Option<PathBuf>,
}

impl TypstReportAdapter {
    pub fn new(template_path: Option<PathBuf>) -> Self {
        Self { template_path }
    }

    pub fn from_config<C: ConfigPort>(config: &C) -> Self {
        let template_path = config
            .get_string("report", "template_path")
            .map(PathBuf::from);
        Self { template_path }
    }

    fn load_template(&self) -> Result<String, SamtraderError> {
        match &self.template_path {
            Some(path) => fs::read_to_string(path).map_err(SamtraderError::Io),
            None => Ok(default_template::get_default_template().to_string()),
        }
    }

    fn resolve_placeholders(&self, template: &str, context: &HashMap<&str, String>) -> String {
        let mut result = template.to_string();
        for (key, value) in context {
            let placeholder = format!("{{{{{}}}}}", key);
            result = result.replace(&placeholder, value);
        }
        result
    }

    fn build_context(
        &self,
        result: &BacktestResult,
        strategy: &Strategy,
    ) -> HashMap<&'static str, String> {
        let mut context = HashMap::new();

        context.insert("STRATEGY_NAME", strategy.name.clone());
        context.insert("STRATEGY_DESCRIPTION", strategy.description.clone());

        context.insert(
            "INITIAL_CAPITAL",
            format!("{:.2}", result.portfolio.initial_capital),
        );
        context.insert("FINAL_CASH", format!("{:.2}", result.portfolio.cash));

        let total_pnl: f64 = result.portfolio.closed_trades.iter().map(|t| t.pnl).sum();
        context.insert("TOTAL_PNL", format!("{:.2}", total_pnl));

        let return_pct = if result.portfolio.initial_capital > 0.0 {
            (total_pnl / result.portfolio.initial_capital) * 100.0
        } else {
            0.0
        };
        context.insert("RETURN_PCT", format!("{:.2}", return_pct));

        let trade_count = result.portfolio.closed_trades.len();
        context.insert("TRADE_COUNT", trade_count.to_string());

        let winning_trades: Vec<_> = result
            .portfolio
            .closed_trades
            .iter()
            .filter(|t| t.pnl > 0.0)
            .collect();
        let win_rate = if trade_count > 0 {
            (winning_trades.len() as f64 / trade_count as f64) * 100.0
        } else {
            0.0
        };
        context.insert("WIN_RATE", format!("{:.2}", win_rate));

        context.insert(
            "TRADES_TABLE",
            tables::format_trades_table(&result.portfolio.closed_trades),
        );
        context.insert(
            "EQUITY_CHART",
            chart_svg::format_equity_chart(&result.portfolio.equity_curve),
        );

        context
    }
}

impl ReportPort for TypstReportAdapter {
    fn write(
        &self,
        result: &BacktestResult,
        strategy: &Strategy,
        output_path: &str,
    ) -> Result<(), SamtraderError> {
        let template = self.load_template()?;
        let context = self.build_context(result, strategy);
        let resolved = self.resolve_placeholders(&template, &context);

        fs::write(output_path, resolved).map_err(SamtraderError::Io)
    }

    fn write_multi(
        &self,
        result: &MultiCodeResult,
        strategy: &Strategy,
        output_path: &str,
    ) -> Result<(), SamtraderError> {
        let template = self.load_template()?;
        let context = self.build_context(&result.aggregate, strategy);

        let mut resolved = self.resolve_placeholders(&template, &context);

        let per_code_summary: String = result
            .code_results
            .iter()
            .map(|cr| {
                let pnl: f64 = cr
                    .result
                    .portfolio
                    .closed_trades
                    .iter()
                    .map(|t| t.pnl)
                    .sum();
                format!(
                    "- {}: {} trades, P&L: {:.2}",
                    cr.code,
                    cr.result.portfolio.closed_trades.len(),
                    pnl
                )
            })
            .collect::<Vec<_>>()
            .join("\n");

        resolved = resolved.replace("{{PER_CODE_SUMMARY}}", &per_code_summary);

        fs::write(output_path, resolved).map_err(SamtraderError::Io)
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::domain::backtest::CodeResult;
    use crate::domain::portfolio::{EquityPoint, Portfolio};
    use crate::domain::position::ClosedTrade;
    use crate::domain::rule::Operand;
    use crate::domain::rule::Rule;
    use chrono::NaiveDate;
    use std::collections::HashMap as StdHashMap;
    use tempfile::NamedTempFile;

    fn sample_strategy() -> Strategy {
        Strategy {
            name: "Test Strategy".into(),
            description: "A test strategy".into(),
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

    fn sample_backtest_result() -> BacktestResult {
        let mut portfolio = Portfolio::new(100_000.0);
        portfolio.cash = 110_000.0;
        portfolio.closed_trades = vec![
            ClosedTrade {
                code: "BHP".into(),
                exchange: "ASX".into(),
                quantity: 100,
                entry_price: 50.0,
                exit_price: 55.0,
                entry_date: NaiveDate::from_ymd_opt(2024, 1, 10).unwrap(),
                exit_date: NaiveDate::from_ymd_opt(2024, 1, 15).unwrap(),
                pnl: 500.0,
            },
            ClosedTrade {
                code: "CBA".into(),
                exchange: "ASX".into(),
                quantity: 50,
                entry_price: 100.0,
                exit_price: 90.0,
                entry_date: NaiveDate::from_ymd_opt(2024, 1, 20).unwrap(),
                exit_date: NaiveDate::from_ymd_opt(2024, 1, 25).unwrap(),
                pnl: -500.0,
            },
        ];
        portfolio.equity_curve = vec![
            EquityPoint {
                date: NaiveDate::from_ymd_opt(2024, 1, 1).unwrap(),
                equity: 100_000.0,
            },
            EquityPoint {
                date: NaiveDate::from_ymd_opt(2024, 1, 31).unwrap(),
                equity: 110_000.0,
            },
        ];
        BacktestResult { portfolio }
    }

    #[test]
    fn new_without_template_path() {
        let adapter = TypstReportAdapter::new(None);
        assert!(adapter.template_path.is_none());
    }

    #[test]
    fn new_with_template_path() {
        let path = PathBuf::from("/custom/template.typ");
        let adapter = TypstReportAdapter::new(Some(path.clone()));
        assert_eq!(adapter.template_path, Some(path));
    }

    #[test]
    fn resolve_placeholders_basic() {
        let adapter = TypstReportAdapter::new(None);
        let template = "Hello {{NAME}}, value: {{VALUE}}";
        let mut context: HashMap<&str, String> = HashMap::new();
        context.insert("NAME", "World".to_string());
        context.insert("VALUE", "42".to_string());

        let result = adapter.resolve_placeholders(template, &context);
        assert_eq!(result, "Hello World, value: 42");
    }

    #[test]
    fn resolve_placeholders_missing_key() {
        let adapter = TypstReportAdapter::new(None);
        let template = "Hello {{NAME}}, {{UNKNOWN}}";
        let mut context: HashMap<&str, String> = HashMap::new();
        context.insert("NAME", "World".to_string());

        let result = adapter.resolve_placeholders(template, &context);
        assert_eq!(result, "Hello World, {{UNKNOWN}}");
    }

    #[test]
    fn write_generates_report() {
        let adapter = TypstReportAdapter::new(None);
        let result = sample_backtest_result();
        let strategy = sample_strategy();

        let temp_file = NamedTempFile::new().unwrap();
        let output_path = temp_file.path().to_str().unwrap();

        adapter.write(&result, &strategy, output_path).unwrap();

        let content = fs::read_to_string(output_path).unwrap();
        assert!(content.contains("Test Strategy"));
        assert!(content.contains("100000.00"));
        assert!(content.contains("110000.00"));
    }

    #[test]
    fn write_includes_trade_data() {
        let adapter = TypstReportAdapter::new(None);
        let result = sample_backtest_result();
        let strategy = sample_strategy();

        let temp_file = NamedTempFile::new().unwrap();
        let output_path = temp_file.path().to_str().unwrap();

        adapter.write(&result, &strategy, output_path).unwrap();

        let content = fs::read_to_string(output_path).unwrap();
        assert!(content.contains("BHP"));
        assert!(content.contains("CBA"));
    }

    #[test]
    fn write_custom_template() {
        let custom_template = "Custom: {{STRATEGY_NAME}} - {{TOTAL_PNL}}";
        let template_file = NamedTempFile::new().unwrap();
        fs::write(template_file.path(), custom_template).unwrap();

        let adapter = TypstReportAdapter::new(Some(template_file.path().to_path_buf()));
        let result = sample_backtest_result();
        let strategy = sample_strategy();

        let output_file = NamedTempFile::new().unwrap();
        let output_path = output_file.path().to_str().unwrap();

        adapter.write(&result, &strategy, output_path).unwrap();

        let content = fs::read_to_string(output_path).unwrap();
        assert_eq!(content, "Custom: Test Strategy - 0.00");
    }

    #[test]
    fn write_multi_includes_per_code_summary() {
        let adapter = TypstReportAdapter::new(None);

        let mut aggregate_portfolio = Portfolio::new(100_000.0);
        aggregate_portfolio.cash = 105_000.0;
        aggregate_portfolio.closed_trades = vec![ClosedTrade {
            code: "BHP".into(),
            exchange: "ASX".into(),
            quantity: 100,
            entry_price: 50.0,
            exit_price: 55.0,
            entry_date: NaiveDate::from_ymd_opt(2024, 1, 10).unwrap(),
            exit_date: NaiveDate::from_ymd_opt(2024, 1, 15).unwrap(),
            pnl: 500.0,
        }];

        let mut bhp_portfolio = Portfolio::new(50_000.0);
        bhp_portfolio.closed_trades = vec![ClosedTrade {
            code: "BHP".into(),
            exchange: "ASX".into(),
            quantity: 100,
            entry_price: 50.0,
            exit_price: 55.0,
            entry_date: NaiveDate::from_ymd_opt(2024, 1, 10).unwrap(),
            exit_date: NaiveDate::from_ymd_opt(2024, 1, 15).unwrap(),
            pnl: 500.0,
        }];

        let multi_result = MultiCodeResult {
            aggregate: BacktestResult {
                portfolio: aggregate_portfolio,
            },
            code_results: vec![CodeResult {
                code: "BHP".into(),
                result: BacktestResult {
                    portfolio: bhp_portfolio,
                },
            }],
        };

        let strategy = sample_strategy();

        let temp_file = NamedTempFile::new().unwrap();
        let output_path = temp_file.path().to_str().unwrap();

        adapter
            .write_multi(&multi_result, &strategy, output_path)
            .unwrap();

        let content = fs::read_to_string(output_path).unwrap();
        assert!(content.contains("BHP"));
    }

    struct MockConfig {
        values: StdHashMap<String, String>,
    }

    impl MockConfig {
        fn new() -> Self {
            Self {
                values: StdHashMap::new(),
            }
        }

        fn set(&mut self, section: &str, key: &str, value: &str) {
            self.values
                .insert(format!("{}.{}", section, key), value.to_string());
        }
    }

    impl ConfigPort for MockConfig {
        fn get_string(&self, section: &str, key: &str) -> Option<String> {
            self.values.get(&format!("{}.{}", section, key)).cloned()
        }

        fn get_int(&self, _section: &str, _key: &str, default: i64) -> i64 {
            default
        }

        fn get_double(&self, _section: &str, _key: &str, default: f64) -> f64 {
            default
        }

        fn get_bool(&self, _section: &str, _key: &str, default: bool) -> bool {
            default
        }
    }

    #[test]
    fn from_config_without_template_path() {
        let config = MockConfig::new();
        let adapter = TypstReportAdapter::from_config(&config);
        assert!(adapter.template_path.is_none());
    }

    #[test]
    fn from_config_with_template_path() {
        let mut config = MockConfig::new();
        config.set("report", "template_path", "/custom/template.typ");
        let adapter = TypstReportAdapter::from_config(&config);
        assert_eq!(
            adapter.template_path,
            Some(PathBuf::from("/custom/template.typ"))
        );
    }

    #[test]
    fn write_handles_missing_template_file() {
        let adapter = TypstReportAdapter::new(Some(PathBuf::from("/nonexistent/template.typ")));
        let result = sample_backtest_result();
        let strategy = sample_strategy();

        let temp_file = NamedTempFile::new().unwrap();
        let output_path = temp_file.path().to_str().unwrap();

        let err = adapter.write(&result, &strategy, output_path);
        assert!(err.is_err());
    }
}
