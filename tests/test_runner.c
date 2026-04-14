#include "sqlparser/common/util.h"
#include "sqlparser/execution/executor.h"
#include "sqlparser/index/bptree.h"
#include "sqlparser/index/table_index.h"
#include "sqlparser/sql/lexer.h"
#include "sqlparser/sql/parser.h"
#include "sqlparser/storage/schema.h"
#include "sqlparser/storage/storage.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#ifdef _WIN32
#include <direct.h>
#define MAKE_DIR(path) _mkdir(path)
#else
#include <sys/stat.h>
#define MAKE_DIR(path) mkdir(path, 0755)
#endif

static int tests_run = 0;
static int tests_failed = 0;
static int temp_dir_counter = 0;

static void expect_true(int condition, const char *name) {
    tests_run++;
    if (!condition) {
        tests_failed++;
        fprintf(stderr, "[FAIL] %s\n", name);
    } else {
        printf("[PASS] %s\n", name);
    }
}

static void reset_runtime_state(void) {
    table_index_registry_reset();
}

static int write_text_file(const char *path, const char *content) {
    FILE *file = fopen(path, "wb");
    if (file == NULL) {
        return 0;
    }

    fputs(content, file);
    fclose(file);
    return 1;
}

static void build_child_path(char *buffer, size_t size, const char *root, const char *child) {
    snprintf(buffer, size, "%s/%s", root, child);
}

static int create_test_dirs(char *root, size_t root_size, char *schema_dir, size_t schema_size, char *data_dir, size_t data_size) {
    long suffix = (long)time(NULL);
    temp_dir_counter++;

    snprintf(root, root_size, "build/tests/tmp_%ld_%d", suffix, temp_dir_counter);
    build_child_path(schema_dir, schema_size, root, "schema");
    build_child_path(data_dir, data_size, root, "data");

    MAKE_DIR("build");
    MAKE_DIR("build/tests");
    if (MAKE_DIR(root) != 0) {
        return 0;
    }
    if (MAKE_DIR(schema_dir) != 0) {
        return 0;
    }
    if (MAKE_DIR(data_dir) != 0) {
        return 0;
    }

    return 1;
}

static int load_statement(const char *sql, Statement *statement) {
    TokenArray tokens = {0};
    ParseResult result;
    char error[256];

    if (!lex_sql(sql, &tokens, error, sizeof(error))) {
        fprintf(stderr, "lex failed: %s\n", error);
        return 0;
    }

    result = parse_statement(&tokens);
    if (!result.ok) {
        fprintf(stderr, "parse failed: %s\n", result.message);
        free_tokens(&tokens);
        return 0;
    }

    *statement = result.statement;
    free_tokens(&tokens);
    return 1;
}

static void test_bptree_insert_and_search(void) {
    BPlusTree tree;
    char error[256];
    long value;
    int index;

    bptree_init(&tree);
    for (index = 1; index <= 10; index++) {
        expect_true(bptree_insert(&tree, index, (long)(index * 100), error, sizeof(error)), "B+ tree inserts key");
    }

    for (index = 1; index <= 10; index++) {
        expect_true(bptree_search(&tree, index, &value), "B+ tree finds inserted key");
        if (bptree_search(&tree, index, &value)) {
            expect_true(value == (long)(index * 100), "B+ tree returns correct offset");
        }
    }

    expect_true(!bptree_insert(&tree, 5, 999L, error, sizeof(error)), "B+ tree rejects duplicate key");
    bptree_free(&tree);
}

static void test_parser_where(void) {
    TokenArray tokens = {0};
    ParseResult result;
    char error[256];

    expect_true(lex_sql("SELECT name FROM users WHERE age = 20;", &tokens, error, sizeof(error)), "lexer parses SELECT with WHERE");
    result = parse_statement(&tokens);
    expect_true(result.ok, "parser accepts WHERE clause");
    if (result.ok) {
        expect_true(result.statement.as.select_statement.has_where == 1, "parser marks WHERE flag");
        expect_true(strcmp(result.statement.as.select_statement.where_column, "age") == 0, "parser reads WHERE column");
        expect_true(strcmp(result.statement.as.select_statement.where_value, "20") == 0, "parser reads WHERE value");
        free_statement(&result.statement);
    }
    free_tokens(&tokens);
}

