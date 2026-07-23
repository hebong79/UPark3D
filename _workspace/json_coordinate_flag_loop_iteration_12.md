# Loop 12 — Korean usage character test encoding

## 실패

Loop 11 수동 compile은 `TCHAR('하')`, `TCHAR('허')`, `TCHAR('호')`의 multi-byte character literal C4310으로 중단됐다. `TCHAR` 단일 character literal이 Korean UTF-16 code unit 비교를 표현하는 안전한 방식이 아니었다.

## 수정

번호 문자열의 네 번째 code unit을 `Expected.Mid(3,1)` `FString`으로 얻고 `TEXT("하")`/`TEXT("허")`/`TEXT("호")`로 비교하도록 test만 변경했다. 허용 pool 확인도 같은 `UsageChar` 문자열을 사용한다.

## 다음 게이트

수동 C++ compile을 재실행한다. 성공 후 Loop 11의 format, frame/strip, text Automation과 PIE 검증을 진행한다.
