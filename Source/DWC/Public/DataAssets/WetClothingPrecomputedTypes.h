#pragma once

// dummy
/*
WetClothingAsset의 precomputed simulation data에서 여러 파일이 공통으로 사용할 작은 보조 타입을 정의할 예정이다.

예상 내용:
- Mesh / Skeleton / SkinWeight 검증용 signature 구조체
- MaterialSlotIndex + UVChannelIndex + UVIslandID를 묶는 precompute key
- VertexIndex / WetPartID / ProfileIndex / MaterialSlotIndex를 묶는 precomputed vertex record
- NeighborGraph 저장용 lightweight neighbor record
- precomputed data 버전 번호와 호환성 검사 enum

분리 이유:
- WetClothingAsset.h에 모든 USTRUCT를 몰아넣으면 Asset 헤더가 너무 비대해진다.
- Bone cache, wet part mapping, neighbor graph가 같은 검증 정보를 공유할 수 있다.
- 단, 실제 런타임 로직은 여기에 두지 않고 순수 데이터 타입만 둔다.
*/
