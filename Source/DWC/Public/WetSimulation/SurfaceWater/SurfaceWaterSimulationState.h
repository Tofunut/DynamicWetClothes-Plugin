// dummy
/*
옷감 표면 위에 남아 있는 물의 런타임 상태를 정의할 예정이다.

예상 내용:
- SurfaceWaterPerVertex: vertex별 표면 잔류 물량
- FlowDirectionPerVertex: 중력/노멀 기반 흐름 방향
- SurfaceVelocityPerVertex: 표면 물 흐름 속도
- DropletCandidateVertices: 물방울 분리 후보 vertex
- TransferToAbsorbedWetnessPerVertex: 내부 흡수 젖음으로 넘길 물량
- DirtySurfaceWaterVertices: 렌더 갱신이 필요한 vertex 목록

AbsorbedWetness와의 차이:
- AbsorbedWetness는 옷감 내부에 흡수된 젖음이다.
- SurfaceWater는 옷감 표면 위에 남아 있는 물방울/물막/흐름을 위한 상태다.
*/
