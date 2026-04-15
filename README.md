# SQL Processor Week 7

7주차 5조 SQL 처리기 프로젝트입니다.

이번 주차의 핵심 목표는 **CSV 기반 SQL 처리기의 조회 성능 개선**입니다. 기존 6주차 SQL 처리기는 데이터를 CSV 파일에 저장하고, 조회 시 파일을 처음부터 끝까지 읽는 선형 탐색 방식을 사용했습니다. 데이터가 많아질수록 특정 레코드를 찾는 비용이 계속 커지는 문제가 있었고, 이를 해결하기 위해 이번 주차에서는 **B+ 트리 기반 인덱스 구조**를 도입했습니다.

## 발표 요약

### 문제 정의

6주차 구조에서는 `SELECT ... WHERE ...` 조회가 CSV 전체를 순차적으로 탐색합니다.

예를 들어 100만 건의 데이터가 있을 때:

- `WHERE name = 'name_100000'`은 CSV를 순서대로 읽으며 비교합니다.
- `WHERE id = 100000`도 인덱스가 없다면 같은 방식으로 선형 탐색해야 합니다.

이번 구현에서는 `id` 기준 조회만이라도 빠르게 처리하기 위해, 실행 중 메모리에 B+ 트리 인덱스를 구성합니다.

### 핵심 구현

1. 기존 `INSERT`, `SELECT`, `WHERE` 실행 흐름 유지
2. 숨은 내부 PK `__internal_id` 자동 부여
3. 테이블별 메모리 B+ 트리 인덱스 유지
4. `WHERE id = <number>` 인덱스 조회
5. CSV 기준 인덱스 재구성
6. 100만 건 이상 데이터 삽입 및 조회 비교용 벤치마크 진입점 구현

## 전체 구조

실제 데이터는 기존과 동일하게 CSV 파일에 저장합니다. 대신 프로그램 실행 중에는 CSV를 읽어 메모리 상에 B+ 트리 인덱스를 구성합니다.

```text
data/student.csv
        |
        | CSV 선형 스캔
        v
메모리 B+ Tree
internal_id -> CSV row_offset
        |
        | WHERE id 조회
        v
fseek(row_offset) 후 해당 행만 읽기
```

즉, B+ 트리에는 CSV row 전체가 아니라 다음 매핑만 저장됩니다.

```text
1      -> 첫 번째 데이터 행의 파일 offset
2      -> 두 번째 데이터 행의 파일 offset
100000 -> 100000번째 데이터 행의 파일 offset
```

인덱스는 메모리 구조이므로 프로그램이 종료되면 사라집니다. 다음 실행에서 `WHERE id = ...` 조회나 `INSERT`가 발생하면 CSV를 기준으로 다시 재구성합니다.

## INSERT 흐름

INSERT 시 사용자는 `id`를 직접 넣지 않습니다.

```sql
INSERT INTO student (department, student_number, name, age)
VALUES ('경제학과', '2026005', '김금융', 21);
```

실행 흐름은 다음과 같습니다.

```text
1. schema/student.meta 로딩
2. INSERT 컬럼/값 검증
3. 다음 내부 id 계산
4. data/student.csv 끝에 새 행 append
5. append된 행의 시작 offset 획득
6. 메모리 B+ 트리에 internal_id -> row_offset 등록
```

CSV에는 사용자 컬럼만 저장됩니다. 내부 id는 CSV 컬럼으로 저장하지 않고, 행 순서를 기준으로 계산합니다.

## SELECT 흐름

### 일반 WHERE 조회

```sql
SELECT id, department, student_number, name, age
FROM student
WHERE name = '김금융';
```

`name` 컬럼에는 인덱스가 없으므로 기존처럼 CSV를 처음부터 끝까지 읽으며 조건을 비교합니다.

```text
CSV open
-> 헤더 읽기
-> 데이터 행을 순서대로 읽기
-> name 값 비교
-> 일치하는 행 출력
```

### id 인덱스 조회

```sql
SELECT id, department, student_number, name, age
FROM student
WHERE id = 101014;
```

`WHERE id = ...`는 특별히 B+ 트리 인덱스 경로를 사용합니다.

