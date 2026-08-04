---
name: korean-docs
description: Park3D 변경 사항·신규 클래스·수정 로직·검증 결과와 사용자 질문 답변을 한글 UTF-8 Markdown으로 기록한다. 코드 변경 완료 후 또는 질문에 답할 때 반드시 사용한다. 단 소스 변경 없이 MCP/RPC 런타임 조작만 한 작업은 문서를 만들지 않는다.
---

**문서를 만들지 않는 경우**: Park3D MCP/RPC로 실행 중인 인스턴스를 조작·조회만 하고 저장소 파일(`Park3D/Source`·`Config`·`Content`·스크립트·문서 등)을 하나도 변경하지 않았다면 `Docs/*.md`를 작성하지 않는다. 판정 기준은 디스크의 저장소 파일 변경 여부이며 RPC 호출 횟수나 난이도가 아니다. `preset.save`/`car.save`처럼 RPC가 데이터 파일을 쓰면 변경에 해당해 문서를 작성한다. 사용자가 문서화를 요청하면 예외를 적용하지 않는다. 문서를 생략해도 결과·실패·미검증은 응답으로 사실대로 보고한다. 상세는 `.claude/skills/korean-docs/SKILL.md`의 동명 절을 따른다.

루트 `AGENTS.md`와 `.claude/skills/korean-docs/SKILL.md`를 끝까지 읽고 실제 시각 기반 `Docs/yyyyMMdd_HHmmss_이름.md`를 작성한다. 표준 작업에서는 최종 문서 전에 `_workspace/{phase}_luna_behavior_impact_report.md`를 작성해 인접 동작 회귀 여부를 점검한다. 실제 플랫폼·역할·모델을 작성자로 기록하고 검증 결과와 미확인 항목을 사실대로 남긴다. Goal/Loop에서는 Sol 설계·영향도, Terra 개발·실행, 별도 Terra QA 산출물이 모두 통과한 뒤 최종 문서화만 하고 결과를 임의 재판정하지 않는다.
