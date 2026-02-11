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

#include <stdio.h>
#include <stdlib.h>

#include <samrena.h>

#include "samtrader/adapters/postgres_adapter.h"
#include "samtrader/ports/data_port.h"

#define ASSERT(cond, msg)                                                                          \
  do {                                                                                             \
    if (!(cond)) {                                                                                 \
      printf("FAIL: %s\n", msg);                                                                   \
      return 1;                                                                                    \
    }                                                                                              \
  } while (0)

static int test_create_null_arena(void) {
  printf("Testing postgres adapter with NULL arena...\n");

  SamtraderDataPort *port = samtrader_postgres_adapter_create(NULL, "host=localhost dbname=test");
  ASSERT(port == NULL, "Should return NULL when arena is NULL");

  printf("  PASS\n");
  return 0;
}

static int test_create_null_conninfo(void) {
  printf("Testing postgres adapter with NULL conninfo...\n");

  Samrena *arena = samrena_create_default();
  ASSERT(arena != NULL, "Failed to create arena");

  SamtraderDataPort *port = samtrader_postgres_adapter_create(arena, NULL);
  ASSERT(port == NULL, "Should return NULL when conninfo is NULL");

  samrena_destroy(arena);
  printf("  PASS\n");
  return 0;
}

static int test_create_invalid_conninfo(void) {
  printf("Testing postgres adapter with invalid conninfo...\n");

  Samrena *arena = samrena_create_default();
  ASSERT(arena != NULL, "Failed to create arena");

  SamtraderDataPort *port =
      samtrader_postgres_adapter_create(arena, "host=invalid_host_that_does_not_exist port=99999");
  ASSERT(port == NULL, "Should return NULL when connection fails");

  samrena_destroy(arena);
  printf("  PASS\n");
  return 0;
}

static int test_port_interface_populated(void) {
  printf("Testing postgres adapter port interface (live DB)...\n");

  const char *conninfo = getenv("SAMTRADER_TEST_PG_CONNINFO");
  if (conninfo == NULL) {
    printf("  SKIP (SAMTRADER_TEST_PG_CONNINFO not set)\n");
    return 0;
  }

  Samrena *arena = samrena_create_default();
  ASSERT(arena != NULL, "Failed to create arena");

  SamtraderDataPort *port = samtrader_postgres_adapter_create(arena, conninfo);
  ASSERT(port != NULL, "Failed to create postgres adapter with live DB");

  ASSERT(port->fetch_ohlcv != NULL, "fetch_ohlcv function pointer should be set");
  ASSERT(port->list_symbols != NULL, "list_symbols function pointer should be set");
  ASSERT(port->close != NULL, "close function pointer should be set");

  port->close(port);
  samrena_destroy(arena);
  printf("  PASS\n");
  return 0;
}

int main(void) {
  printf("=== PostgreSQL Adapter Tests ===\n\n");

  int failures = 0;

  failures += test_create_null_arena();
  failures += test_create_null_conninfo();
  failures += test_create_invalid_conninfo();
  failures += test_port_interface_populated();

  printf("\n=== Results: %d failures ===\n", failures);

  return failures > 0 ? EXIT_FAILURE : EXIT_SUCCESS;
}
