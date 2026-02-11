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

#include <samtrader/adapters/file_config_adapter.h>

#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include <samdata/samhashmap.h>

/* Maximum line length in config file */
#define MAX_LINE_LENGTH 4096

/* Maximum key length (section.key format) */
#define MAX_KEY_LENGTH 512

/**
 * @brief Internal implementation data for file config adapter.
 */
typedef struct {
  SamHashMap *config_map; /**< Stores "section.key" -> "value" mappings */
} FileConfigImpl;

/* Forward declarations of port interface functions */
static const char *file_config_get_string(SamtraderConfigPort *port, const char *section,
                                          const char *key);
static int file_config_get_int(SamtraderConfigPort *port, const char *section, const char *key,
                               int default_value);
static double file_config_get_double(SamtraderConfigPort *port, const char *section,
                                     const char *key, double default_value);
static bool file_config_get_bool(SamtraderConfigPort *port, const char *section, const char *key,
                                 bool default_value);
static void file_config_close(SamtraderConfigPort *port);

/**
 * @brief Trim leading and trailing whitespace from a string in place.
 *
 * @param str String to trim (modified in place)
 * @return Pointer to trimmed string (may point into original string)
 */
static char *trim_whitespace(char *str) {
  if (str == NULL) {
    return NULL;
  }

  /* Trim leading whitespace */
  while (isspace((unsigned char)*str)) {
    str++;
  }

  if (*str == '\0') {
    return str;
  }

  /* Trim trailing whitespace */
  char *end = str + strlen(str) - 1;
  while (end > str && isspace((unsigned char)*end)) {
    end--;
  }
  end[1] = '\0';

  return str;
}

/**
 * @brief Build a composite key from section and key name.
 *
 * @param arena Memory arena for allocation
 * @param section Section name
 * @param key Key name
 * @return Arena-allocated composite key "section.key", or NULL on failure
 */
static char *build_composite_key(Samrena *arena, const char *section, const char *key) {
  if (arena == NULL || section == NULL || key == NULL) {
    return NULL;
  }

  size_t section_len = strlen(section);
  size_t key_len = strlen(key);
  size_t total_len = section_len + 1 + key_len + 1; /* section.key\0 */

  char *composite = samrena_push(arena, total_len);
  if (composite == NULL) {
    return NULL;
  }

  memcpy(composite, section, section_len);
  composite[section_len] = '.';
  memcpy(composite + section_len + 1, key, key_len);
  composite[total_len - 1] = '\0';

  return composite;
}

/**
 * @brief Duplicate a string into the arena.
 *
 * @param arena Memory arena for allocation
 * @param str String to duplicate
 * @return Arena-allocated copy of string, or NULL on failure
 */
static char *arena_strdup(Samrena *arena, const char *str) {
  if (arena == NULL || str == NULL) {
    return NULL;
  }

  size_t len = strlen(str) + 1;
  char *copy = samrena_push(arena, len);
  if (copy == NULL) {
    return NULL;
  }

  memcpy(copy, str, len);
  return copy;
}

/**
 * @brief Parse a single line from the INI file.
 *
 * @param arena Memory arena for allocations
 * @param impl Implementation data containing config map
 * @param line Line to parse (will be modified)
 * @param current_section Current section name (updated if line is section header)
 * @param current_section_size Size of current_section buffer
 * @return true on success, false on error
 */
static bool parse_line(Samrena *arena, FileConfigImpl *impl, char *line, char *current_section,
                       size_t current_section_size) {
  /* Trim whitespace */
  char *trimmed = trim_whitespace(line);

  /* Skip empty lines */
  if (*trimmed == '\0') {
    return true;
  }

  /* Skip comment lines */
  if (*trimmed == '#' || *trimmed == ';') {
    return true;
  }

  /* Check for section header [section] */
  if (*trimmed == '[') {
    char *end_bracket = strchr(trimmed, ']');
    if (end_bracket == NULL) {
      /* Invalid section header - missing closing bracket */
      return false;
    }

    /* Extract section name */
    size_t section_len = (size_t)(end_bracket - trimmed - 1);
    if (section_len >= current_section_size) {
      /* Section name too long */
      return false;
    }

    memcpy(current_section, trimmed + 1, section_len);
    current_section[section_len] = '\0';

    /* Trim section name */
    char *section_trimmed = trim_whitespace(current_section);
    if (section_trimmed != current_section) {
      memmove(current_section, section_trimmed, strlen(section_trimmed) + 1);
    }

    return true;
  }

  /* Must be a key=value line */
  char *equals = strchr(trimmed, '=');
  if (equals == NULL) {
    /* Invalid line - no equals sign outside a section is okay, skip it */
    return true;
  }

  /* Skip key=value pairs that appear before any section */
  if (current_section[0] == '\0') {
    return true;
  }

  /* Extract key */
  *equals = '\0';
  char *key = trim_whitespace(trimmed);
  if (*key == '\0') {
    /* Empty key */
    return false;
  }

  /* Extract value */
  char *value = trim_whitespace(equals + 1);

  /* Build composite key */
  char *composite_key = build_composite_key(arena, current_section, key);
  if (composite_key == NULL) {
    return false;
  }

  /* Duplicate value to arena */
  char *value_copy = arena_strdup(arena, value);
  if (value_copy == NULL) {
    return false;
  }

  /* Store in hashmap */
  if (!samhashmap_put(impl->config_map, composite_key, value_copy)) {
    return false;
  }

  return true;
}

