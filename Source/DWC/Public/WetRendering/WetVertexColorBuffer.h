#pragma once

// dummy
/*
Wetness 값을 VertexColor로 반영하기 위한 임시 버퍼와 부분 갱신 정책을 관리할 예정이다.

예상 내용:
- CachedWetVertexColors
- DirtyVertexIndices 기반 부분 갱신
- wetness-only color 모드
- WetPart debug color + wetness mask 조합 모드
- vertex count mismatch 시 안전한 resize 정책
- SetVertexColorOverride_LinearColor 호출 전 검증

분리 이유:
- RenderStage가 Material parameter 처리와 VertexColor 버퍼 처리를 모두 맡으면 비대해진다.
- VertexColor 업데이트 정책은 WetRendering 안에서도 독립된 책임으로 보는 것이 좋다.
*/