static void test_parser_utf8_identifiers(void) {
    TokenArray tokens = {0};
    ParseResult result;
    char error[256];

    expect_true(lex_sql("SELECT department, name FROM 학생;", &tokens, error, sizeof(error)), "lexer parses UTF-8 identifiers");
    result = parse_statement(&tokens);
    expect_true(result.ok, "parser accepts UTF-8 identifiers");
    if (result.ok) {
        expect_true(strcmp(result.statement.as.select_statement.table_name, "학생") == 0, "parser reads UTF-8 table name");
        free_statement(&result.statement);
    }
    free_tokens(&tokens);
}

static void test_schema_loading_with_alias_filename(void) {
    char root[128];
    char schema_dir[160];
    char data_dir[160];
    char schema_path[192];
    char data_path[192];
    SchemaResult result;

    reset_runtime_state();
    expect_true(create_test_dirs(root, sizeof(root), schema_dir, sizeof(schema_dir), data_dir, sizeof(data_dir)), "create alias schema test directories");
    build_child_path(schema_path, sizeof(schema_path), schema_dir, "student.meta");
    build_child_path(data_path, sizeof(data_path), data_dir, "student.csv");
    expect_true(write_text_file(schema_path, "table=학생\ncolumns=id,department,student_number,name,age\n"), "write alias schema meta");
    expect_true(write_text_file(data_path, "id,department,student_number,name,age\n1,컴퓨터공학과,2024001,김민수,20\n"), "write alias schema CSV");

    result = load_schema(schema_dir, data_dir, "학생");
    expect_true(result.ok, "load schema resolves alias table name");
    if (result.ok) {
        expect_true(strcmp(result.schema.storage_name, "student") == 0, "load schema keeps alias storage name");
        free_schema(&result.schema);
    }
}

static void test_insert_auto_id(void) {
    char root[128];
    char schema_dir[160];
    char data_dir[160];
    char schema_path[192];
    char data_path[192];
    char error[256];
    char *csv_text;
    Statement statement = {0};
    ExecResult result;

    reset_runtime_state();
    expect_true(create_test_dirs(root, sizeof(root), schema_dir, sizeof(schema_dir), data_dir, sizeof(data_dir)), "create auto id test directories");
    build_child_path(schema_path, sizeof(schema_path), schema_dir, "users.meta");
    build_child_path(data_path, sizeof(data_path), data_dir, "users.csv");
    expect_true(write_text_file(schema_path, "table=users\ncolumns=id,name,age\n"), "write auto id schema");
    expect_true(write_text_file(data_path, "id,name,age\n"), "write auto id CSV");
    expect_true(load_statement("INSERT INTO users (name) VALUES ('Alice');", &statement), "build auto id INSERT");

    result = execute_statement(&statement, schema_dir, data_dir, stdout);
    expect_true(result.ok, "execute INSERT with auto id");
    csv_text = read_entire_file(data_path, error, sizeof(error));
    expect_true(csv_text != NULL, "read CSV after auto id INSERT");
    if (csv_text != NULL) {
        expect_true(strstr(csv_text, "1,Alice,\"\"") != NULL, "INSERT auto-generates id 1");
        free(csv_text);
    }
    expect_true(table_index_is_loaded("users"), "INSERT loads table index");
    free_statement(&statement);
}

