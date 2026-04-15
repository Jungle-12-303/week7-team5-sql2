// AST 구조체를 해제하기 위한 선언입니다.
/*
 * app/main.c
 *
 * 이 파일은 프로그램의 시작점이다.
 * 사용자가 CLI 인자를 줬는지, 파일을 실행하려는지, SQL 문자열을 직접 넣었는지,
 * 아니면 REPL로 대화형 입력을 원하는지를 판단해 전체 흐름을 시작한다.
 *
 * 아키텍처 관점에서 app 계층은 "입력 방식 결정"만 담당하고,
 * 실제 SQL 해석과 실행은 각각 lexer/parser/executor에 맡긴다.
 */
#include "sqlparser/sql/ast.h"
// 파싱된 SQL을 실제로 실행하기 위한 선언입니다.
#include "sqlparser/execution/executor.h"
// SQL 문자열을 토큰으로 나누기 위한 선언입니다.
#include "sqlparser/sql/lexer.h"
// 토큰 목록을 INSERT / SELECT 구조로 해석하기 위한 선언입니다.
#include "sqlparser/sql/parser.h"
// 파일 읽기와 문자열 복사 같은 공통 유틸 함수를 쓰기 위한 선언입니다.
#include "sqlparser/common/util.h"

#include <ctype.h>
#include <errno.h>
// 표준 입출력과 fopen을 사용합니다.
#include <stdio.h>
// free, malloc 같은 메모리 함수들을 사용합니다.
#include <stdlib.h>
// strlen, memcpy 같은 문자열 함수를 사용합니다.
#include <string.h>
#include <sys/stat.h>

#ifdef _MSC_VER
#include <io.h>
#define ISATTY _isatty
#define STAT_STRUCT struct _stat
#define STAT_FUNC _stat
#define IS_REGULAR_FILE(mode) (((mode) & _S_IFMT) == _S_IFREG)
#define IS_DIRECTORY_FILE(mode) (((mode) & _S_IFMT) == _S_IFDIR)
#else
#include <unistd.h>
#define ISATTY isatty
#define STAT_STRUCT struct stat
#define STAT_FUNC stat
#define IS_REGULAR_FILE(mode) S_ISREG(mode)
#define IS_DIRECTORY_FILE(mode) S_ISDIR(mode)
#endif

// 전달받은 인자가 실제 파일 경로인지 간단히 검사합니다.
/* 단순히 "이 경로를 파일로 열 수 있는가"를 확인하는 작은 헬퍼 함수다. */
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

/*
 * bare argument가 "파일 경로인지" 아니면 "그냥 SQL 문자열인지" 판단한다.
 *
 * 예:
 * - users.sql  -> 파일로 읽기
 * - SELECT ... -> SQL 문자열로 간주
 * - 디렉터리 경로 -> 오류
 */
static int resolve_bare_argument_file(const char *path, int *should_read_file, char *error, size_t error_size) {
    STAT_STRUCT info;

    *should_read_file = 0;
    if (file_exists(path)) {
        *should_read_file = 1;
        return 1;
    }

    if (STAT_FUNC(path, &info) == 0) {
        if (IS_DIRECTORY_FILE(info.st_mode)) {
            snprintf(error, error_size, "path is a directory, not a SQL file: %s", path);
            return 0;
        }

        if (!IS_REGULAR_FILE(info.st_mode)) {
            snprintf(error, error_size, "path is not a regular file: %s", path);
            return 0;
        }

        format_system_error(error, error_size, "failed to access SQL file", path);
        return 0;
    }

    if (errno == ENOENT || errno == ENOTDIR || errno == EILSEQ) {
        return 1;
    }

    format_system_error(error, error_size, "failed to access SQL path", path);
    return 0;
}

// 표준입력이 터미널에 연결된 대화형 환경인지 확인합니다.
/* 표준입력이 터미널에 연결됐는지 확인해 REPL 여부를 판단한다. */
static int stdin_is_interactive(void) {
#ifdef _MSC_VER
    return ISATTY(_fileno(stdin)) != 0;
#else
    return ISATTY(STDIN_FILENO) != 0;
#endif
}

