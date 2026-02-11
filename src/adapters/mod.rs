//! Concrete adapter implementations for ports (TRD Section 2.2).

#[cfg(feature = "postgres")]
pub mod postgres_adapter;
pub mod csv_adapter;
pub mod file_config_adapter;
pub mod typst_report;