static void test_insert_overrides_user_id(void) {
    char root[128];
    char schema_dir[160];
    char data_dir[160];
    char schema_path[192];
    char data_path[192];
    char error[256];
    char *csv_text;
    Statement statement = {0};
    ExecResult result;

    reset_runtime_state();
    expect_true(create_test_dirs(root, sizeof(root), schema_dir, sizeof(schema_dir), data_dir, sizeof(data_dir)), "create override id test directories");
    build_child_path(schema_path, sizeof(schema_path), schema_dir, "users.meta");
    build_child_path(data_path, sizeof(data_path), data_dir, "users.csv");
    expect_true(write_text_file(schema_path, "table=users\ncolumns=id,name,age\n"), "write override id schema");
    expect_true(write_text_file(data_path, "id,name,age\n1,Bob,21\n"), "write existing CSV row");
    expect_true(load_statement("INSERT INTO users (id, name) VALUES (99, 'Alice');", &statement), "build INSERT with explicit id");

    result = execute_statement(&statement, schema_dir, data_dir, stdout);
    expect_true(result.ok, "execute INSERT with explicit id");
    csv_text = read_entire_file(data_path, error, sizeof(error));
    expect_true(csv_text != NULL, "read CSV after explicit id INSERT");
    if (csv_text != NULL) {
        expect_true(strstr(csv_text, "2,Alice,\"\"") != NULL, "system-managed id overrides user-provided id");
        expect_true(strstr(csv_text, "99,Alice") == NULL, "user-provided id is not stored");
        free(csv_text);
    }
    free_statement(&statement);
}

static void test_select_execution_with_general_where(void) {
    char root[128];
    char schema_dir[160];
    char data_dir[160];
    char schema_path[192];
    char data_path[192];
    char output_path[192];
    char error[256];
    char *output_text;
    Statement statement = {0};
    ExecResult result;
    FILE *output_file;

    reset_runtime_state();
    expect_true(create_test_dirs(root, sizeof(root), schema_dir, sizeof(schema_dir), data_dir, sizeof(data_dir)), "create general WHERE test directories");
    build_child_path(schema_path, sizeof(schema_path), schema_dir, "users.meta");
    build_child_path(data_path, sizeof(data_path), data_dir, "users.csv");
    build_child_path(output_path, sizeof(output_path), root, "select_where_output.txt");
    expect_true(write_text_file(schema_path, "table=users\ncolumns=id,name,age\n"), "write general WHERE schema");
    expect_true(write_text_file(data_path, "id,name,age\n1,Alice,20\n2,Bob,21\n3,Carol,20\n"), "write general WHERE CSV");
    expect_true(load_statement("SELECT name FROM users WHERE age = 20;", &statement), "build general WHERE SELECT");

    output_file = fopen(output_path, "wb");
    expect_true(output_file != NULL, "open output file for general WHERE");
    if (output_file == NULL) {
        free_statement(&statement);
        return;
    }

    result = execute_statement(&statement, schema_dir, data_dir, output_file);
    fclose(output_file);
    expect_true(result.ok, "execute general WHERE SELECT");
    expect_true(result.affected_rows == 2, "general WHERE returns matching row count");
    output_text = read_entire_file(output_path, error, sizeof(error));
    expect_true(output_text != NULL, "read general WHERE output");
    if (output_text != NULL) {
        expect_true(strstr(output_text, "Alice") != NULL, "general WHERE prints first row");
        expect_true(strstr(output_text, "Carol") != NULL, "general WHERE prints second row");
        expect_true(strstr(output_text, "Bob") == NULL, "general WHERE excludes non-matching row");
        free(output_text);
    }
    expect_true(!table_index_is_loaded("users"), "general WHERE does not build id index");
    free_statement(&statement);
}

