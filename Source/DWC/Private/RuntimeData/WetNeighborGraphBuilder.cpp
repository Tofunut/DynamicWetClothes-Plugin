// dummy
/*
WetNeighborGraphBuilder의 구현 파일이 될 예정이다.

예상 구현:
- LOD index buffer에서 triangle index를 읽는다.
- 각 triangle의 세 edge를 양방향 neighbor로 추가한다.
- 중복 neighbor를 제거한다.
- vertex count와 index buffer 유효성을 검사한다.
- WetClothingAsset baked neighbor graph가 유효하지 않을 때 fallback graph를 만든다.

현재 neighbor graph 생성 로직은 WetClothingRuntimeDataBuilder 안에 남아 있으며, 이후 이 파일로 분리한다.
*/
