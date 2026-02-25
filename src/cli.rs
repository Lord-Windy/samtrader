//! CLI definition and dispatch (TRD §13.1-13.2).

use chrono::NaiveDate;
use clap::{Parser, Subcommand};
use std::fs;
use std::path::PathBuf;
use std::process::ExitCode;

use crate::adapters::file_config_adapter::FileConfigAdapter;
use crate::adapters::typst_report;
use crate::adapters::typst_report::default_template;
use crate::domain::backtest::{self as backtest_engine, BacktestConfig};
use crate::domain::code_data::{build_unified_timeline, CodeData};
use crate::domain::config_validation::{validate_backtest_config, validate_strategy_config};
use crate::domain::error::SamtraderError;
use crate::domain::indicator::IndicatorType;
use crate::domain::indicator_helpers::compute_indicators;
use crate::domain::metrics::{CodeResult, Metrics};
use crate::domain::rule::extract_indicators;
use crate::domain::rule_parser;
use crate::domain::strategy::Strategy;
use crate::domain::universe::{parse_codes, validate_universe};
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
    /// Start the web server
    Serve {
        #[arg(short, long)]
        config: PathBuf,
    },
    /// Output an argon2 hash for a password
    HashPassword,
}

pub fn run(cli: Cli) -> ExitCode {
    match cli.command {
        Command::Backtest {
            config,
            strategy,
            output,
            code,
            exchange,
            dry_run,
        } => {
            if dry_run {
                run_dry_run(&config)
            } else {
                run_backtest(
                    &config,
                    strategy.as_ref(),
                    output.as_ref(),
                    code.as_deref(),
                    exchange.as_deref(),
                )
            }
        }
        Command::ListSymbols { exchange, config } => run_list_symbols(&exchange, config.as_ref()),
        Command::Validate { strategy } => run_validate(&strategy),
        Command::Info {
            code,
            exchange,
            config,
        } => run_info(code.as_deref(), exchange.as_deref(), config.as_ref()),
        Command::Serve { config } => run_serve(&config),
        Command::HashPassword => run_hash_password(),
    }
}

pub fn load_config(path: &PathBuf) -> Result<FileConfigAdapter, ExitCode> {
    FileConfigAdapter::from_file(path).map_err(|e| {
        let err = SamtraderError::ConfigParse {
            file: path.display().to_string(),
            reason: e.to_string(),
        };
        eprintln!("error: {err}");
        ExitCode::from(&err)
    })
}

fn run_backtest(
    config_path: &PathBuf,
    strategy_path: Option<&PathBuf>,
    output_path: Option<&PathBuf>,
    code_override: Option<&str>,
    exchange_override: Option<&str>,
) -> ExitCode {
    // Stage 1: Load config
    eprintln!("Loading config from {}", config_path.display());
    let adapter = match load_config(config_path) {
        Ok(a) => a,
        Err(code) => return code,
    };

    // Stage 2: Validate backtest config
    if let Err(e) = validate_backtest_config(&adapter) {
        eprintln!("error: {e}");
        return (&e).into();
    }

    // Stage 3: Resolve strategy source, validate, parse
    let strategy_adapter: Option<FileConfigAdapter>;
    let strategy_config: &dyn ConfigPort = if let Some(strat_path) = strategy_path {
        eprintln!("Loading strategy from {}", strat_path.display());
        strategy_adapter = Some(match load_config(strat_path) {
            Ok(a) => a,
            Err(code) => return code,
        });
        if let Err(e) = validate_strategy_config(strategy_adapter.as_ref().unwrap()) {
            eprintln!("error: {e}");
            return (&e).into();
        }
        strategy_adapter.as_ref().unwrap()
    } else {
        if let Err(e) = validate_strategy_config(&adapter) {
            eprintln!("error: {e}");
            return (&e).into();
        }
        &adapter
    };

    let strategy = match build_strategy(strategy_config) {
        Ok(s) => s,
        Err(code) => return code,
    };
    eprintln!("Loading strategy: {}", strategy.name);

    // Stage 4: Build BacktestConfig
    let bt_config = match build_backtest_config(&adapter) {
        Ok(c) => c,
        Err(e) => {
            eprintln!("error: {e}");
            return (&e).into();
        }
    };

    // Stage 5: Resolve codes and exchange
    let codes = resolve_codes(code_override, &adapter);
    if codes.is_empty() {
        eprintln!("error: no codes configured");
        return ExitCode::from(2);
    }

    let exchange = match exchange_override {
        Some(e) => e.to_string(),
        None => match adapter.get_string("backtest", "exchange") {
            Some(e) => e,
            None => {
                eprintln!("error: exchange is required");
                return ExitCode::from(2);
            }
        },
    };

    eprintln!("Validating {} codes on {}...", codes.len(), exchange);

    // Read template path before entering feature-gated block
    let template_path = adapter.get_string("report", "template_path");

    // Stages 6-11: Data port dependent pipeline
    #[cfg(feature = "postgres")]
    {
        use crate::adapters::postgres_adapter::PostgresAdapter;

        let data_port = match PostgresAdapter::from_config(&adapter) {
            Ok(a) => a,
            Err(e) => {
                eprintln!("error: {e}");
                return (&e).into();
            }
        };

        run_backtest_pipeline(
            &data_port,
            &strategy,
            &bt_config,
            &codes,
            &exchange,
            output_path,
            template_path.as_deref(),
        )
    }

    #[cfg(not(feature = "postgres"))]
    {
        let _ = (
            &strategy,
            &bt_config,
            &codes,
            &exchange,
            output_path,
            template_path,
        );
        eprintln!("error: postgres feature is required for backtest");
        ExitCode::from(1)
    }
}

