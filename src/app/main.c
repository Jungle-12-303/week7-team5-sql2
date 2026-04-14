// AST 구조체를 해제하기 위한 선언입니다.
#include "sqlparser/sql/ast.h"
// 파싱된 SQL을 실제로 실행하기 위한 선언입니다.
#include "sqlparser/execution/executor.h"
// SQL 문자열을 토큰으로 나누기 위한 선언입니다.
#include "sqlparser/sql/lexer.h"
// 토큰 목록을 INSERT / SELECT 구조로 해석하기 위한 선언입니다.
#include "sqlparser/sql/parser.h"
// 파일 읽기와 문자열 복사 같은 공통 유틸 함수를 쓰기 위한 선언입니다.
#include "sqlparser/common/util.h"
#include "sqlparser/index/table_index.h"

#include <ctype.h>
// 표준 입출력과 fopen을 사용합니다.
#include <stdio.h>
// free, malloc 같은 메모리 함수들을 사용합니다.
#include <stdlib.h>
// strlen, memcpy 같은 문자열 함수를 사용합니다.
#include <string.h>

#ifdef _MSC_VER
#include <io.h>
#define ISATTY _isatty
#else
#include <unistd.h>
#define ISATTY isatty
#endif

// 전달받은 인자가 실제 파일 경로인지 간단히 검사합니다.
static int file_exists(const char *path) {
    // 파일을 열어 존재 여부를 확인할 포인터입니다.
    FILE *file;

    // 읽기 모드로 파일을 열어 봅니다.
    file = fopen(path, "rb");
    // 열기에 성공하면 파일이 존재합니다.
    if (file != NULL) {
        fclose(file);
        return 1;
    }

    // 열지 못했으면 파일이 없다고 봅니다.
    return 0;
}

// 표준입력이 터미널에 연결된 대화형 환경인지 확인합니다.
static int stdin_is_interactive(void) {
#ifdef _MSC_VER
    return ISATTY(_fileno(stdin)) != 0;
#else
    return ISATTY(STDIN_FILENO) != 0;
#endif
}

// 공백만 있는 문자열인지 검사합니다.
static int is_blank_string(const char *text) {
    while (*text != '\0') {
        if (!isspace((unsigned char)*text)) {
            return 0;
        }
        text++;
    }

    return 1;
}

// 여러 개로 나뉘어 들어온 명령줄 인자를 하나의 SQL 문자열로 합칩니다.
static char *join_arguments_as_sql(int argc, char *argv[], int start_index, char *error, size_t error_size) {
    // 최종 SQL 문자열에 필요한 전체 길이입니다.
    size_t total_length = 1;
    // 반복문에 사용할 인덱스입니다.
    int index;
    // 최종 SQL 문자열 버퍼입니다.
    char *sql_text;
    // 버퍼의 현재 작성 위치입니다.
    size_t offset = 0;
    // 현재 인자 조각 길이입니다.
    size_t piece_length;

    // 합칠 SQL 인자가 비어 있으면 오류입니다.
    if (start_index >= argc) {
        snprintf(error, error_size, "missing SQL statement");
        return NULL;
    }

    // 모든 인자 길이와 중간 공백 하나를 포함해 필요한 크기를 계산합니다.
    for (index = start_index; index < argc; index++) {
        total_length += strlen(argv[index]);
        if (index + 1 < argc) {
            total_length += 1;
        }
    }

    // 계산된 길이만큼 버퍼를 할당합니다.
    sql_text = (char *)malloc(total_length);
    if (sql_text == NULL) {
        snprintf(error, error_size, "out of memory while building SQL statement");
        return NULL;
    }

    // 빈 문자열로 시작합니다.
    sql_text[0] = '\0';

    // 각 인자를 공백 하나로 이어 붙여 하나의 SQL 문장으로 만듭니다.
    for (index = start_index; index < argc; index++) {
        piece_length = strlen(argv[index]);
        memcpy(sql_text + offset, argv[index], piece_length);
        offset += piece_length;

        if (index + 1 < argc) {
            sql_text[offset] = ' ';
            offset++;
        }
    }

    // C 문자열 종료 표시를 붙입니다.
    sql_text[offset] = '\0';
    return sql_text;
}

