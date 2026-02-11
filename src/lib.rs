//! samtrader — systematic trading strategy backtester.
//!
//! Hexagonal architecture: domain logic in [`domain`], port traits in [`ports`],
//! concrete implementations in [`adapters`].

pub mod domain;
pub mod ports;
pub mod adapters;
