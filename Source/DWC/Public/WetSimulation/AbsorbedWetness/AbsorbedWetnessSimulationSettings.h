//Copyright 2026 Team Tofunut. All Rights Reserved.
#pragma once

// dummy
/*
Absorbed Wetness Simulation 계산에 필요한 설정 snapshot을 정의할 예정이다.

예상 내용:
- absorption multiplier
- spread rate per second
- dry rate per second
- dry hold duration
- max wetness / min pending wetness
- gravity flow strength
- update interval / delta time clamp

분리 이유:
- 사용자 노출 값은 WetnessProfile과 WetClothingSettings에 나뉘어 있다.
- WetSimulationStage가 매번 UObject/Profile을 직접 해석하기보다,
  계산에 필요한 순수 값만 모아둔 snapshot을 받는 편이 좋다.
- 지금은 동기식 stage 구조를 유지하므로 실제 구현은 보류한다.
*/
