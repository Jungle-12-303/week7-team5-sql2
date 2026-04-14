// executor는 파싱된 문장을 실제 동작으로 바꾸는 단계다.
#ifndef EXECUTOR_H
#define EXECUTOR_H

#include "sqlparser/sql/ast.h"

#include <stdio.h>

// 실행 단계의 성공/실패와 요약 정보를 담는 구조체다.
typedef struct {
    // 1이면 실행 성공, 0이면 실패다.
    int ok;
    // 영향받은 행 수다. INSERT 1, SELECT 3 같은 값에 쓰인다.
    int affected_rows;
    // 사용자에게 보여 줄 실행 결과 메시지다.
    char message[256];
} ExecResult;

// 파싱된 Statement를 실제 파일 기반 DB 동작으로 실행한다.
ExecResult execute_statement(const Statement *statement, const char *schema_dir, const char *data_dir, FILE *out);

#endif
