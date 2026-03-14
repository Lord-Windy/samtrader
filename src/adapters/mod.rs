//! Concrete adapter implementations for ports (TRD Section 2.2).

pub mod csv_adapter;
pub mod file_config_adapter;
#[cfg(any(feature = "web-sqlite", feature = "web-postgres"))]
pub mod html_report_adapter;
#[cfg(feature = "postgres")]
pub mod postgres_adapter;
#[cfg(feature = "sqlite")]
pub mod sqlite_adapter;
pub mod typst_report;
#[cfg(any(feature = "web-sqlite", feature = "web-postgres"))]
pub mod web;