static void test_select_execution_with_id_index(void) {
    char root[128];
    char schema_dir[160];
    char data_dir[160];
    char schema_path[192];
    char data_path[192];
    char output_path[192];
    char error[256];
    char *output_text;
    Statement statement = {0};
    ExecResult result;
    FILE *output_file;

    reset_runtime_state();
    expect_true(create_test_dirs(root, sizeof(root), schema_dir, sizeof(schema_dir), data_dir, sizeof(data_dir)), "create id WHERE test directories");
    build_child_path(schema_path, sizeof(schema_path), schema_dir, "users.meta");
    build_child_path(data_path, sizeof(data_path), data_dir, "users.csv");
    build_child_path(output_path, sizeof(output_path), root, "select_id_output.txt");
    expect_true(write_text_file(schema_path, "table=users\ncolumns=id,name,age\n"), "write id WHERE schema");
    expect_true(write_text_file(data_path, "id,name,age\n1,Alice,20\n2,Bob,21\n3,Carol,22\n"), "write id WHERE CSV");
    expect_true(load_statement("SELECT name FROM users WHERE id = 2;", &statement), "build id WHERE SELECT");

    output_file = fopen(output_path, "wb");
    expect_true(output_file != NULL, "open output file for id WHERE");
    if (output_file == NULL) {
        free_statement(&statement);
        return;
    }

    result = execute_statement(&statement, schema_dir, data_dir, output_file);
    fclose(output_file);
    expect_true(result.ok, "execute indexed id SELECT");
    expect_true(result.affected_rows == 1, "indexed id SELECT returns one row");
    output_text = read_entire_file(output_path, error, sizeof(error));
    expect_true(output_text != NULL, "read indexed id output");
    if (output_text != NULL) {
        expect_true(strstr(output_text, "Bob") != NULL, "indexed id SELECT prints matching row");
        expect_true(strstr(output_text, "Alice") == NULL, "indexed id SELECT excludes non-matching rows");
        free(output_text);
    }
    expect_true(table_index_is_loaded("users"), "id WHERE builds table index");
    free_statement(&statement);
}

static void test_index_rebuild_after_reset(void) {
    char root[128];
    char schema_dir[160];
    char data_dir[160];
    char schema_path[192];
    char data_path[192];
    char output_path[192];
    char error[256];
    char *output_text;
    Statement statement = {0};
    ExecResult result;
    FILE *output_file;

    reset_runtime_state();
    expect_true(create_test_dirs(root, sizeof(root), schema_dir, sizeof(schema_dir), data_dir, sizeof(data_dir)), "create rebuild test directories");
    build_child_path(schema_path, sizeof(schema_path), schema_dir, "users.meta");
    build_child_path(data_path, sizeof(data_path), data_dir, "users.csv");
    build_child_path(output_path, sizeof(output_path), root, "rebuild_output.txt");
    expect_true(write_text_file(schema_path, "table=users\ncolumns=id,name,age\n"), "write rebuild schema");
    expect_true(write_text_file(data_path, "id,name,age\n1,Alice,20\n2,Bob,21\n"), "write rebuild CSV");
    expect_true(load_statement("SELECT name FROM users WHERE id = 2;", &statement), "build rebuild SELECT");

    output_file = fopen(output_path, "wb");
    expect_true(output_file != NULL, "open rebuild output");
    if (output_file == NULL) {
        free_statement(&statement);
        return;
    }

    result = execute_statement(&statement, schema_dir, data_dir, output_file);
    fclose(output_file);
    expect_true(result.ok, "execute first indexed SELECT");
    expect_true(table_index_is_loaded("users"), "index loaded before reset");

    reset_runtime_state();
    output_file = fopen(output_path, "wb");
    expect_true(output_file != NULL, "reopen rebuild output");
    if (output_file == NULL) {
        free_statement(&statement);
        return;
    }

    result = execute_statement(&statement, schema_dir, data_dir, output_file);
    fclose(output_file);
    expect_true(result.ok, "execute indexed SELECT after reset");
    output_text = read_entire_file(output_path, error, sizeof(error));
    expect_true(output_text != NULL, "read rebuild output");
    if (output_text != NULL) {
        expect_true(strstr(output_text, "Bob") != NULL, "rebuild restores indexed result");
        free(output_text);
    }
    free_statement(&statement);
}

