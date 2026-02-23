use clap::Parser;
use samtrader::cli::{run, Cli};

fn main() -> std::process::ExitCode {
    run(Cli::parse())
}
