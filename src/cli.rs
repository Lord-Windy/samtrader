//! CLI definition and dispatch (TRD §13.1-13.2).

use clap::{Parser, Subcommand};
use std::path::PathBuf;
use std::process::ExitCode;

use crate::adapters::file_config_adapter::FileConfigAdapter;
use crate::domain::config_validation::{validate_backtest_config, validate_strategy_config};
use crate::domain::error::SamtraderError;
use crate::domain::rule::extract_indicators;
use crate::domain::rule_parser;
use crate::domain::universe::parse_codes;
use crate::ports::config_port::ConfigPort;

#[derive(Parser, Debug)]
#[command(name = "samtrader", about = "Algorithmic trading backtester")]
pub struct Cli {
    #[command(subcommand)]
    pub command: Command,
}

#[derive(Subcommand, Debug)]
pub enum Command {
    /// Run a backtest
    Backtest {
        #[arg(short, long)]
        config: PathBuf,
        #[arg(short, long)]
        strategy: Option<PathBuf>,
        #[arg(short, long)]
        output: Option<PathBuf>,
        #[arg(long)]
        code: Option<String>,
        #[arg(long)]
        exchange: Option<String>,
        #[arg(long)]
        dry_run: bool,
    },
    /// List available symbols on an exchange
    ListSymbols {
        #[arg(long)]
        exchange: String,
        #[arg(short, long)]
        config: Option<PathBuf>,
    },
    /// Validate a strategy configuration
    Validate {
        #[arg(short, long)]
        strategy: PathBuf,
    },
    /// Show data range for symbol(s)
    Info {
        #[arg(long)]
        code: Option<String>,
        #[arg(long)]
        exchange: Option<String>,
        #[arg(short, long)]
        config: Option<PathBuf>,
    },
}

pub fn run(cli: Cli) -> ExitCode {
    match cli.command {
        Command::Backtest {
            config,
            strategy: _,
            output: _,
            code: _,
            exchange: _,
            dry_run,
        } => {
            if dry_run {
                run_dry_run(&config)
            } else {
                run_backtest(&config)
            }
        }
        Command::ListSymbols {
            exchange,
            config: _,
        } => run_list_symbols(&exchange),
        Command::Validate { strategy } => run_validate(&strategy),
        Command::Info {
            code: _,
            exchange: _,
            config: _,
        } => run_info(),
    }
}

fn load_config(path: &PathBuf) -> Result<FileConfigAdapter, ExitCode> {
    FileConfigAdapter::from_file(path).map_err(|e| {
        let err = SamtraderError::ConfigParse {
            file: path.display().to_string(),
            reason: e.to_string(),
        };
        eprintln!("error: {err}");
        ExitCode::from(&err)
    })
}

fn run_backtest(config_path: &PathBuf) -> ExitCode {
    eprintln!("Loading config from {}", config_path.display());
    let adapter = match load_config(config_path) {
        Ok(a) => a,
        Err(code) => return code,
    };
    if let Err(e) = validate_backtest_config(&adapter) {
        eprintln!("error: {e}");
        return (&e).into();
    }
    if let Err(e) = validate_strategy_config(&adapter) {
        eprintln!("error: {e}");
        return (&e).into();
    }
    eprintln!("Config validated successfully");
    eprintln!("Backtest not yet implemented");
    ExitCode::SUCCESS
}

fn run_dry_run(config_path: &PathBuf) -> ExitCode {
    eprintln!("Loading config from {}", config_path.display());
    let adapter = match load_config(config_path) {
        Ok(a) => a,
        Err(code) => return code,
    };

    if let Err(e) = validate_backtest_config(&adapter) {
        eprintln!("error: {e}");
        return (&e).into();
    }
    if let Err(e) = validate_strategy_config(&adapter) {
        eprintln!("error: {e}");
        return (&e).into();
    }
    eprintln!("Config validated successfully");

    let entry_long_str = adapter
        .get_string("strategy", "entry_long")
        .unwrap_or_default();
    let exit_long_str = adapter
        .get_string("strategy", "exit_long")
        .unwrap_or_default();

    eprintln!("\nStrategy rules:");
    eprintln!("  entry_long: {}", entry_long_str);
    eprintln!("  exit_long:  {}", exit_long_str);

    let entry_rule = match rule_parser::parse(&entry_long_str) {
        Ok(r) => r,
        Err(e) => {
            eprintln!("error: failed to parse entry_long: {e}");
            return ExitCode::from(4);
        }
    };

    let exit_rule = match rule_parser::parse(&exit_long_str) {
        Ok(r) => r,
        Err(e) => {
            eprintln!("error: failed to parse exit_long: {e}");
            return ExitCode::from(4);
        }
    };

    let mut indicators = extract_indicators(&entry_rule);
    indicators.extend(extract_indicators(&exit_rule));

    let mut indicator_list: Vec<_> = indicators.iter().map(|i| i.to_string()).collect();
    indicator_list.sort();

    eprintln!("\nIndicators to compute:");
    for ind in &indicator_list {
        eprintln!("  {}", ind);
    }

    let exchange = adapter
        .get_string("backtest", "exchange")
        .unwrap_or_default();
    let codes_str = adapter
        .get_string("backtest", "codes")
        .or_else(|| adapter.get_string("backtest", "code"));

    eprintln!("\nUniverse:");
    eprintln!("  exchange: {}", exchange);

    match codes_str {
        Some(codes) => match parse_codes(&codes) {
            Ok(parsed_codes) => {
                eprintln!("  codes: {}", parsed_codes.join(", "));
            }
            Err(e) => {
                eprintln!("error: failed to parse codes: {e}");
                return ExitCode::from(2);
            }
        },
        None => {
            eprintln!("error: no codes configured");
            return ExitCode::from(2);
        }
    }

    eprintln!("\nDry run complete: configuration is valid");
    ExitCode::SUCCESS
}

fn run_list_symbols(exchange: &str) -> ExitCode {
    eprintln!("Listing symbols for exchange: {exchange}");
    eprintln!("list-symbols not yet implemented");
    ExitCode::SUCCESS
}

fn run_validate(strategy_path: &PathBuf) -> ExitCode {
    eprintln!("Validating strategy: {}", strategy_path.display());
    let adapter = match load_config(strategy_path) {
        Ok(a) => a,
        Err(code) => return code,
    };
    match validate_strategy_config(&adapter) {
        Ok(()) => eprintln!("strategy config: ok"),
        Err(e) => {
            eprintln!("strategy config: {e}");
            return (&e).into();
        }
    }
    ExitCode::SUCCESS
}

fn run_info() -> ExitCode {
    eprintln!("samtrader {}", env!("CARGO_PKG_VERSION"));
    eprintln!("Systematic trading strategy backtester");
    ExitCode::SUCCESS
}
