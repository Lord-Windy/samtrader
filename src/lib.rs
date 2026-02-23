//! samtrader — systematic trading strategy backtester.
//!
//! Hexagonal architecture: domain logic in [`domain`], port traits in [`ports`],
//! concrete implementations in [`adapters`].

pub mod adapters;
pub mod cli;
pub mod domain;
pub mod ports;
