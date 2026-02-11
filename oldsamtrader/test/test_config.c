/*
 * Copyright 2025 Samuel "Lord-Windy" Brown
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <assert.h>
#include <math.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <samrena.h>

#include <samtrader/adapters/file_config_adapter.h>
#include <samtrader/ports/config_port.h>

/* Test helper: write a test config file */
static char *write_test_config(const char *content) {
  static char path[256];
  snprintf(path, sizeof(path), "/tmp/test_config_%d.ini", getpid());

  FILE *f = fopen(path, "w");
  if (f == NULL) {
    return NULL;
  }
  fprintf(f, "%s", content);
  fclose(f);
  return path;
}

/* Test helper: remove test config file */
static void cleanup_test_config(const char *path) {
  if (path != NULL) {
    unlink(path);
  }
}

/* Test helper: compare doubles with tolerance */
static bool double_eq(double a, double b) { return fabs(a - b) < 0.0001; }

/* Test: Create adapter from valid config file */
static void test_create_adapter(void) {
  printf("  test_create_adapter...");

  const char *config = "[database]\n"
                       "host = localhost\n"
                       "port = 5432\n";

  char *path = write_test_config(config);
  assert(path != NULL);

  Samrena *arena = samrena_create_default();
  assert(arena != NULL);

  SamtraderConfigPort *port = samtrader_file_config_adapter_create(arena, path);
  assert(port != NULL);
  assert(port->get_string != NULL);
  assert(port->get_int != NULL);
  assert(port->get_double != NULL);
  assert(port->get_bool != NULL);
  assert(port->close != NULL);

  port->close(port);
  samrena_destroy(arena);
  cleanup_test_config(path);

  printf(" PASSED\n");
}

/* Test: Create adapter with non-existent file */
static void test_create_adapter_file_not_found(void) {
  printf("  test_create_adapter_file_not_found...");

  Samrena *arena = samrena_create_default();
  assert(arena != NULL);

  SamtraderConfigPort *port =
      samtrader_file_config_adapter_create(arena, "/nonexistent/path/config.ini");
  assert(port == NULL);

  samrena_destroy(arena);

  printf(" PASSED\n");
}

/* Test: Create adapter with NULL parameters */
static void test_create_adapter_null_params(void) {
  printf("  test_create_adapter_null_params...");

  Samrena *arena = samrena_create_default();

  SamtraderConfigPort *port1 = samtrader_file_config_adapter_create(NULL, "config.ini");
  assert(port1 == NULL);

  SamtraderConfigPort *port2 = samtrader_file_config_adapter_create(arena, NULL);
  assert(port2 == NULL);

  samrena_destroy(arena);

  printf(" PASSED\n");
}

/* Test: Get string values */
static void test_get_string(void) {
  printf("  test_get_string...");

  const char *config = "[database]\n"
                       "host = localhost\n"
                       "user = testuser\n"
                       "password = secret123\n"
                       "\n"
                       "[backtest]\n"
                       "name = My Strategy\n";

  char *path = write_test_config(config);
  assert(path != NULL);

  Samrena *arena = samrena_create_default();
  SamtraderConfigPort *port = samtrader_file_config_adapter_create(arena, path);
  assert(port != NULL);

  /* Test basic string retrieval */
  const char *host = port->get_string(port, "database", "host");
  assert(host != NULL);
  assert(strcmp(host, "localhost") == 0);

  const char *user = port->get_string(port, "database", "user");
  assert(user != NULL);
  assert(strcmp(user, "testuser") == 0);

  const char *name = port->get_string(port, "backtest", "name");
  assert(name != NULL);
  assert(strcmp(name, "My Strategy") == 0);

  /* Test missing key returns NULL */
  const char *missing = port->get_string(port, "database", "missing_key");
  assert(missing == NULL);

  /* Test missing section returns NULL */
  const char *missing_section = port->get_string(port, "nonexistent", "key");
  assert(missing_section == NULL);

  port->close(port);
  samrena_destroy(arena);
  cleanup_test_config(path);

  printf(" PASSED\n");
}

