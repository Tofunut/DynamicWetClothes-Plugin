#pragma once

// dummy
/*
DWC WetRendering에서 사용하는 머티리얼 파라미터 이름과 기본값을 한 곳에 모을 예정이다.

예상 내용:
- DWC_WetnessProfileMap0
- DWC_UseWetnessProfileMap0
- DWC_WetPartDebugStrength
- DWC_WetPartDebugUseWetnessMask
- AbsorbedWetness visual strength 관련 parameter name
- SurfaceWater droplet / streak / film 관련 parameter name

분리 이유:
- 문자열 FName이 RenderStage, Component, Editor MaterialSetup에 흩어지면 변경이 어렵다.
- 기본 파라미터 이름을 중앙에서 관리하면 material graph 자동 삽입/복사본 생성 로직과도 맞추기 쉽다.
*/
