//! INI file configuration adapter (TRD §11.2, §6.1).

use crate::ports::config_port::ConfigPort;
use configparser::ini::Ini;
use std::path::Path;

pub struct FileConfigAdapter {
    config: Ini,
}

impl FileConfigAdapter {
    pub fn from_file<P: AsRef<Path>>(path: P) -> std::io::Result<Self> {
        let mut config = Ini::new();
        config.load(path).map_err(|e| std::io::Error::other(e))?;
        Ok(Self { config })
    }

    pub fn from_string(content: &str) -> Result<Self, String> {
        let mut config = Ini::new();
        config.read(content.to_string())?;
        Ok(Self { config })
    }

    fn parse_bool(value: &str) -> Option<bool> {
        match value.to_lowercase().as_str() {
            "true" | "yes" | "1" => Some(true),
            "false" | "no" | "0" => Some(false),
            _ => None,
        }
    }
}

impl ConfigPort for FileConfigAdapter {
    fn get_string(&self, section: &str, key: &str) -> Option<String> {
        self.config.get(section, key)
    }

    fn get_int(&self, section: &str, key: &str, default: i64) -> i64 {
        self.config
            .getint(section, key)
            .ok()
            .flatten()
            .unwrap_or(default)
    }

    fn get_double(&self, section: &str, key: &str, default: f64) -> f64 {
        self.config
            .getfloat(section, key)
            .ok()
            .flatten()
            .unwrap_or(default)
    }

    fn get_bool(&self, section: &str, key: &str, default: bool) -> bool {
        self.config
            .get(section, key)
            .as_ref()
            .and_then(|v| Self::parse_bool(v))
            .unwrap_or(default)
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::io::Write;
    use tempfile::NamedTempFile;

    fn create_temp_config(content: &str) -> NamedTempFile {
        let mut file = NamedTempFile::new().unwrap();
        write!(file, "{}", content).unwrap();
        file
    }

    #[test]
    fn from_string_parses_config() {
        let content = r#"
[database]
conninfo = host=localhost dbname=test

[backtest]
initial_capital = 100000.0
commission_per_trade = 10

[strategy]
name = Test Strategy
max_positions = 5
"#;
        let adapter = FileConfigAdapter::from_string(content).unwrap();
        assert_eq!(
            adapter.get_string("database", "conninfo"),
            Some("host=localhost dbname=test".to_string())
        );
        assert_eq!(
            adapter.get_string("strategy", "name"),
            Some("Test Strategy".to_string())
        );
    }

    #[test]
    fn get_string_returns_none_for_missing_key() {
        let adapter =
            FileConfigAdapter::from_string("[backtest]\ninitial_capital = 100\n").unwrap();
        assert_eq!(adapter.get_string("backtest", "missing"), None);
        assert_eq!(adapter.get_string("missing_section", "key"), None);
    }

    #[test]
    fn get_int_returns_value() {
        let adapter =
            FileConfigAdapter::from_string("[backtest]\nmax_positions = 5\n").unwrap();
        assert_eq!(adapter.get_int("backtest", "max_positions", 0), 5);
    }

    #[test]
    fn get_int_returns_default_for_missing() {
        let adapter = FileConfigAdapter::from_string("[backtest]\n").unwrap();
        assert_eq!(adapter.get_int("backtest", "missing", 42), 42);
    }

    #[test]
    fn get_int_returns_default_for_non_numeric() {
        let adapter =
            FileConfigAdapter::from_string("[backtest]\nmax_positions = abc\n").unwrap();
        assert_eq!(adapter.get_int("backtest", "max_positions", 42), 42);
    }

    #[test]
    fn get_double_returns_value() {
        let adapter =
            FileConfigAdapter::from_string("[backtest]\ninitial_capital = 100000.5\n").unwrap();
        assert_eq!(
            adapter.get_double("backtest", "initial_capital", 0.0),
            100000.5
        );
    }

    #[test]
    fn get_double_returns_default_for_missing() {
        let adapter = FileConfigAdapter::from_string("[backtest]\n").unwrap();
        assert_eq!(adapter.get_double("backtest", "missing", 99.9), 99.9);
    }

    #[test]
    fn get_double_returns_default_for_non_numeric() {
        let adapter =
            FileConfigAdapter::from_string("[backtest]\ninitial_capital = not_a_number\n").unwrap();
        assert_eq!(
            adapter.get_double("backtest", "initial_capital", 99.9),
            99.9
        );
    }

    #[test]
    fn get_bool_returns_true_values() {
        let adapter =
            FileConfigAdapter::from_string("[backtest]\na = true\nb = yes\nc = 1\n").unwrap();
        assert!(adapter.get_bool("backtest", "a", false));
        assert!(adapter.get_bool("backtest", "b", false));
        assert!(adapter.get_bool("backtest", "c", false));
    }

    #[test]
    fn get_bool_returns_false_values() {
        let adapter =
            FileConfigAdapter::from_string("[backtest]\na = false\nb = no\nc = 0\n").unwrap();
        assert!(!adapter.get_bool("backtest", "a", true));
        assert!(!adapter.get_bool("backtest", "b", true));
        assert!(!adapter.get_bool("backtest", "c", true));
    }

    #[test]
    fn get_bool_returns_default_for_missing() {
        let adapter = FileConfigAdapter::from_string("[backtest]\n").unwrap();
        assert!(adapter.get_bool("backtest", "missing", true));
        assert!(!adapter.get_bool("backtest", "missing", false));
    }

    #[test]
    fn from_file_reads_config() {
        let content = "[report]\ntemplate_path = /path/to/template.typ\n";
        let file = create_temp_config(content);
        let adapter = FileConfigAdapter::from_file(file.path()).unwrap();
        assert_eq!(
            adapter.get_string("report", "template_path"),
            Some("/path/to/template.typ".to_string())
        );
    }

    #[test]
    fn from_file_returns_error_for_missing_file() {
        let result = FileConfigAdapter::from_file("/nonexistent/path/config.ini");
        assert!(result.is_err());
    }

    #[test]
    fn handles_all_config_sections() {
        let content = r#"
[database]
conninfo = host=localhost

[backtest]
initial_capital = 100000.0
allow_shorting = true

[strategy]
name = Cross Strategy
position_size = 0.25

[report]
template_path = /custom.typ
"#;
        let adapter = FileConfigAdapter::from_string(content).unwrap();

        assert_eq!(
            adapter.get_string("database", "conninfo"),
            Some("host=localhost".to_string())
        );
        assert_eq!(
            adapter.get_double("backtest", "initial_capital", 0.0),
            100000.0
        );
        assert!(adapter.get_bool("backtest", "allow_shorting", false));
        assert_eq!(
            adapter.get_string("strategy", "name"),
            Some("Cross Strategy".to_string())
        );
        assert_eq!(adapter.get_double("strategy", "position_size", 0.0), 0.25);
        assert_eq!(
            adapter.get_string("report", "template_path"),
            Some("/custom.typ".to_string())
        );
    }
}
