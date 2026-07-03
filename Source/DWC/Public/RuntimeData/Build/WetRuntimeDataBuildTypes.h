#pragma once

// dummy
/*
RuntimeData build 과정에서만 사용하는 내부 보조 타입을 정의할 예정이다.

예상 내용:
- LOD vertex count / section range / material slot range 정보
- WetPart 매핑 빌드 중 사용할 material-slot + uv-channel scope key
- BoneOptimizationCache fallback build 중 사용할 bone별 임시 vertex bucket
- NeighborGraph build 중 사용할 triangle edge pair
- build 결과 통계: processed vertex count, fallback 여부, warning count 등

역할:
- RuntimeDataBuilder.cpp 안에 지역 struct가 과도하게 늘어나는 것을 방지한다.
- Public API라기보다는 RuntimeData/Build 내부 구조 정리를 위한 파일이다.
- 외부 모듈이 직접 쓰는 타입은 이 파일에 두지 않는다.
*/