/* Test: Get integer values */
static void test_get_int(void) {
  printf("  test_get_int...");

  const char *config = "[database]\n"
                       "port = 5432\n"
                       "max_connections = 100\n"
                       "negative = -42\n"
                       "invalid = abc\n"
                       "float_val = 3.14\n";

  char *path = write_test_config(config);
  assert(path != NULL);

  Samrena *arena = samrena_create_default();
  SamtraderConfigPort *port = samtrader_file_config_adapter_create(arena, path);
  assert(port != NULL);

  /* Test basic int retrieval */
  int port_num = port->get_int(port, "database", "port", 0);
  assert(port_num == 5432);

  int max_conn = port->get_int(port, "database", "max_connections", 0);
  assert(max_conn == 100);

  int negative = port->get_int(port, "database", "negative", 0);
  assert(negative == -42);

  /* Test missing key returns default */
  int missing = port->get_int(port, "database", "missing", 999);
  assert(missing == 999);

  /* Test invalid value returns default */
  int invalid = port->get_int(port, "database", "invalid", 111);
  assert(invalid == 111);

  /* Test float value (should fail and return default) */
  int float_val = port->get_int(port, "database", "float_val", 222);
  assert(float_val == 222);

  port->close(port);
  samrena_destroy(arena);
  cleanup_test_config(path);

  printf(" PASSED\n");
}

/* Test: Get double values */
static void test_get_double(void) {
  printf("  test_get_double...");

  const char *config = "[risk]\n"
                       "position_size = 0.1\n"
                       "stop_loss = 5.5\n"
                       "commission = 9.95\n"
                       "integer_val = 100\n"
                       "invalid = xyz\n";

  char *path = write_test_config(config);
  assert(path != NULL);

  Samrena *arena = samrena_create_default();
  SamtraderConfigPort *port = samtrader_file_config_adapter_create(arena, path);
  assert(port != NULL);

  /* Test basic double retrieval */
  double pos_size = port->get_double(port, "risk", "position_size", 0.0);
  assert(double_eq(pos_size, 0.1));

  double stop_loss = port->get_double(port, "risk", "stop_loss", 0.0);
  assert(double_eq(stop_loss, 5.5));

  double commission = port->get_double(port, "risk", "commission", 0.0);
  assert(double_eq(commission, 9.95));

  /* Test integer value works as double */
  double int_as_double = port->get_double(port, "risk", "integer_val", 0.0);
  assert(double_eq(int_as_double, 100.0));

  /* Test missing key returns default */
  double missing = port->get_double(port, "risk", "missing", 1.23);
  assert(double_eq(missing, 1.23));

  /* Test invalid value returns default */
  double invalid = port->get_double(port, "risk", "invalid", 4.56);
  assert(double_eq(invalid, 4.56));

  port->close(port);
  samrena_destroy(arena);
  cleanup_test_config(path);

  printf(" PASSED\n");
}