```text
B+ 트리에서 id 검색
-> row_offset 획득
-> CSV 파일에서 fseek(row_offset)
-> 해당 행 한 줄만 읽기
```

그래서 같은 데이터셋에서 일반 컬럼 조회보다 훨씬 빠르게 동작합니다.

## B+ 트리 구현

B+ 트리 구현은 `src/index/bptree.c`에 있습니다.

현재 노드 최대 key 수는 학습과 테스트를 위해 작게 설정되어 있습니다.

```c
#define BPTREE_MAX_KEYS 3
```

즉 한 노드는 정상 상태에서 최대 3개의 key를 가집니다. 삽입 후 key가 4개가 되면 overflow로 판단하고 split합니다.

리프 노드는 다음 정보를 가집니다.

```text
keys   = internal_id
values = CSV row_offset
next   = 다음 리프 노드 포인터
```

리프 노드 연결 리스트도 구현되어 있습니다.

```text
[1, 2] -> [3, 4] -> [5, 6]
```

다만 현재 SQL 기능은 `WHERE id = <number>` 단건 조회만 지원하므로, 리프 연결 리스트를 활용한 범위 검색은 아직 구현하지 않았습니다.

## 현재 구현 범위와 한계

지원하는 기능:

- `INSERT`
- `SELECT *`
- `SELECT column1, column2`
- `SELECT ... WHERE column = value`
- `WHERE id = <number>` 인덱스 조회
- `SELECT id, ...` 내부 id 출력
- SQL 파일 실행
- SQL 문자열 직접 실행
- REPL 실행
- 벤치마크 실행

아직 지원하지 않는 기능:

- `UPDATE`
- `DELETE`
- `JOIN`
- `ORDER BY`
- `GROUP BY`
- 복합 `WHERE`
- `BETWEEN`, `>`, `<` 같은 범위 검색
- 디스크 기반 B+ 트리
- 버퍼 풀 또는 파일 캐시
- 인덱스 파일 영속화

현재 B+ 트리는 실제 DB의 페이지 기반 고 fanout B+ 트리라기보다는, `id -> row_offset` 매핑을 위한 **메모리 기반 학습용 B+ 트리**입니다.

## 발표 시연 순서

먼저 실행합니다.

```bash
./build/bin/sqlparser
```

### 1. id 없이 INSERT

```sql
INSERT INTO student (department, student_number, name, age) VALUES ('경제학과', '2026005', '김금융', 21);
```

기대 출력:

```text
INSERT 1
Elapsed time: ...
```

### 2. 일반 컬럼 name으로 조회

```sql
SELECT id, department, student_number, name, age FROM student WHERE name = '김금융';
```

이 조회는 `name` 인덱스가 없으므로 CSV 선형 탐색을 수행합니다.

### 3. id로 조회

아래 id 값은 현재 CSV 상태에 따라 달라질 수 있습니다. 직전 조회 결과에 나온 가장 마지막 id를 사용하면 됩니다.

```sql
SELECT id, department, student_number, name, age FROM student WHERE id = 101014;
```

이 조회는 B+ 트리 인덱스를 사용합니다. 일반 컬럼 조회보다 elapsed time이 작게 나오는 것을 확인할 수 있습니다.

REPL 종료:

```text
.exit
```

## 벤치마크

벤치마크는 별도 바이너리 `benchmark_runner`로 실행합니다.

```bash
make benchmark
```

### prepare 모드

CSV를 헤더만 남기고 초기화한 뒤, 지정한 개수만큼 데이터를 생성해 INSERT합니다.

```bash
./build/bin/benchmark_runner prepare benchmark-workdir/schema benchmark-workdir/data student 1000000
```

출력:

```text
Prepared rows: 1000000
Insert time: ...
```

### query-only 모드

이미 준비된 데이터셋에서 조회 성능만 비교합니다.

```bash
./build/bin/benchmark_runner query-only benchmark-workdir/schema benchmark-workdir/data student 100000 10
./build/bin/benchmark_runner query-only benchmark-workdir/schema benchmark-workdir/data student 1000000 10
```

비교 기준:

