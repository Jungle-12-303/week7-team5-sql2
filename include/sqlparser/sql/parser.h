// parser는 token 배열을 실제 SQL 의미 구조로 바꾸는 단계다.
#ifndef PARSER_H
#define PARSER_H

#include "sqlparser/sql/ast.h"
#include "sqlparser/sql/lexer.h"

// parser가 문장을 해석한 뒤 돌려주는 결과 구조체다.
typedef struct {
    // 1이면 파싱 성공, 0이면 실패다.
    int ok;
    // 성공했을 때 해석된 SQL 문장이 들어간다.
    Statement statement;
    // 실패했을 때 사용자에게 보여 줄 메시지다.
    char message[256];
} ParseResult;

// 토큰 목록을 읽어 Statement 하나로 파싱한다.
ParseResult parse_statement(const TokenArray *tokens);

#endif
