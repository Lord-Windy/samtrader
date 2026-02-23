//! CLI definition and dispatch (TRD §13.1-13.2).

use clap::{Parser, Subcommand};
use std::path::PathBuf;
use std::process::ExitCode;

use crate::adapters::file_config_adapter::FileConfigAdapter;
use crate::domain::config_validation::{validate_backtest_config, validate_strategy_config};
use crate::domain::error::SamtraderError;
use crate::domain::rule_parser;
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
            dry_run: _,
        } => run_backtest(&config),
        Command::ListSymbols { exchange, config } => run_list_symbols(&exchange, config.as_ref()),
        Command::Validate { strategy } => run_validate(&strategy),
        Command::Info {
            code,
            exchange,
            config,
        } => run_info(code.as_deref(), exchange.as_deref(), config.as_ref()),
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

fn run_list_symbols(exchange: &str, config_path: Option<&PathBuf>) -> ExitCode {
    let config_path = match config_path {
        Some(p) => p,
        None => {
            eprintln!("error: --config is required for list-symbols");
            return ExitCode::from(1);
        }
    };

    let config = match load_config(config_path) {
        Ok(c) => c,
        Err(code) => return code,
    };

    #[cfg(feature = "postgres")]
    {
        use crate::adapters::postgres_adapter::PostgresAdapter;
        use crate::ports::data_port::DataPort;

        let adapter = match PostgresAdapter::from_config(&config) {
            Ok(a) => a,
            Err(e) => {
                eprintln!("error: {e}");
                return (&e).into();
            }
        };

        let symbols = match adapter.list_symbols(exchange) {
            Ok(s) => s,
            Err(e) => {
                eprintln!("error: {e}");
                return (&e).into();
            }
        };

        if symbols.is_empty() {
            eprintln!("No symbols found for exchange {}", exchange);
        } else {
            for symbol in &symbols {
                println!("{}", symbol);
            }
            eprintln!("{} symbols found", symbols.len());
        }
        ExitCode::SUCCESS
    }

    #[cfg(not(feature = "postgres"))]
    {
        eprintln!("error: postgres feature is required for list-symbols");
        ExitCode::from(1)
    }
}

fn run_validate(strategy_path: &PathBuf) -> ExitCode {
    eprintln!("Validating strategy: {}", strategy_path.display());
    let adapter = match load_config(strategy_path) {
        Ok(a) => a,
        Err(code) => return code,
    };

    match validate_strategy_config(&adapter) {
        Ok(()) => {}
        Err(e) => {
            eprintln!("error: {e}");
            return (&e).into();
        }
    }

    let entry_long = adapter
        .get_string("strategy", "entry_long")
        .unwrap_or_default();
    let exit_long = adapter
        .get_string("strategy", "exit_long")
        .unwrap_or_default();

    eprintln!("\nEntry Long Rule:");
    match rule_parser::parse(&entry_long) {
        Ok(rule) => {
            eprintln!("  Parsed: {}", rule);
            eprintln!("  Raw:    {}", entry_long);
        }
        Err(e) => {
            eprintln!("  error: {}", e.display_with_context(&entry_long));
            return (&SamtraderError::from(e)).into();
        }
    }

    eprintln!("\nExit Long Rule:");
    match rule_parser::parse(&exit_long) {
        Ok(rule) => {
            eprintln!("  Parsed: {}", rule);
            eprintln!("  Raw:    {}", exit_long);
        }
        Err(e) => {
            eprintln!("  error: {}", e.display_with_context(&exit_long));
            return (&SamtraderError::from(e)).into();
        }
    }

    if let Some(entry_short) = adapter.get_string("strategy", "entry_short") {
        if !entry_short.trim().is_empty() {
            eprintln!("\nEntry Short Rule:");
            match rule_parser::parse(&entry_short) {
                Ok(rule) => {
                    eprintln!("  Parsed: {}", rule);
                    eprintln!("  Raw:    {}", entry_short);
                }
                Err(e) => {
                    eprintln!("  error: {}", e.display_with_context(&entry_short));
                    return (&SamtraderError::from(e)).into();
                }
            }
        }
    }

    if let Some(exit_short) = adapter.get_string("strategy", "exit_short") {
        if !exit_short.trim().is_empty() {
            eprintln!("\nExit Short Rule:");
            match rule_parser::parse(&exit_short) {
                Ok(rule) => {
                    eprintln!("  Parsed: {}", rule);
                    eprintln!("  Raw:    {}", exit_short);
                }
                Err(e) => {
                    eprintln!("  error: {}", e.display_with_context(&exit_short));
                    return (&SamtraderError::from(e)).into();
                }
            }
        }
    }

    eprintln!("\nStrategy configuration is valid.");
    ExitCode::SUCCESS
}

fn run_info(code: Option<&str>, exchange: Option<&str>, config_path: Option<&PathBuf>) -> ExitCode {
    let config_path = match config_path {
        Some(p) => p,
        None => {
            eprintln!("error: --config is required for info");
            return ExitCode::from(1);
        }
    };

    let config = match load_config(config_path) {
        Ok(c) => c,
        Err(code) => return code,
    };

    let codes = resolve_codes(code, &config);
    let exchange = match exchange {
        Some(e) => e.to_string(),
        None => match config.get_string("backtest", "exchange") {
            Some(e) => e,
            None => {
                eprintln!("error: exchange is required (use --exchange or set in config)");
                return ExitCode::from(1);
            }
        },
    };

    #[cfg(feature = "postgres")]
    {
        use crate::adapters::postgres_adapter::PostgresAdapter;
        use crate::ports::data_port::DataPort;

        let adapter = match PostgresAdapter::from_config(&config) {
            Ok(a) => a,
            Err(e) => {
                eprintln!("error: {e}");
                return (&e).into();
            }
        };

        for c in &codes {
            match adapter.get_data_range(c, &exchange) {
                Ok(Some((min_date, max_date, count))) => {
                    println!(
                        "{}.{}: {} bars, {} to {}",
                        c, exchange, count, min_date, max_date
                    );
                }
                Ok(None) => {
                    eprintln!("{}.{}: no data found", c, exchange);
                }
                Err(e) => {
                    eprintln!("error querying {}.{}: {}", c, exchange, e);
                }
            }
        }
        ExitCode::SUCCESS
    }

    #[cfg(not(feature = "postgres"))]
    {
        eprintln!("error: postgres feature is required for info");
        ExitCode::from(1)
    }
}

fn resolve_codes(code_override: Option<&str>, config: &dyn ConfigPort) -> Vec<String> {
    if let Some(c) = code_override {
        return vec![c.to_uppercase()];
    }

    if let Some(codes_str) = config.get_string("backtest", "codes") {
        return codes_str
            .split(',')
            .map(|s| s.trim().to_uppercase())
            .filter(|s| !s.is_empty())
            .collect();
    }

    if let Some(code) = config.get_string("backtest", "code") {
        let code = code.trim().to_uppercase();
        if !code.is_empty() {
            return vec![code];
        }
    }

    vec![]
}