static void test_index_rebuild_after_invalidate(void) {
    char root[128];
    char schema_dir[160];
    char data_dir[160];
    char schema_path[192];
    char data_path[192];
    char output_path[192];
    char error[256];
    char *output_text;
    Statement statement = {0};
    ExecResult result;
    FILE *output_file;

    reset_runtime_state();
    expect_true(create_test_dirs(root, sizeof(root), schema_dir, sizeof(schema_dir), data_dir, sizeof(data_dir)), "create invalidate rebuild test directories");
    build_child_path(schema_path, sizeof(schema_path), schema_dir, "users.meta");
    build_child_path(data_path, sizeof(data_path), data_dir, "users.csv");
    build_child_path(output_path, sizeof(output_path), root, "invalidate_rebuild_output.txt");
    expect_true(write_text_file(schema_path, "table=users\ncolumns=id,name,age\n"), "write invalidate rebuild schema");
    expect_true(write_text_file(data_path, "id,name,age\n1,Alice,20\n2,Bob,21\n"), "write invalidate rebuild CSV");
    expect_true(load_statement("SELECT name FROM users WHERE id = 2;", &statement), "build invalidate rebuild SELECT");

    output_file = fopen(output_path, "wb");
    expect_true(output_file != NULL, "open invalidate rebuild output");
    if (output_file == NULL) {
        free_statement(&statement);
        return;
    }

    result = execute_statement(&statement, schema_dir, data_dir, output_file);
    fclose(output_file);
    expect_true(result.ok, "execute indexed SELECT before invalidate");
    expect_true(table_index_is_loaded("users"), "index loaded before invalidate");

    table_index_invalidate("users");
    expect_true(!table_index_is_loaded("users"), "invalidate unloads table index");

    output_file = fopen(output_path, "wb");
    expect_true(output_file != NULL, "reopen invalidate rebuild output");
    if (output_file == NULL) {
        free_statement(&statement);
        return;
    }

    result = execute_statement(&statement, schema_dir, data_dir, output_file);
    fclose(output_file);
    expect_true(result.ok, "execute indexed SELECT after invalidate");
    expect_true(table_index_is_loaded("users"), "invalidate path rebuilds table index");
    output_text = read_entire_file(output_path, error, sizeof(error));
    expect_true(output_text != NULL, "read invalidate rebuild output");
    if (output_text != NULL) {
        expect_true(strstr(output_text, "Bob") != NULL, "invalidate path returns rebuilt indexed result");
        free(output_text);
    }
    free_statement(&statement);
}

static void test_invalid_where_id_value(void) {
    char root[128];
    char schema_dir[160];
    char data_dir[160];
    char schema_path[192];
    char data_path[192];
    Statement statement = {0};
    ExecResult result;

    reset_runtime_state();
    expect_true(create_test_dirs(root, sizeof(root), schema_dir, sizeof(schema_dir), data_dir, sizeof(data_dir)), "create invalid id WHERE test directories");
    build_child_path(schema_path, sizeof(schema_path), schema_dir, "users.meta");
    build_child_path(data_path, sizeof(data_path), data_dir, "users.csv");
    expect_true(write_text_file(schema_path, "table=users\ncolumns=id,name,age\n"), "write invalid id WHERE schema");
    expect_true(write_text_file(data_path, "id,name,age\n1,Alice,20\n"), "write invalid id WHERE CSV");
    expect_true(load_statement("SELECT * FROM users WHERE id = abc;", &statement), "build invalid id WHERE SELECT");

    result = execute_statement(&statement, schema_dir, data_dir, stdout);
    expect_true(!result.ok, "non-integer id WHERE returns error");
    free_statement(&statement);
}

static void test_invalid_where_id_value_no_header_output(void) {
    char root[128];
    char schema_dir[160];
    char data_dir[160];
    char schema_path[192];
    char data_path[192];
    char output_path[192];
    char error[256];
    char *output_text;
    Statement statement = {0};
    ExecResult result;
    FILE *output_file;

    reset_runtime_state();
    expect_true(create_test_dirs(root, sizeof(root), schema_dir, sizeof(schema_dir), data_dir, sizeof(data_dir)), "create invalid id WHERE output test directories");
    build_child_path(schema_path, sizeof(schema_path), schema_dir, "users.meta");
    build_child_path(data_path, sizeof(data_path), data_dir, "users.csv");
    build_child_path(output_path, sizeof(output_path), root, "invalid_id_output.txt");
    expect_true(write_text_file(schema_path, "table=users\ncolumns=id,name,age\n"), "write invalid id WHERE output schema");
    expect_true(write_text_file(data_path, "id,name,age\n1,Alice,20\n"), "write invalid id WHERE output CSV");
    expect_true(load_statement("SELECT name FROM users WHERE id = abc;", &statement), "build invalid id WHERE output SELECT");

    output_file = fopen(output_path, "wb");
    expect_true(output_file != NULL, "open output file for invalid id WHERE");
    if (output_file == NULL) {
        free_statement(&statement);
        return;
    }

    result = execute_statement(&statement, schema_dir, data_dir, output_file);
    fclose(output_file);
    expect_true(!result.ok, "invalid id WHERE fails before writing output");
    output_text = read_entire_file(output_path, error, sizeof(error));
    expect_true(output_text != NULL, "read invalid id WHERE output");
    if (output_text != NULL) {
        expect_true(output_text[0] == '\0', "invalid id WHERE does not print header");
        free(output_text);
    }
    free_statement(&statement);
}