- 인덱스 조회: `WHERE id = <target_id>`
- 선형 조회: 첫 번째 사용자 컬럼 기준 `WHERE department = 'department_<target_id>'`
- 반복 횟수: 마지막 인자 `10`

출력 예시:

```text
Query target id: 1000000
Query target column: department
Query target value: department_1000000
Query repeats: 10
Indexed query avg time: 0.090429 sec
Linear query avg time: 0.627952 sec
```

주의할 점:

- `prepare`는 기존 벤치마크 CSV를 초기화합니다.
- `query-only`는 CSV를 초기화하지 않고 이미 준비된 데이터를 그대로 사용합니다.
- 첫 id 조회에는 인덱스 재구성 비용이 섞일 수 있습니다.
- 같은 프로세스 안에서 인덱스가 이미 로드된 뒤에는 `WHERE id = ...` 조회가 더 빠르게 동작합니다.

## 빌드와 실행

기본 CLI 빌드:

```bash
make
```

또는:

```bash
make all
```

테스트:

```bash
make test
```

벤치마크 빌드:

```bash
make benchmark
```

도움말:

```bash
./build/bin/sqlparser --help
```

SQL 문자열 직접 실행:

```bash
./build/bin/sqlparser -e "SELECT * FROM student;"
```

SQL 파일 실행:

```bash
./build/bin/sqlparser -f examples/select_name_age.sql
```

REPL 실행:

```bash
./build/bin/sqlparser
```

REPL 종료 명령:

- `.exit`
- `.quit`
- `exit`
- `quit`

macOS/Linux에서는 `./build/bin/sqlparser` 형식으로 실행합니다.
Windows MinGW 환경에서는 `mingw32-make`, `.\build\bin\sqlparser.exe` 형식을 사용할 수 있습니다.

## 디렉터리 구조

```text
src/app        CLI, 파일 입력, REPL 시작점
src/sql        lexer, parser, AST
src/execution  INSERT/SELECT 실행, 인덱스 경로 선택
src/storage    schema 로딩, CSV 읽기/쓰기
src/index      B+ 트리, 테이블별 인덱스 관리
src/benchmark  대량 삽입 및 조회 성능 측정
tests          단위/기능 테스트
docs           요구사항, 아키텍처, 테스트 문서
examples       SQL 예시 파일
```

## 데이터 형식

테이블은 아래 두 파일이 모두 있어야 합니다.

```text
schema/<table>.meta
data/<table>.csv
```

예시:

```text
schema/student.meta
data/student.csv
```

스키마 파일:

```text
table=학생
columns=department,student_number,name,age
```

CSV 파일 첫 줄:

```text
department,student_number,name,age
```

## 테스트

현재 테스트 러너 기준:

- assertion 375개
- B+ 트리 삽입/검색/split
- 내부 id 자동 부여
- `WHERE id` 인덱스 경로
- 일반 `WHERE` 선형 탐색
- 인덱스 재구성
- CLI 오류 메시지
- 벤치마크 데이터 재생성

실행:

```bash
make test
```

## 발표 마무리 포인트

이번 프로젝트를 통해 단순히 SQL 기능을 추가하는 데서 끝나지 않고, CSV 기반 저장 구조 위에 B+ 트리 인덱스를 붙여 조회 경로를 분리했습니다.

`WHERE name = ...` 같은 일반 조건은 기존처럼 선형 탐색을 수행하지만, `WHERE id = ...` 조건은 메모리 B+ 트리에서 row offset을 찾고 해당 CSV 행만 읽습니다. 이를 통해 DB에서 인덱스가 왜 필요한지, 그리고 B+ 트리가 왜 인덱스 자료구조로 사용되는지 직접 확인할 수 있었습니다.

## 참고 문서

- [docs/requirements.md](docs/requirements.md)
- [docs/architecture.md](docs/architecture.md)
- [docs/test-cases.md](docs/test-cases.md)
- [learning-docs/beginner-guide.md](learning-docs/beginner-guide.md)
- [learning-docs/docker-basics-for-week7.md](learning-docs/docker-basics-for-week7.md)
- [learning-docs/makefile-basics-for-sql-processor.md](learning-docs/makefile-basics-for-sql-processor.md)