/* Test: Get boolean values */
static void test_get_bool(void) {
  printf("  test_get_bool...");

  const char *config = "[settings]\n"
                       "enabled = true\n"
                       "disabled = false\n"
                       "yes_val = yes\n"
                       "no_val = no\n"
                       "one_val = 1\n"
                       "zero_val = 0\n"
                       "on_val = on\n"
                       "off_val = off\n"
                       "TRUE_UPPER = TRUE\n"
                       "FALSE_UPPER = FALSE\n"
                       "invalid = maybe\n";

  char *path = write_test_config(config);
  assert(path != NULL);

  Samrena *arena = samrena_create_default();
  SamtraderConfigPort *port = samtrader_file_config_adapter_create(arena, path);
  assert(port != NULL);

  /* Test true values */
  assert(port->get_bool(port, "settings", "enabled", false) == true);
  assert(port->get_bool(port, "settings", "yes_val", false) == true);
  assert(port->get_bool(port, "settings", "one_val", false) == true);
  assert(port->get_bool(port, "settings", "on_val", false) == true);
  assert(port->get_bool(port, "settings", "TRUE_UPPER", false) == true);

  /* Test false values */
  assert(port->get_bool(port, "settings", "disabled", true) == false);
  assert(port->get_bool(port, "settings", "no_val", true) == false);
  assert(port->get_bool(port, "settings", "zero_val", true) == false);
  assert(port->get_bool(port, "settings", "off_val", true) == false);
  assert(port->get_bool(port, "settings", "FALSE_UPPER", true) == false);

  /* Test invalid returns default */
  assert(port->get_bool(port, "settings", "invalid", true) == true);
  assert(port->get_bool(port, "settings", "invalid", false) == false);

  /* Test missing returns default */
  assert(port->get_bool(port, "settings", "missing", true) == true);
  assert(port->get_bool(port, "settings", "missing", false) == false);

  port->close(port);
  samrena_destroy(arena);
  cleanup_test_config(path);

  printf(" PASSED\n");
}

/* Test: Comments are properly ignored */
static void test_comments(void) {
  printf("  test_comments...");

  const char *config = "# This is a comment at the start\n"
                       "[section1]\n"
                       "key1 = value1\n"
                       "# This is a comment\n"
                       "key2 = value2\n"
                       "; This is also a comment\n"
                       "key3 = value3\n";

  char *path = write_test_config(config);
  assert(path != NULL);

  Samrena *arena = samrena_create_default();
  SamtraderConfigPort *port = samtrader_file_config_adapter_create(arena, path);
  assert(port != NULL);

  assert(strcmp(port->get_string(port, "section1", "key1"), "value1") == 0);
  assert(strcmp(port->get_string(port, "section1", "key2"), "value2") == 0);
  assert(strcmp(port->get_string(port, "section1", "key3"), "value3") == 0);

  port->close(port);
  samrena_destroy(arena);
  cleanup_test_config(path);

  printf(" PASSED\n");
}

/* Test: Whitespace handling */
static void test_whitespace(void) {
  printf("  test_whitespace...");

  const char *config = "[  section_with_spaces  ]\n"
                       "  key_with_spaces  =  value_with_spaces  \n"
                       "key=valuenospace\n"
                       "  key2  =  multiple   words  here  \n";

  char *path = write_test_config(config);
  assert(path != NULL);

  Samrena *arena = samrena_create_default();
  SamtraderConfigPort *port = samtrader_file_config_adapter_create(arena, path);
  assert(port != NULL);

  /* Section name should be trimmed */
  const char *val1 = port->get_string(port, "section_with_spaces", "key_with_spaces");
  assert(val1 != NULL);
  assert(strcmp(val1, "value_with_spaces") == 0);

  const char *val2 = port->get_string(port, "section_with_spaces", "key");
  assert(val2 != NULL);
  assert(strcmp(val2, "valuenospace") == 0);

  /* Value with multiple words preserves internal spaces after trim */
  const char *val3 = port->get_string(port, "section_with_spaces", "key2");
  assert(val3 != NULL);
  assert(strcmp(val3, "multiple   words  here") == 0);

  port->close(port);
  samrena_destroy(arena);
  cleanup_test_config(path);

  printf(" PASSED\n");
}