// 파일이나 파이프처럼 길이를 알 수 없는 입력 스트림 전체를 메모리로 읽습니다.
static char *read_stream(FILE *stream, char *error, size_t error_size) {
    char chunk[1024];
    char *buffer = NULL;
    size_t length = 0;
    size_t capacity = 0;

    while (!feof(stream)) {
        size_t bytes_read = fread(chunk, 1, sizeof(chunk), stream);

        if (bytes_read > 0) {
            size_t required = length + bytes_read + 1;
            char *new_buffer;
            size_t new_capacity = capacity == 0 ? 1024 : capacity;

            while (new_capacity < required) {
                new_capacity *= 2;
            }

            new_buffer = (char *)realloc(buffer, new_capacity);
            if (new_buffer == NULL) {
                free(buffer);
                snprintf(error, error_size, "out of memory while reading standard input");
                return NULL;
            }

            buffer = new_buffer;
            capacity = new_capacity;
            memcpy(buffer + length, chunk, bytes_read);
            length += bytes_read;
            buffer[length] = '\0';
        }

        if (ferror(stream)) {
            free(buffer);
            snprintf(error, error_size, "failed to read standard input");
            return NULL;
        }
    }

    if (buffer == NULL) {
        buffer = copy_string("");
        if (buffer == NULL) {
            snprintf(error, error_size, "out of memory while reading standard input");
            return NULL;
        }
    }

    return buffer;
}

// 파일 경로인지 SQL 문자열인지 판단해 실제 SQL 본문을 읽어 옵니다.
static char *load_sql_from_argument(const char *value, int force_file, char *error, size_t error_size) {
    if (force_file || file_exists(value)) {
        return read_entire_file(value, error, error_size);
    }

    return copy_string(value);
}

// 사용자에게 보여줄 CLI 사용법을 출력합니다.
static void print_usage(FILE *stream, const char *program_name) {
    fprintf(stream, "Usage: %s [OPTION]... [SQL_OR_FILE]\n", program_name);
    fprintf(stream, "       %s\n", program_name);
    fprintf(stream, "\n");
    fprintf(stream, "Options:\n");
    fprintf(stream, "  -e, --execute SQL   execute a SQL statement\n");
    fprintf(stream, "  -f, --file PATH     execute SQL loaded from PATH\n");
    fprintf(stream, "  -h, --help          show this help message\n");
    fprintf(stream, "\n");
    fprintf(stream, "Interactive mode starts when no arguments are given on a terminal.\n");
    fprintf(stream, "In interactive mode, enter either a SQL statement or a SQL file path.\n");
    fprintf(stream, "Use .exit, .quit, exit, or quit to leave the prompt.\n");
    fprintf(stream, "\n");
    fprintf(stream, "Examples:\n");
    fprintf(stream, "  %s -e \"SELECT * FROM student;\"\n", program_name);
    fprintf(stream, "  %s -f examples/select_students.sql\n", program_name);
    fprintf(stream, "  echo \"SELECT name FROM student;\" | %s\n", program_name);
}

// SQL 문자열 하나를 lexer -> parser -> executor 순서로 처리합니다.
static int execute_sql_text(const char *sql_text, FILE *out, char *error, size_t error_size) {
    TokenArray tokens = {0};
    ParseResult parse_result;
    ExecResult exec_result;

    if (is_blank_string(sql_text)) {
        snprintf(error, error_size, "missing SQL statement");
        return 0;
    }

    if (!lex_sql(sql_text, &tokens, error, error_size)) {
        return 0;
    }

    parse_result = parse_statement(&tokens);
    if (!parse_result.ok) {
        snprintf(error, error_size, "%s", parse_result.message);
        free_tokens(&tokens);
        return 0;
    }

    exec_result = execute_statement(&parse_result.statement, "schema", "data", out);
    if (!exec_result.ok) {
        snprintf(error, error_size, "%s", exec_result.message);
        free_statement(&parse_result.statement);
        free_tokens(&tokens);
        return 0;
    }

    if (exec_result.message[0] != '\0') {
        fprintf(out, "%s\n", exec_result.message);
    }

    free_statement(&parse_result.statement);
    free_tokens(&tokens);
    return 1;
}