pub fn build_backtest_config(adapter: &dyn ConfigPort) -> Result<BacktestConfig, SamtraderError> {
    let start_str = adapter
        .get_string("backtest", "start_date")
        .ok_or_else(|| SamtraderError::ConfigMissing {
            section: "backtest".into(),
            key: "start_date".into(),
        })?;
    let end_str = adapter.get_string("backtest", "end_date").ok_or_else(|| {
        SamtraderError::ConfigMissing {
            section: "backtest".into(),
            key: "end_date".into(),
        }
    })?;

    let start_date = NaiveDate::parse_from_str(&start_str, "%Y-%m-%d").map_err(|_| {
        SamtraderError::ConfigInvalid {
            section: "backtest".into(),
            key: "start_date".into(),
            reason: "invalid date format (expected YYYY-MM-DD)".into(),
        }
    })?;
    let end_date = NaiveDate::parse_from_str(&end_str, "%Y-%m-%d").map_err(|_| {
        SamtraderError::ConfigInvalid {
            section: "backtest".into(),
            key: "end_date".into(),
            reason: "invalid date format (expected YYYY-MM-DD)".into(),
        }
    })?;

    Ok(BacktestConfig {
        start_date,
        end_date,
        initial_capital: adapter.get_double("backtest", "initial_capital", 100_000.0),
        commission_per_trade: adapter.get_double("backtest", "commission_per_trade", 0.0),
        commission_pct: adapter.get_double("backtest", "commission_pct", 0.0),
        slippage_pct: adapter.get_double("backtest", "slippage_pct", 0.0),
        allow_shorting: adapter.get_bool("backtest", "allow_shorting", false),
        risk_free_rate: adapter.get_double("backtest", "risk_free_rate", 0.05),
    })
}

pub fn build_strategy(adapter: &dyn ConfigPort) -> Result<Strategy, ExitCode> {
    let name = adapter
        .get_string("strategy", "name")
        .unwrap_or_else(|| "Unnamed".to_string());
    let description = adapter
        .get_string("strategy", "description")
        .unwrap_or_default();

    let entry_long_str = adapter
        .get_string("strategy", "entry_long")
        .unwrap_or_default();
    let exit_long_str = adapter
        .get_string("strategy", "exit_long")
        .unwrap_or_default();

    let entry_long = match rule_parser::parse(&entry_long_str) {
        Ok(r) => r,
        Err(e) => {
            eprintln!(
                "error: failed to parse entry_long:\n{}",
                e.display_with_context(&entry_long_str)
            );
            return Err(ExitCode::from(4));
        }
    };
    let exit_long = match rule_parser::parse(&exit_long_str) {
        Ok(r) => r,
        Err(e) => {
            eprintln!(
                "error: failed to parse exit_long:\n{}",
                e.display_with_context(&exit_long_str)
            );
            return Err(ExitCode::from(4));
        }
    };

    let entry_short = match adapter
        .get_string("strategy", "entry_short")
        .filter(|s| !s.trim().is_empty())
    {
        Some(s) => match rule_parser::parse(&s) {
            Ok(r) => Some(r),
            Err(e) => {
                eprintln!(
                    "error: failed to parse entry_short:\n{}",
                    e.display_with_context(&s)
                );
                return Err(ExitCode::from(4));
            }
        },
        None => None,
    };

    let exit_short = match adapter
        .get_string("strategy", "exit_short")
        .filter(|s| !s.trim().is_empty())
    {
        Some(s) => match rule_parser::parse(&s) {
            Ok(r) => Some(r),
            Err(e) => {
                eprintln!(
                    "error: failed to parse exit_short:\n{}",
                    e.display_with_context(&s)
                );
                return Err(ExitCode::from(4));
            }
        },
        None => None,
    };

    Ok(Strategy {
        name,
        description,
        entry_long,
        exit_long,
        entry_short,
        exit_short,
        position_size: adapter.get_double("strategy", "position_size", 0.25),
        stop_loss_pct: adapter.get_double("strategy", "stop_loss", 0.0),
        take_profit_pct: adapter.get_double("strategy", "take_profit", 0.0),
        max_positions: adapter.get_int("strategy", "max_positions", 1) as usize,
    })
}

