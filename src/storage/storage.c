#include "sqlparser/storage/storage.h"

#include "sqlparser/common/util.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int append_character(char **buffer, size_t *length, size_t *capacity, char value) {
    char *new_buffer;
    size_t new_capacity;

    if (*length + 1 >= *capacity) {
        new_capacity = *capacity == 0 ? 16 : *capacity * 2;
        new_buffer = (char *)realloc(*buffer, new_capacity);
        if (new_buffer == NULL) {
            return 0;
        }

        *buffer = new_buffer;
        *capacity = new_capacity;
    }

    (*buffer)[*length] = value;
    (*length)++;
    (*buffer)[*length] = '\0';
    return 1;
}

static FILE *open_table_file(const char *data_dir, const char *table_name, const char *mode, char *error, size_t error_size) {
    char *path = build_path(data_dir, table_name, ".csv");
    FILE *file;

    if (path == NULL) {
        snprintf(error, error_size, "out of memory while building table path");
        return NULL;
    }

    file = fopen(path, mode);
    if (file == NULL) {
        format_system_error(error, error_size, "failed to open table file", path);
        free(path);
        return NULL;
    }

    free(path);

    return file;
}

int csv_parse_line(const char *line, StringList *fields, char *error, size_t error_size) {
    int in_quotes = 0;
    int just_closed_quote = 0;
    char *buffer = NULL;
    size_t length = 0;
    size_t capacity = 0;
    const char *cursor;

    for (cursor = line; ; cursor++) {
        char current = *cursor;
        char *field_text;

        if (in_quotes) {
            if (current == '"') {
                if (cursor[1] == '"') {
                    if (!append_character(&buffer, &length, &capacity, '"')) {
                        free(buffer);
                        snprintf(error, error_size, "out of memory while parsing CSV");
                        return 0;
                    }
                    cursor++;
                } else {
                    in_quotes = 0;
                    just_closed_quote = 1;
                }
            } else if (current == '\0') {
                free(buffer);
                snprintf(error, error_size, "unterminated quoted CSV field");
                return 0;
            } else if (!append_character(&buffer, &length, &capacity, current)) {
                free(buffer);
                snprintf(error, error_size, "out of memory while parsing CSV");
                return 0;
            }
            continue;
        }

        if (current == '\0' || current == ',') {
            if (buffer == NULL) {
                buffer = copy_string("");
                if (buffer == NULL) {
                    snprintf(error, error_size, "out of memory while parsing CSV");
                    return 0;
                }
            }

            field_text = just_closed_quote ? buffer : trim_whitespace(buffer);
            if (!string_list_push(fields, field_text)) {
                free(buffer);
                snprintf(error, error_size, "out of memory while parsing CSV");
                return 0;
            }

            free(buffer);
            buffer = NULL;
            length = 0;
            capacity = 0;
            just_closed_quote = 0;

            if (current == '\0') {
                break;
            }
            continue;
        }

        if (current == '"' && buffer == NULL) {
            in_quotes = 1;
            continue;
        }

        if (just_closed_quote && current != ' ' && current != '\t') {
            free(buffer);
            snprintf(error, error_size, "unexpected character after quoted CSV field");
            return 0;
        }

        if (current != '\r' && current != '\n') {
            if (!append_character(&buffer, &length, &capacity, current)) {
                free(buffer);
                snprintf(error, error_size, "out of memory while parsing CSV");
                return 0;
            }
        }
    }

    return 1;
}

char *csv_escape_field(const char *value) {
    int needs_quotes = 0;
    size_t index;
    size_t extra_quotes = 0;
    size_t length = strlen(value);
    char *escaped;
    size_t write_index = 0;

    if (length == 0) {
        return copy_string("\"\"");
    }

    for (index = 0; index < length; index++) {
        if (value[index] == '"' || value[index] == ',' || value[index] == '\n' || value[index] == '\r') {
            needs_quotes = 1;
        }
        if (value[index] == '"') {
            extra_quotes++;
        }
    }

    if (!needs_quotes) {
        return copy_string(value);
    }

    escaped = (char *)malloc(length + extra_quotes + 3);
    if (escaped == NULL) {
        return NULL;
    }

    escaped[write_index++] = '"';
    for (index = 0; index < length; index++) {
        if (value[index] == '"') {
            escaped[write_index++] = '"';
        }
        escaped[write_index++] = value[index];
    }
    escaped[write_index++] = '"';
    escaped[write_index] = '\0';
    return escaped;
}