static void test_invalid_rebuild_data(void) {
    char root[128];
    char schema_dir[160];
    char data_dir[160];
    char schema_path[192];
    char data_path[192];
    Statement statement = {0};
    ExecResult result;

    reset_runtime_state();
    expect_true(create_test_dirs(root, sizeof(root), schema_dir, sizeof(schema_dir), data_dir, sizeof(data_dir)), "create invalid rebuild test directories");
    build_child_path(schema_path, sizeof(schema_path), schema_dir, "users.meta");
    build_child_path(data_path, sizeof(data_path), data_dir, "users.csv");
    expect_true(write_text_file(schema_path, "table=users\ncolumns=id,name,age\n"), "write invalid rebuild schema");
    expect_true(write_text_file(data_path, "id,name,age\n1,Alice,20\n1,Bob,21\n"), "write duplicate id CSV");
    expect_true(load_statement("SELECT * FROM users WHERE id = 1;", &statement), "build duplicate id SELECT");

    result = execute_statement(&statement, schema_dir, data_dir, stdout);
    expect_true(!result.ok, "duplicate id during rebuild returns error");
    free_statement(&statement);
}

static void test_invalid_rebuild_non_integer_id(void) {
    char root[128];
    char schema_dir[160];
    char data_dir[160];
    char schema_path[192];
    char data_path[192];
    Statement statement = {0};
    ExecResult result;

    reset_runtime_state();
    expect_true(create_test_dirs(root, sizeof(root), schema_dir, sizeof(schema_dir), data_dir, sizeof(data_dir)), "create non-integer rebuild test directories");
    build_child_path(schema_path, sizeof(schema_path), schema_dir, "users.meta");
    build_child_path(data_path, sizeof(data_path), data_dir, "users.csv");
    expect_true(write_text_file(schema_path, "table=users\ncolumns=id,name,age\n"), "write non-integer rebuild schema");
    expect_true(write_text_file(data_path, "id,name,age\nabc,Alice,20\n"), "write non-integer id CSV");
    expect_true(load_statement("SELECT * FROM users WHERE id = 1;", &statement), "build non-integer id SELECT");

    result = execute_statement(&statement, schema_dir, data_dir, stdout);
    expect_true(!result.ok, "non-integer id during rebuild returns error");
    free_statement(&statement);
}

static void test_invalid_rebuild_missing_id(void) {
    char root[128];
    char schema_dir[160];
    char data_dir[160];
    char schema_path[192];
    char data_path[192];
    Statement statement = {0};
    ExecResult result;

    reset_runtime_state();
    expect_true(create_test_dirs(root, sizeof(root), schema_dir, sizeof(schema_dir), data_dir, sizeof(data_dir)), "create missing id rebuild test directories");
    build_child_path(schema_path, sizeof(schema_path), schema_dir, "users.meta");
    build_child_path(data_path, sizeof(data_path), data_dir, "users.csv");
    expect_true(write_text_file(schema_path, "table=users\ncolumns=id,name,age\n"), "write missing id rebuild schema");
    expect_true(write_text_file(data_path, "id,name,age\n,Alice,20\n"), "write missing id CSV");
    expect_true(load_statement("SELECT * FROM users WHERE id = 1;", &statement), "build missing id SELECT");

    result = execute_statement(&statement, schema_dir, data_dir, stdout);
    expect_true(!result.ok, "missing id during rebuild returns error");
    free_statement(&statement);
}

