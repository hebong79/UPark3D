# Loop 14 — 번호 글자 크기 확대

사용자 시각 피드백에 따라 TextRender `WorldSize`만 `6.5→9.0cm`으로 변경했다. 목표는 11cm plate 높이 대비 screenshot상 약 50%의 읽기 쉬운 글자 높이다. horizontal scale `.80`, X=4/Y=1.55, spacing/orientation/font/material/Content background는 그대로다.

테스트 expected world size도 `9.0`으로 갱신했다. 수동 C++ compile 후 Automation과 PIE screenshot으로 글자 폭 overflow·blue field overlap 없이 가독성이 좋아졌는지 확인한다.
