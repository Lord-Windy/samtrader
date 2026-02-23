//! CLI definition and dispatch (TRD §13.1-13.2).

use clap::{Parser, Subcommand};
use std::path::PathBuf;
use std::process::ExitCode;

use crate::adapters::file_config_adapter::FileConfigAdapter;
use crate::domain::config_validation::{validate_backtest_config, validate_strategy_config};
use crate::domain::error::SamtraderError;

#[derive(Parser, Debug)]
#[command(
    name = "samtrader",
    version,
    about = "Systematic trading strategy backtester"
)]
pub struct Cli {
    #[command(subcommand)]
    pub command: Command,
}

#[derive(Subcommand, Debug)]
pub enum Command {
    Backtest {
        #[arg(short, long, value_name = "FILE")]
        config: PathBuf,
    },
    ListSymbols {
        #[arg(short = 'e', long)]
        exchange: String,
    },
    Validate {
        #[arg(short, long, value_name = "FILE")]
        config: PathBuf,
    },
    Info,
}

pub fn run(cli: Cli) -> ExitCode {
    match cli.command {
        Command::Backtest { config } => run_backtest(&config),
        Command::ListSymbols { exchange } => run_list_symbols(&exchange),
        Command::Validate { config } => run_validate(&config),
        Command::Info => run_info(),
    }
}

fn run_backtest(config_path: &PathBuf) -> ExitCode {
    eprintln!("Loading config from {}", config_path.display());
    let adapter = match FileConfigAdapter::from_file(config_path) {
        Ok(a) => a,
        Err(e) => {
            let err = SamtraderError::ConfigParse {
                file: config_path.display().to_string(),
                reason: e.to_string(),
            };
            eprintln!("error: {err}");
            return (&err).into();
        }
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

fn run_list_symbols(exchange: &str) -> ExitCode {
    eprintln!("Listing symbols for exchange: {exchange}");
    eprintln!("list-symbols not yet implemented");
    ExitCode::SUCCESS
}

fn run_validate(config_path: &PathBuf) -> ExitCode {
    eprintln!("Validating config: {}", config_path.display());
    let adapter = match FileConfigAdapter::from_file(config_path) {
        Ok(a) => a,
        Err(e) => {
            let err = SamtraderError::ConfigParse {
                file: config_path.display().to_string(),
                reason: e.to_string(),
            };
            eprintln!("error: {err}");
            return (&err).into();
        }
    };
    match validate_backtest_config(&adapter) {
        Ok(()) => eprintln!("backtest config: ok"),
        Err(e) => {
            eprintln!("backtest config: {e}");
            return (&e).into();
        }
    }
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