static void test_insert_requires_id_column(void) {
    char root[128];
    char schema_dir[160];
    char data_dir[160];
    char schema_path[192];
    char data_path[192];
    Statement statement = {0};
    ExecResult result;

    reset_runtime_state();
    expect_true(create_test_dirs(root, sizeof(root), schema_dir, sizeof(schema_dir), data_dir, sizeof(data_dir)), "create no id INSERT test directories");
    build_child_path(schema_path, sizeof(schema_path), schema_dir, "users.meta");
    build_child_path(data_path, sizeof(data_path), data_dir, "users.csv");
    expect_true(write_text_file(schema_path, "table=users\ncolumns=name,age\n"), "write no id INSERT schema");
    expect_true(write_text_file(data_path, "name,age\n"), "write no id INSERT CSV");
    expect_true(load_statement("INSERT INTO users (name) VALUES ('Alice');", &statement), "build no id INSERT");

    result = execute_statement(&statement, schema_dir, data_dir, stdout);
    expect_true(!result.ok, "INSERT fails when id column is missing");
    free_statement(&statement);
}

static void test_select_where_id_requires_id_column(void) {
    char root[128];
    char schema_dir[160];
    char data_dir[160];
    char schema_path[192];
    char data_path[192];
    Statement statement = {0};
    ExecResult result;

    reset_runtime_state();
    expect_true(create_test_dirs(root, sizeof(root), schema_dir, sizeof(schema_dir), data_dir, sizeof(data_dir)), "create no id SELECT test directories");
    build_child_path(schema_path, sizeof(schema_path), schema_dir, "users.meta");
    build_child_path(data_path, sizeof(data_path), data_dir, "users.csv");
    expect_true(write_text_file(schema_path, "table=users\ncolumns=name,age\n"), "write no id SELECT schema");
    expect_true(write_text_file(data_path, "name,age\nAlice,20\n"), "write no id SELECT CSV");
    expect_true(load_statement("SELECT * FROM users WHERE id = 1;", &statement), "build no id SELECT");

    result = execute_statement(&statement, schema_dir, data_dir, stdout);
    expect_true(!result.ok, "WHERE id fails when id column is missing");
    free_statement(&statement);
}

static void test_csv_escape(void) {
    StringList row = {0};
    char root[128];
    char data_dir[160];
    char schema_dir[160];
    char data_path[192];
    char error[256];
    char *csv_text;
    StorageResult result;

    reset_runtime_state();
    expect_true(create_test_dirs(root, sizeof(root), schema_dir, sizeof(schema_dir), data_dir, sizeof(data_dir)), "create CSV escape test directories");
    build_child_path(data_path, sizeof(data_path), data_dir, "notes.csv");
    expect_true(write_text_file(data_path, "text\n"), "write CSV escape header");
    expect_true(string_list_push(&row, "hello, \"world\""), "prepare CSV escape row");

    result = append_row_csv(data_dir, "notes", &row);
    expect_true(result.ok, "append CSV row with comma and quote");
    csv_text = read_entire_file(data_path, error, sizeof(error));
    expect_true(csv_text != NULL, "read CSV after escape write");
    if (csv_text != NULL) {
        expect_true(strstr(csv_text, "\"hello, \"\"world\"\"\"") != NULL, "CSV writer escapes quote and comma");
        free(csv_text);
    }
    string_list_free(&row);
}

int main(void) {
    test_bptree_insert_and_search();
    test_parser_where();
    test_parser_utf8_identifiers();
    test_schema_loading_with_alias_filename();
    test_insert_auto_id();
    test_insert_overrides_user_id();
    test_select_execution_with_general_where();
    test_select_execution_with_id_index();
    test_index_rebuild_after_reset();
    test_index_rebuild_after_invalidate();
    test_invalid_where_id_value();
    test_invalid_where_id_value_no_header_output();
    test_invalid_rebuild_data();
    test_invalid_rebuild_non_integer_id();
    test_invalid_rebuild_missing_id();
    test_insert_requires_id_column();
    test_select_where_id_requires_id_column();
    test_csv_escape();

    table_index_registry_reset();
    printf("Tests run: %d\n", tests_run);
    printf("Tests failed: %d\n", tests_failed);
    return tests_failed == 0 ? 0 : 1;
}
