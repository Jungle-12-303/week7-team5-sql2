// lexer는 SQL 문자열을 작은 조각(Token)들로 자르는 단계다.
#ifndef LEXER_H
#define LEXER_H

#include <stddef.h>

// lexer가 SQL 문자열을 자른 뒤 붙이는 토큰 종류들이다.
typedef enum {
    TOKEN_IDENTIFIER,
    TOKEN_STRING,
    TOKEN_NUMBER,
    TOKEN_STAR,
    TOKEN_EQUALS,
    TOKEN_COMMA,
    TOKEN_LPAREN,
    TOKEN_RPAREN,
    TOKEN_SEMICOLON,
    TOKEN_END
} TokenType;

// 토큰 하나를 표현하는 구조체다.
typedef struct {
    // 토큰 종류다. 예: 식별자, 문자열, 쉼표.
    TokenType type;
    // 토큰 원문 텍스트다.
    char *text;
    // 원래 SQL 문자열에서 시작 위치다.
    int position;
} Token;

// 여러 개의 Token을 동적으로 담기 위한 배열 구조체다.
typedef struct {
    // 실제 토큰들이 담긴 배열이다.
    Token *items;
    // 현재 저장된 토큰 수다.
    int count;
    // 현재 확보한 배열 크기다.
    int capacity;
} TokenArray;

// SQL 문자열을 읽어 TokenArray로 분해한다.
int lex_sql(const char *input, TokenArray *tokens, char *error, size_t error_size);
// TokenArray 내부 메모리를 해제한다.
void free_tokens(TokenArray *tokens);

#endif
