#pragma once

// dummy
/*
NeighborGraph 생성 전담 Builder를 정의할 예정이다.

예상 내용:
- SkeletalMesh LOD index buffer를 순회해 vertex adjacency 생성
- triangle의 세 edge를 양방향 neighbor로 추가
- 중복 neighbor 제거
- vertex count mismatch / index buffer null / invalid triangle 방어 코드
- WetClothingAsset의 baked neighbor graph가 무효일 때 runtime fallback으로 생성

분리 이유:
- 현재 neighbor graph 생성 로직이 RuntimeDataBuilder 안에 있으면 RuntimeDataBuilder가 너무 비대해진다.
- RuntimeDataBuilder는 전체 build 흐름 조율만 하고, 실제 graph 생성은 이 Builder가 담당하는 구조가 좋다.
*/