pub fn collect_all_indicators(strategy: &Strategy) -> Vec<IndicatorType> {
    let mut indicators = extract_indicators(&strategy.entry_long);
    indicators.extend(extract_indicators(&strategy.exit_long));
    if let Some(ref rule) = strategy.entry_short {
        indicators.extend(extract_indicators(rule));
    }
    if let Some(ref rule) = strategy.exit_short {
        indicators.extend(extract_indicators(rule));
    }
    indicators.into_iter().collect()
}

pub fn run_backtest_pipeline(
    data_port: &dyn crate::ports::data_port::DataPort,
    strategy: &Strategy,
    bt_config: &BacktestConfig,
    codes: &[String],
    exchange: &str,
    output_path: Option<&PathBuf>,
    template_path: Option<&str>,
) -> ExitCode {
    // Stage 6: Validate universe
    let validation = match validate_universe(
        data_port,
        codes.to_vec(),
        exchange,
        bt_config.start_date,
        bt_config.end_date,
    ) {
        Ok(v) => v,
        Err(e) => {
            eprintln!("error: {e}");
            return (&e).into();
        }
    };

    let valid_codes = &validation.universe.codes;

    // Stage 7: Fetch OHLCV data and compute indicators
    let indicator_types = collect_all_indicators(strategy);
    let mut code_data_vec: Vec<CodeData> = Vec::with_capacity(valid_codes.len());

    for code in valid_codes {
        let ohlcv =
            match data_port.fetch_ohlcv(code, exchange, bt_config.start_date, bt_config.end_date) {
                Ok(bars) => bars,
                Err(e) => {
                    eprintln!("warning: skipping {} ({})", code, e);
                    continue;
                }
            };

        let indicators = compute_indicators(&ohlcv, &indicator_types);
        let mut cd = CodeData::new(code.clone(), exchange.to_string(), ohlcv);
        cd.indicators = indicators;
        code_data_vec.push(cd);
    }

    if code_data_vec.is_empty() {
        eprintln!("error: no valid codes with data to backtest");
        return ExitCode::from(5);
    }

    // Stage 8: Build timeline and run backtest
    let timeline = build_unified_timeline(&code_data_vec);

    eprintln!(
        "Running backtest: {} codes, {} to {}",
        code_data_vec.len(),
        bt_config.start_date,
        bt_config.end_date,
    );
    eprintln!("  Processing: {} dates", timeline.len());

    let result = backtest_engine::run_backtest(&code_data_vec, &timeline, strategy, bt_config);

    // Stage 9: Compute metrics
    let metrics = Metrics::compute(&result.portfolio, bt_config.risk_free_rate);
    let code_results = CodeResult::compute_per_code(&result.portfolio.closed_trades);

    // Stage 10: Print console summary to stderr
    eprintln!("\n=== Aggregate Results ===");
    eprintln!("Total Return:     {:.2}%", metrics.total_return * 100.0);
    eprintln!(
        "Annualized:       {:.2}%",
        metrics.annualized_return * 100.0
    );
    eprintln!("Sharpe Ratio:     {:.2}", metrics.sharpe_ratio);
    eprintln!("Sortino Ratio:    {:.2}", metrics.sortino_ratio);
    eprintln!("Max Drawdown:     -{:.1}%", metrics.max_drawdown * 100.0);
    eprintln!("Total Trades:     {}", metrics.total_trades);
    eprintln!("Win Rate:         {:.1}%", metrics.win_rate * 100.0);
    eprintln!("Profit Factor:    {:.2}", metrics.profit_factor);

    if !code_results.is_empty() {
        eprintln!("\n=== Per-Code Summary ===");
        for cr in &code_results {
            let pnl_sign = if cr.total_pnl >= 0.0 { "+" } else { "" };
            eprintln!(
                "  {}:  {} trades, {:.1}% win rate, {}${:.0}",
                cr.code,
                cr.total_trades,
                cr.win_rate * 100.0,
                pnl_sign,
                cr.total_pnl,
            );
        }
    }

    // Stage 11: Generate report
    let output = output_path
        .cloned()
        .unwrap_or_else(|| PathBuf::from("report.typ"));

    let template_content: String;
    let template: &str = match template_path {
        Some(path) => {
            template_content = match fs::read_to_string(path) {
                Ok(content) => content,
                Err(e) => {
                    eprintln!("error: failed to read template {}: {}", path, e);
                    return ExitCode::from(1);
                }
            };
            &template_content
        }
        None => default_template::template(),
    };

    let ctx = typst_report::ReportContext {
        strategy,
        result: &result,
        metrics: &metrics,
        code_results: if code_results.is_empty() {
            None
        } else {
            Some(&code_results)
        },
        start_date: bt_config.start_date,
        end_date: bt_config.end_date,
        initial_capital: bt_config.initial_capital,
    };

    let typst_content = typst_report::resolve(template, &ctx);

    match fs::write(&output, &typst_content) {
        Ok(()) => {
            eprintln!("\nReport written to: {}", output.display());
            ExitCode::SUCCESS
        }
        Err(e) => {
            eprintln!("error: failed to write report: {e}");
            ExitCode::from(1)
        }
    }
}

