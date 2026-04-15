#ifndef UTIL_H
#define UTIL_H

#include <stddef.h>

typedef struct {
    char **items;
    int count;
    int capacity;
} StringList;

char *read_entire_file(const char *path, char *error, size_t error_size);
char *copy_string(const char *source);
int strings_equal_ignore_case(const char *left, const char *right);
char *trim_whitespace(char *text);
void strip_line_endings(char *text);
int string_list_push(StringList *list, const char *value);
int string_list_index_of(const StringList *list, const char *value);
void string_list_free(StringList *list);
char *build_path(const char *dir, const char *name, const char *extension);
int parse_int_strict(const char *text, int *value);
void format_system_error(char *error, size_t error_size, const char *action, const char *path);

#endif