// 공백만 있는 문자열인지 검사합니다.
/* 공백만 있는 입력인지 검사해 빈 SQL을 빠르게 걸러낸다. */
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
/* 여러 CLI 인자를 하나의 SQL 문자열로 합친다. */
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
/* stdin 같은 스트림 전체를 읽어 하나의 문자열로 만든다. */
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
/* 인자를 파일로 읽을지, SQL 문자열로 그대로 쓸지 결정해 SQL 본문을 준비한다. */
static char *load_sql_from_argument(const char *value, int force_file, char *error, size_t error_size) {
    int should_read_file = 0;

    if (force_file) {
        return read_entire_file(value, error, error_size);
    }

    if (!resolve_bare_argument_file(value, &should_read_file, error, error_size)) {
        return NULL;
    }

    if (should_read_file) {
        return read_entire_file(value, error, error_size);
    }

    return copy_string(value);
}

// 사용자에게 보여줄 CLI 사용법을 출력합니다.
/* 사용자가 --help 또는 잘못된 인자를 넣었을 때 보여 줄 도움말을 출력한다. */
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
    fprintf(stream, "  %s -f examples/select_name_age.sql\n", program_name);
    fprintf(stream, "  echo \"SELECT name FROM student;\" | %s\n", program_name);
}

// SQL 문자열 하나를 lexer -> parser -> executor 순서로 처리합니다.
/*
 * SQL 문자열 하나를 끝까지 실행한다.
 *
 * 흐름:
 * 1. 빈 입력 검사
 * 2. lexer
 * 3. parser
 * 4. executor
 * 5. 결과 메시지 출력
 */
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
/* REPL에서 받은 입력을 파일 경로 또는 SQL 문자열로 해석해 실행한다. */
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
/*
 * 대화형 프롬프트를 실행한다.
 *
 * 사용자는 한 줄씩 SQL 또는 파일 경로를 입력할 수 있고,
 * .exit / .quit / help 같은 간단한 명령도 사용할 수 있다.
 */
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
/* 비대화형 실행에서 CLI 인자를 해석해 최종 SQL 본문을 결정한다. */
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

    if (argv[1][0] == '-' && strcmp(argv[1], "-") != 0) {
        snprintf(error, error_size, "unknown option: %s", argv[1]);
        return NULL;
    }

    if (argc == 2 && strcmp(argv[1], "-") == 0) {
        return read_stream(stdin, error, error_size);
    }

    if (argc == 2) {
        return load_sql_from_argument(argv[1], 0, error, error_size);
    }

    return join_arguments_as_sql(argc, argv, 1, error, error_size);
}

/*
 * 프로그램 시작점.
 *
 * 크게 보면 아래 분기만 담당한다.
 * - REPL로 들어갈지
 * - help만 출력할지
 * - SQL을 한 번 실행하고 끝낼지
 */
int main(int argc, char *argv[]) {
    char error[256];
    char *sql_text;
    int show_help = 0;
    int exit_code;

    if (argc == 1 && stdin_is_interactive()) {
        exit_code = run_repl(stdout, stderr);
        execution_runtime_reset();
        return exit_code;
    }

    sql_text = load_noninteractive_sql(argc, argv, error, sizeof(error), &show_help);
    if (show_help) {
        print_usage(stdout, argv[0]);
        execution_runtime_reset();
        return 0;
    }

    if (sql_text == NULL) {
        fprintf(stderr, "error: %s\n", error);
        print_usage(stderr, argv[0]);
        execution_runtime_reset();
        return 1;
    }

    if (!execute_sql_text(sql_text, stdout, error, sizeof(error))) {
        fprintf(stderr, "error: %s\n", error);
        free(sql_text);
        execution_runtime_reset();
        return 1;
    }

    free(sql_text);
    execution_runtime_reset();
    return 0;
}