// 한 줄 입력이 파일 경로인지 SQL인지 판별해 실행합니다.
static int execute_argument_or_sql(const char *value, int force_file, FILE *out, FILE *err) {
    char error[256];
    char *sql_text = load_sql_from_argument(value, force_file, error, sizeof(error));
    int ok;

    if (sql_text == NULL) {
        fprintf(err, "error: %s\n", error);
        return 0;
    }

    ok = execute_sql_text(sql_text, out, error, sizeof(error));
    if (!ok) {
        fprintf(err, "error: %s\n", error);
    }

    free(sql_text);
    return ok;
}

// 대화형 프롬프트를 제공해 SQL 또는 파일 경로를 반복 실행합니다.
static int run_repl(FILE *out, FILE *err) {
    char line[4096];
    int had_error = 0;

    while (1) {
        char *input;

        fprintf(out, "sqlparser> ");
        fflush(out);

        if (fgets(line, sizeof(line), stdin) == NULL) {
            if (ferror(stdin)) {
                fprintf(err, "error: failed to read interactive input\n");
                return 1;
            }
            fprintf(out, "\n");
            break;
        }

        strip_line_endings(line);
        input = trim_whitespace(line);

        if (*input == '\0') {
            continue;
        }

        if (strcmp(input, ".exit") == 0 || strcmp(input, ".quit") == 0 ||
            strcmp(input, "exit") == 0 || strcmp(input, "quit") == 0) {
            break;
        }

        if (strcmp(input, ".help") == 0 || strcmp(input, "help") == 0) {
            fprintf(out, "Enter a SQL statement or a SQL file path.\n");
            fprintf(out, "Commands: .help, .exit, .quit\n");
            continue;
        }

        if (!execute_argument_or_sql(input, 0, out, err)) {
            had_error = 1;
        }
    }

    return had_error;
}

// 명령줄 옵션과 표준입력을 해석해 실행할 SQL 문자열을 준비합니다.
static char *load_noninteractive_sql(int argc, char *argv[], char *error, size_t error_size, int *show_help) {
    if (argc < 2) {
        if (stdin_is_interactive()) {
            return NULL;
        }
        return read_stream(stdin, error, error_size);
    }

    if (strcmp(argv[1], "-h") == 0 || strcmp(argv[1], "--help") == 0) {
        *show_help = 1;
        return NULL;
    }

    if (strcmp(argv[1], "-e") == 0 || strcmp(argv[1], "--execute") == 0) {
        return join_arguments_as_sql(argc, argv, 2, error, error_size);
    }

    if (strcmp(argv[1], "-f") == 0 || strcmp(argv[1], "--file") == 0) {
        if (argc < 3) {
            snprintf(error, error_size, "missing file path after %s", argv[1]);
            return NULL;
        }

        if (strcmp(argv[2], "-") == 0) {
            return read_stream(stdin, error, error_size);
        }

        if (argc > 3) {
            snprintf(error, error_size, "unexpected arguments after file path");
            return NULL;
        }

        return read_entire_file(argv[2], error, error_size);
    }

    if (argc == 2 && strcmp(argv[1], "-") == 0) {
        return read_stream(stdin, error, error_size);
    }

    if (argc == 2) {
        return load_sql_from_argument(argv[1], 0, error, error_size);
    }

    return join_arguments_as_sql(argc, argv, 1, error, error_size);
}

int main(int argc, char *argv[]) {
    char error[256];
    char *sql_text;
    int show_help = 0;
    int exit_code;

    if (argc == 1 && stdin_is_interactive()) {
        exit_code = run_repl(stdout, stderr);
        table_index_registry_reset();
        return exit_code;
    }

    sql_text = load_noninteractive_sql(argc, argv, error, sizeof(error), &show_help);
    if (show_help) {
        print_usage(stdout, argv[0]);
        table_index_registry_reset();
        return 0;
    }

    if (sql_text == NULL) {
        fprintf(stderr, "error: %s\n", error);
        print_usage(stderr, argv[0]);
        table_index_registry_reset();
        return 1;
    }

    if (!execute_sql_text(sql_text, stdout, error, sizeof(error))) {
        fprintf(stderr, "error: %s\n", error);
        free(sql_text);
        table_index_registry_reset();
        return 1;
    }

    free(sql_text);
    table_index_registry_reset();
    return 0;
}