pub fn run_dry_run(config_path: &PathBuf) -> ExitCode {
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

    eprintln!("\nStrategy rules (parsed):");
    eprintln!("  entry_long: {}", entry_rule);
    eprintln!("  exit_long:  {}", exit_rule);

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
        let _ = config;
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

    if let Some(entry_short) = adapter
        .get_string("strategy", "entry_short")
        .filter(|s| !s.trim().is_empty())
    {
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

    if let Some(exit_short) = adapter
        .get_string("strategy", "exit_short")
        .filter(|s| !s.trim().is_empty())
    {
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
        let _ = (codes, exchange);
        eprintln!("error: postgres feature is required for info");
        ExitCode::from(1)
    }
}

pub fn resolve_codes(code_override: Option<&str>, config: &dyn ConfigPort) -> Vec<String> {
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

fn run_serve(config_path: &PathBuf) -> ExitCode {
    #[cfg(feature = "web")]
    {
        use crate::adapters::sqlite_adapter::SqliteAdapter;
        use crate::adapters::web::build_router;
        use std::net::SocketAddr;
        use std::sync::Arc;

        eprintln!("Loading config from {}", config_path.display());
        let config = match load_config(config_path) {
            Ok(c) => c,
            Err(code) => return code,
        };

        let data_port = match SqliteAdapter::from_config(&config) {
            Ok(a) => Arc::new(a) as Arc<dyn crate::ports::data_port::DataPort + Send + Sync>,
            Err(e) => {
                eprintln!("error: {e}");
                return ExitCode::from(1);
            }
        };

        let addr: SocketAddr = config
            .get_string("web", "listen")
            .unwrap_or_else(|| "127.0.0.1:3000".to_string())
            .parse()
            .unwrap_or_else(|_| "127.0.0.1:3000".parse().unwrap());

        eprintln!("Starting web server on {}", addr);

        let state = crate::adapters::web::AppState {
            data_port,
            config: Arc::new(config),
        };

        let router = build_router(state);

        tokio::runtime::Runtime::new()
            .unwrap()
            .block_on(async {
                let listener = tokio::net::TcpListener::bind(addr).await.unwrap();
                axum::serve(listener, router).await.unwrap();
            });

        ExitCode::SUCCESS
    }

    #[cfg(not(feature = "web"))]
    {
        let _ = config_path;
        eprintln!("error: web feature is required for serve");
        ExitCode::from(1)
    }
}

fn run_hash_password() -> ExitCode {
    #[cfg(feature = "web")]
    {
        use argon2::{Algorithm, Argon2, Params, PasswordHasher, Version, password_hash::SaltString};
        use std::io::{self, BufRead};
        use rand::rngs::OsRng;

        eprintln!("Enter password to hash:");
        let stdin = io::stdin();
        let password = stdin.lock().lines().next().unwrap_or(Ok(String::new())).unwrap();

        let salt = SaltString::generate(&mut OsRng);
        let argon2 = Argon2::new(Algorithm::Argon2id, Version::V0x13, Params::default());
        let hash = argon2.hash_password(password.as_bytes(), &salt).unwrap();

        println!("{}", hash);
        ExitCode::SUCCESS
    }

    #[cfg(not(feature = "web"))]
    {
        eprintln!("error: web feature is required for hash-password");
        ExitCode::from(1)
    }
}