StorageResult append_row_csv(const char *data_dir, const char *table_name, const StringList *row_values) {
    StorageResult result = {0};
    FILE *file = open_table_file(data_dir, table_name, "r+b", result.message, sizeof(result.message));
    int index;
    long file_size;
    int last_character;

    result.row_offset = -1;
    if (file == NULL) {
        return result;
    }

    if (fseek(file, 0, SEEK_END) != 0) {
        fclose(file);
        snprintf(result.message, sizeof(result.message), "failed to seek table file");
        return result;
    }

    file_size = ftell(file);
    if (file_size < 0) {
        fclose(file);
        snprintf(result.message, sizeof(result.message), "failed to measure table file");
        return result;
    }

    if (file_size > 0) {
        if (fseek(file, -1L, SEEK_END) != 0) {
            fclose(file);
            snprintf(result.message, sizeof(result.message), "failed to inspect table file");
            return result;
        }

        last_character = fgetc(file);
        if (fseek(file, 0, SEEK_END) != 0) {
            fclose(file);
            snprintf(result.message, sizeof(result.message), "failed to rewind table file");
            return result;
        }

        if (last_character != '\n') {
            if (fputc('\n', file) == EOF) {
                fclose(file);
                snprintf(result.message, sizeof(result.message), "failed to write line separator");
                return result;
            }
        }
    }

    result.row_offset = ftell(file);
    if (result.row_offset < 0) {
        fclose(file);
        snprintf(result.message, sizeof(result.message), "failed to determine row offset");
        return result;
    }

    for (index = 0; index < row_values->count; index++) {
        char *escaped = csv_escape_field(row_values->items[index]);
        if (escaped == NULL) {
            fclose(file);
            snprintf(result.message, sizeof(result.message), "out of memory while writing CSV row");
            return result;
        }

        if (index > 0 && fputc(',', file) == EOF) {
            free(escaped);
            fclose(file);
            snprintf(result.message, sizeof(result.message), "failed to write CSV separator");
            return result;
        }

        if (fputs(escaped, file) == EOF) {
            free(escaped);
            fclose(file);
            snprintf(result.message, sizeof(result.message), "failed to write CSV field");
            return result;
        }

        free(escaped);
    }

    fclose(file);
    result.ok = 1;
    result.affected_rows = 1;
    snprintf(result.message, sizeof(result.message), "INSERT 1");
    return result;
}

StorageReadResult read_row_at_offset_csv(const char *data_dir, const char *table_name, long row_offset) {
    StorageReadResult result = {0};
    FILE *file = open_table_file(data_dir, table_name, "rb", result.message, sizeof(result.message));
    char line[4096];

    if (file == NULL) {
        return result;
    }

    if (fseek(file, row_offset, SEEK_SET) != 0) {
        fclose(file);
        snprintf(result.message, sizeof(result.message), "failed to seek row offset");
        return result;
    }

    if (fgets(line, sizeof(line), file) == NULL) {
        fclose(file);
        snprintf(result.message, sizeof(result.message), "failed to read row at offset");
        return result;
    }

    fclose(file);
    strip_line_endings(line);
    if (line[0] == '\0') {
        snprintf(result.message, sizeof(result.message), "row at offset is empty");
        return result;
    }

    if (!csv_parse_line(line, &result.fields, result.message, sizeof(result.message))) {
        return result;
    }

    result.ok = 1;
    return result;
}

int scan_rows_csv(const char *data_dir, const char *table_name, StorageRowVisitor visitor, void *context, char *error, size_t error_size) {
    FILE *file = open_table_file(data_dir, table_name, "rb", error, error_size);
    char line[4096];

    if (file == NULL) {
        return 0;
    }

    if (fgets(line, sizeof(line), file) == NULL) {
        fclose(file);
        snprintf(error, error_size, "table data file is empty");
        return 0;
    }

    while (1) {
        long row_offset = ftell(file);
        StringList fields = {0};

        if (row_offset < 0) {
            fclose(file);
            snprintf(error, error_size, "failed to determine row offset");
            return 0;
        }

        if (fgets(line, sizeof(line), file) == NULL) {
            break;
        }

        strip_line_endings(line);
        if (line[0] == '\0') {
            continue;
        }

        if (!csv_parse_line(line, &fields, error, error_size)) {
            fclose(file);
            return 0;
        }

        if (!visitor(&fields, row_offset, context, error, error_size)) {
            string_list_free(&fields);
            fclose(file);
            return 0;
        }

        string_list_free(&fields);
    }

    fclose(file);
    return 1;
}
