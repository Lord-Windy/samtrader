# Copyright 2025 Samuel "Lord-Windy" Brown
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

ptah_add_executable(samtrader
    SOURCES
        src/main.c
        src/domain/ohlcv.c
        src/domain/indicator.c
        src/domain/indicator_sma.c
        src/domain/indicator_ema.c
        src/domain/indicator_wma.c
        src/domain/indicator_rsi.c
        src/domain/indicator_bollinger.c
        src/domain/indicator_macd.c
        src/domain/indicator_stochastic.c
        src/domain/indicator_atr.c
        src/domain/indicator_pivot.c
        src/domain/rule.c
        src/domain/rule_eval.c
        src/domain/rule_parser.c
        src/domain/position.c
        src/domain/portfolio.c
        src/domain/execution.c
        src/domain/metrics.c
        src/domain/universe.c
        src/domain/code_data.c
        src/adapters/file_config_adapter.c
        src/adapters/postgres_adapter.c
        src/adapters/typst_report_adapter.c
    DEPENDENCIES
        samrena
        samdata
        PostgreSQL::PostgreSQL
        m
)

target_include_directories(samtrader PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

# Build tests if testing is enabled
if(BUILD_TESTING)
    enable_testing()

    # OHLCV data structure tests
    add_executable(samtrader_ohlcv_test
        test/test_ohlcv.c
        src/domain/ohlcv.c
        src/domain/indicator.c
        src/domain/indicator_sma.c
        src/domain/indicator_ema.c
        src/domain/indicator_wma.c
        src/domain/indicator_rsi.c
        src/domain/indicator_bollinger.c
        src/domain/indicator_macd.c
        src/domain/indicator_stochastic.c
        src/domain/indicator_atr.c
        src/domain/indicator_pivot.c
        src/adapters/postgres_adapter.c
    )
    target_include_directories(samtrader_ohlcv_test PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
    )
    target_link_libraries(samtrader_ohlcv_test PRIVATE samrena samdata PostgreSQL::PostgreSQL m)
    add_test(NAME samtrader_ohlcv_test COMMAND samtrader_ohlcv_test)

    # Config adapter tests
    add_executable(samtrader_config_test
        test/test_config.c
        src/adapters/file_config_adapter.c
    )
    target_include_directories(samtrader_config_test PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
    )
    target_link_libraries(samtrader_config_test PRIVATE samrena samdata)
    add_test(NAME samtrader_config_test COMMAND samtrader_config_test)

    # Indicator data structure tests
    add_executable(samtrader_indicator_test
        test/test_indicator.c
        src/domain/ohlcv.c
        src/domain/indicator.c
        src/domain/indicator_sma.c
        src/domain/indicator_ema.c
        src/domain/indicator_wma.c
        src/domain/indicator_rsi.c
        src/domain/indicator_bollinger.c
        src/domain/indicator_macd.c
        src/domain/indicator_stochastic.c
        src/domain/indicator_atr.c
        src/domain/indicator_pivot.c
    )
    target_include_directories(samtrader_indicator_test PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
    )
    target_link_libraries(samtrader_indicator_test PRIVATE samrena samdata m)
    add_test(NAME samtrader_indicator_test COMMAND samtrader_indicator_test)

    # Indicator calculation tests (SMA, EMA, WMA, RSI, MACD, Stochastic, Bollinger, ATR, Pivot)
    add_executable(samtrader_indicator_calc_test
        test/test_indicator_calc.c
        src/domain/ohlcv.c
        src/domain/indicator.c
        src/domain/indicator_sma.c
        src/domain/indicator_ema.c
        src/domain/indicator_wma.c
        src/domain/indicator_rsi.c
        src/domain/indicator_bollinger.c
        src/domain/indicator_macd.c
        src/domain/indicator_stochastic.c
        src/domain/indicator_atr.c
        src/domain/indicator_pivot.c
    )
    target_include_directories(samtrader_indicator_calc_test PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
    )
    target_link_libraries(samtrader_indicator_calc_test PRIVATE samrena samdata m)
    add_test(NAME samtrader_indicator_calc_test COMMAND samtrader_indicator_calc_test)

    # Rule data structure tests
    add_executable(samtrader_rule_test
        test/test_rule.c
        src/domain/rule.c
    )
    target_include_directories(samtrader_rule_test PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
    )
    target_link_libraries(samtrader_rule_test PRIVATE samrena samdata m)
    add_test(NAME samtrader_rule_test COMMAND samtrader_rule_test)

    # Rule evaluation tests
    add_executable(samtrader_rule_eval_test
        test/test_rule_eval.c
        src/domain/rule.c
        src/domain/rule_eval.c
        src/domain/ohlcv.c
        src/domain/indicator.c
        src/domain/indicator_sma.c
        src/domain/indicator_ema.c
        src/domain/indicator_wma.c
        src/domain/indicator_rsi.c
        src/domain/indicator_bollinger.c
        src/domain/indicator_macd.c
        src/domain/indicator_stochastic.c
        src/domain/indicator_atr.c
        src/domain/indicator_pivot.c
    )
    target_include_directories(samtrader_rule_eval_test PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
    )
    target_link_libraries(samtrader_rule_eval_test PRIVATE samrena samdata m)
    add_test(NAME samtrader_rule_eval_test COMMAND samtrader_rule_eval_test)

    # Position tests
    add_executable(samtrader_position_test
        test/test_position.c
        src/domain/position.c
    )
    target_include_directories(samtrader_position_test PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
    )
    target_link_libraries(samtrader_position_test PRIVATE samrena samdata m)
    add_test(NAME samtrader_position_test COMMAND samtrader_position_test)

    # Portfolio tests
    add_executable(samtrader_portfolio_test
        test/test_portfolio.c
        src/domain/portfolio.c
    )
    target_include_directories(samtrader_portfolio_test PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
    )
    target_link_libraries(samtrader_portfolio_test PRIVATE samrena samdata m)
    add_test(NAME samtrader_portfolio_test COMMAND samtrader_portfolio_test)

    # Execution tests
    add_executable(samtrader_execution_test
        test/test_execution.c
        src/domain/execution.c
        src/domain/portfolio.c
        src/domain/position.c
    )
    target_include_directories(samtrader_execution_test PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
    )
    target_link_libraries(samtrader_execution_test PRIVATE samrena samdata m)
    add_test(NAME samtrader_execution_test COMMAND samtrader_execution_test)

    # Metrics tests
    add_executable(samtrader_metrics_test
        test/test_metrics.c
        src/domain/metrics.c
        src/domain/portfolio.c
    )
    target_include_directories(samtrader_metrics_test PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
    )
    target_link_libraries(samtrader_metrics_test PRIVATE samrena samdata m)
    add_test(NAME samtrader_metrics_test COMMAND samtrader_metrics_test)

    # PostgreSQL adapter tests
    add_executable(samtrader_postgres_test
        test/test_postgres.c
        src/adapters/postgres_adapter.c
        src/domain/ohlcv.c
    )
    target_include_directories(samtrader_postgres_test PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
    )
    target_link_libraries(samtrader_postgres_test PRIVATE samrena samdata PostgreSQL::PostgreSQL m)
    add_test(NAME samtrader_postgres_test COMMAND samtrader_postgres_test)

    # Rule parser tests
    add_executable(samtrader_rule_parser_test
        test/test_rule_parser.c
        src/domain/rule.c
        src/domain/rule_parser.c
    )
    target_include_directories(samtrader_rule_parser_test PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
    )
    target_link_libraries(samtrader_rule_parser_test PRIVATE samrena samdata m)
    add_test(NAME samtrader_rule_parser_test COMMAND samtrader_rule_parser_test)

    # Backtest integration tests
    add_executable(samtrader_backtest_test
        test/test_backtest.c
        src/domain/ohlcv.c
        src/domain/indicator.c
        src/domain/indicator_sma.c
        src/domain/indicator_ema.c
        src/domain/indicator_wma.c
        src/domain/indicator_rsi.c
        src/domain/indicator_bollinger.c
        src/domain/indicator_macd.c
        src/domain/indicator_stochastic.c
        src/domain/indicator_atr.c
        src/domain/indicator_pivot.c
        src/domain/rule.c
        src/domain/rule_eval.c
        src/domain/position.c
        src/domain/portfolio.c
        src/domain/execution.c
        src/domain/metrics.c
        src/domain/code_data.c
        src/domain/universe.c
    )
    target_include_directories(samtrader_backtest_test PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
    )
    target_link_libraries(samtrader_backtest_test PRIVATE samrena samdata m)
    add_test(NAME samtrader_backtest_test COMMAND samtrader_backtest_test)

    # Typst report adapter tests
    add_executable(samtrader_report_test
        test/test_report.c
        src/adapters/typst_report_adapter.c
    )
    target_include_directories(samtrader_report_test PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
    )
    target_link_libraries(samtrader_report_test PRIVATE samrena samdata m)
    add_test(NAME samtrader_report_test COMMAND samtrader_report_test)

    # Universe parsing and validation tests
    add_executable(samtrader_universe_test
        test/test_universe.c
        src/domain/universe.c
        src/domain/ohlcv.c
    )
    target_include_directories(samtrader_universe_test PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
    )
    target_link_libraries(samtrader_universe_test PRIVATE samrena samdata m)
    add_test(NAME samtrader_universe_test COMMAND samtrader_universe_test)

    # Code data loading, date timeline, and indicator pre-computation tests
    add_executable(samtrader_code_data_test
        test/test_code_data.c
        src/domain/code_data.c
        src/domain/ohlcv.c
        src/domain/indicator.c
        src/domain/indicator_sma.c
        src/domain/indicator_ema.c
        src/domain/indicator_wma.c
        src/domain/indicator_rsi.c
        src/domain/indicator_bollinger.c
        src/domain/indicator_macd.c
        src/domain/indicator_stochastic.c
        src/domain/indicator_atr.c
        src/domain/indicator_pivot.c
        src/domain/rule.c
        src/domain/rule_eval.c
        src/domain/rule_parser.c
    )
    target_include_directories(samtrader_code_data_test PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
    )
    target_link_libraries(samtrader_code_data_test PRIVATE samrena samdata m)
    add_test(NAME samtrader_code_data_test COMMAND samtrader_code_data_test)

    # End-to-end pipeline tests
    add_executable(samtrader_e2e_test
        test/test_e2e.c
        src/domain/ohlcv.c
        src/domain/indicator.c
        src/domain/indicator_sma.c src/domain/indicator_ema.c src/domain/indicator_wma.c
        src/domain/indicator_rsi.c src/domain/indicator_bollinger.c src/domain/indicator_macd.c
        src/domain/indicator_stochastic.c src/domain/indicator_atr.c src/domain/indicator_pivot.c
        src/domain/rule.c src/domain/rule_eval.c src/domain/rule_parser.c
        src/domain/position.c src/domain/portfolio.c src/domain/execution.c src/domain/metrics.c
        src/domain/universe.c src/domain/code_data.c
        src/adapters/file_config_adapter.c src/adapters/postgres_adapter.c
        src/adapters/typst_report_adapter.c
    )
    target_include_directories(samtrader_e2e_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
    target_link_libraries(samtrader_e2e_test PRIVATE samrena samdata PostgreSQL::PostgreSQL m)
    add_test(NAME samtrader_e2e_test COMMAND samtrader_e2e_test)
endif()