/* Test: Multiple sections */
static void test_multiple_sections(void) {
  printf("  test_multiple_sections...");

  const char *config = "[database]\n"
                       "host = db.example.com\n"
                       "port = 5432\n"
                       "\n"
                       "[backtest]\n"
                       "initial_capital = 100000.0\n"
                       "commission = 9.95\n"
                       "\n"
                       "[strategy]\n"
                       "name = Golden Cross\n"
                       "entry_long = CROSS_ABOVE(SMA(50), SMA(200))\n";

  char *path = write_test_config(config);
  assert(path != NULL);

  Samrena *arena = samrena_create_default();
  SamtraderConfigPort *port = samtrader_file_config_adapter_create(arena, path);
  assert(port != NULL);

  /* Database section */
  assert(strcmp(port->get_string(port, "database", "host"), "db.example.com") == 0);
  assert(port->get_int(port, "database", "port", 0) == 5432);

  /* Backtest section */
  assert(double_eq(port->get_double(port, "backtest", "initial_capital", 0.0), 100000.0));
  assert(double_eq(port->get_double(port, "backtest", "commission", 0.0), 9.95));

  /* Strategy section */
  assert(strcmp(port->get_string(port, "strategy", "name"), "Golden Cross") == 0);
  assert(strcmp(port->get_string(port, "strategy", "entry_long"),
                "CROSS_ABOVE(SMA(50), SMA(200))") == 0);

  port->close(port);
  samrena_destroy(arena);
  cleanup_test_config(path);

  printf(" PASSED\n");
}

/* Test: Empty file */
static void test_empty_file(void) {
  printf("  test_empty_file...");

  const char *config = "";

  char *path = write_test_config(config);
  assert(path != NULL);

  Samrena *arena = samrena_create_default();
  SamtraderConfigPort *port = samtrader_file_config_adapter_create(arena, path);
  assert(port != NULL);

  /* All lookups should return NULL/default */
  assert(port->get_string(port, "any", "key") == NULL);
  assert(port->get_int(port, "any", "key", 42) == 42);
  assert(double_eq(port->get_double(port, "any", "key", 3.14), 3.14));
  assert(port->get_bool(port, "any", "key", true) == true);

  port->close(port);
  samrena_destroy(arena);
  cleanup_test_config(path);

  printf(" PASSED\n");
}

/* Test: Sample config from TRD */
static void test_trd_sample_config(void) {
  printf("  test_trd_sample_config...");

  /* This is the sample config from TRD Section 8.2 */
  const char *config = "[database]\n"
                       "conninfo = postgres://user:password@localhost:5432/samtrader\n"
                       "\n"
                       "[backtest]\n"
                       "initial_capital = 100000.0\n"
                       "commission_per_trade = 9.95\n"
                       "commission_pct = 0.0\n"
                       "slippage_pct = 0.1\n"
                       "allow_shorting = false\n"
                       "\n"
                       "[strategy]\n"
                       "name = My Strategy\n";

  char *path = write_test_config(config);
  assert(path != NULL);

  Samrena *arena = samrena_create_default();
  SamtraderConfigPort *port = samtrader_file_config_adapter_create(arena, path);
  assert(port != NULL);

  /* Database section */
  const char *conninfo = port->get_string(port, "database", "conninfo");
  assert(conninfo != NULL);
  assert(strcmp(conninfo, "postgres://user:password@localhost:5432/samtrader") == 0);

  /* Backtest section */
  assert(double_eq(port->get_double(port, "backtest", "initial_capital", 0.0), 100000.0));
  assert(double_eq(port->get_double(port, "backtest", "commission_per_trade", 0.0), 9.95));
  assert(double_eq(port->get_double(port, "backtest", "commission_pct", 1.0), 0.0));
  assert(double_eq(port->get_double(port, "backtest", "slippage_pct", 0.0), 0.1));
  assert(port->get_bool(port, "backtest", "allow_shorting", true) == false);

  /* Strategy section */
  assert(strcmp(port->get_string(port, "strategy", "name"), "My Strategy") == 0);

  port->close(port);
  samrena_destroy(arena);
  cleanup_test_config(path);

  printf(" PASSED\n");
}

int main(void) {
  printf("Running file config adapter tests...\n");

  test_create_adapter();
  test_create_adapter_file_not_found();
  test_create_adapter_null_params();
  test_get_string();
  test_get_int();
  test_get_double();
  test_get_bool();
  test_comments();
  test_whitespace();
  test_multiple_sections();
  test_empty_file();
  test_trd_sample_config();

  printf("\nAll tests PASSED!\n");
  return 0;
}