/**
 * @brief Parse the INI configuration file.
 *
 * @param arena Memory arena for allocations
 * @param impl Implementation data to populate
 * @param config_path Path to the configuration file
 * @return true on success, false on error
 */
static bool parse_config_file(Samrena *arena, FileConfigImpl *impl, const char *config_path) {
  FILE *file = fopen(config_path, "r");
  if (file == NULL) {
    return false;
  }

  char line[MAX_LINE_LENGTH];
  char current_section[MAX_KEY_LENGTH] = "";
  bool success = true;

  while (fgets(line, sizeof(line), file) != NULL) {
    /* Remove newline if present */
    size_t len = strlen(line);
    if (len > 0 && line[len - 1] == '\n') {
      line[len - 1] = '\0';
      len--;
    }
    if (len > 0 && line[len - 1] == '\r') {
      line[len - 1] = '\0';
    }

    if (!parse_line(arena, impl, line, current_section, sizeof(current_section))) {
      success = false;
      break;
    }
  }

  fclose(file);
  return success;
}

/* ============================================================================
 * Port Interface Implementations
 * ============================================================================ */

static const char *file_config_get_string(SamtraderConfigPort *port, const char *section,
                                          const char *key) {
  if (port == NULL || port->impl == NULL || section == NULL || key == NULL) {
    return NULL;
  }

  FileConfigImpl *impl = (FileConfigImpl *)port->impl;

  /* Build temporary composite key on stack */
  char composite_key[MAX_KEY_LENGTH];
  int written = snprintf(composite_key, sizeof(composite_key), "%s.%s", section, key);
  if (written < 0 || (size_t)written >= sizeof(composite_key)) {
    return NULL;
  }

  return (const char *)samhashmap_get(impl->config_map, composite_key);
}

static int file_config_get_int(SamtraderConfigPort *port, const char *section, const char *key,
                               int default_value) {
  const char *value = file_config_get_string(port, section, key);
  if (value == NULL) {
    return default_value;
  }

  char *endptr;
  errno = 0;
  long result = strtol(value, &endptr, 10);

  /* Check for conversion errors */
  if (errno != 0 || endptr == value || *endptr != '\0') {
    return default_value;
  }

  /* Check for overflow */
  if (result < INT_MIN || result > INT_MAX) {
    return default_value;
  }

  return (int)result;
}

static double file_config_get_double(SamtraderConfigPort *port, const char *section,
                                     const char *key, double default_value) {
  const char *value = file_config_get_string(port, section, key);
  if (value == NULL) {
    return default_value;
  }

  char *endptr;
  errno = 0;
  double result = strtod(value, &endptr);

  /* Check for conversion errors */
  if (errno != 0 || endptr == value || *endptr != '\0') {
    return default_value;
  }

  return result;
}

static bool file_config_get_bool(SamtraderConfigPort *port, const char *section, const char *key,
                                 bool default_value) {
  const char *value = file_config_get_string(port, section, key);
  if (value == NULL) {
    return default_value;
  }

  /* Check for true values (case-insensitive) */
  if (strcasecmp(value, "true") == 0 || strcasecmp(value, "yes") == 0 ||
      strcasecmp(value, "1") == 0 || strcasecmp(value, "on") == 0) {
    return true;
  }

  /* Check for false values (case-insensitive) */
  if (strcasecmp(value, "false") == 0 || strcasecmp(value, "no") == 0 ||
      strcasecmp(value, "0") == 0 || strcasecmp(value, "off") == 0) {
    return false;
  }

  return default_value;
}

static void file_config_close(SamtraderConfigPort *port) {
  if (port == NULL) {
    return;
  }

  /* Note: All memory is arena-allocated, so nothing to free manually.
   * The hashmap and all strings will be freed when the arena is destroyed. */
  (void)port;
}

/* ============================================================================
 * Public API
 * ============================================================================ */

SamtraderConfigPort *samtrader_file_config_adapter_create(Samrena *arena, const char *config_path) {
  if (arena == NULL || config_path == NULL) {
    return NULL;
  }

  /* Allocate port structure */
  SamtraderConfigPort *port = samrena_push(arena, sizeof(SamtraderConfigPort));
  if (port == NULL) {
    return NULL;
  }

  /* Allocate implementation data */
  FileConfigImpl *impl = samrena_push(arena, sizeof(FileConfigImpl));
  if (impl == NULL) {
    return NULL;
  }

  /* Create hashmap for config storage */
  impl->config_map = samhashmap_create(64, arena);
  if (impl->config_map == NULL) {
    return NULL;
  }

  /* Parse the configuration file */
  if (!parse_config_file(arena, impl, config_path)) {
    /* Parsing failed - arena memory will be cleaned up by caller */
    return NULL;
  }

  /* Initialize port structure */
  port->impl = impl;
  port->arena = arena;
  port->get_string = file_config_get_string;
  port->get_int = file_config_get_int;
  port->get_double = file_config_get_double;
  port->get_bool = file_config_get_bool;
  port->close = file_config_close;

  return port;
}
