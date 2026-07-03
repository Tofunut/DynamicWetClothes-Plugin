#pragma once

// dummy
/*
DWC 전용 프로파일링 helper와 scope macro를 정의할 예정이다.

예상 측정 구간:
- WetRuntimeDataBuilder: runtime data 초기화, neighbor graph 생성, bone cache 생성
- WetBakedRuntimeDataBridge: 에셋에 저장된 baked data를 runtime data로 변환하는 시간
- WetInputStage: contact / area / surface 입력을 내부 wetness state에 누적하는 시간
- WetSimulationStage: absorbed wetness 확산/건조 및 surface water 갱신 시간
- WetRenderStage: material parameter, vertex color, wetness profile map 갱신 시간

현재는 멀티스레드와 TaskQueue를 도입하지 않으므로 파일만 준비한다.
나중에 Unreal Insights용 TRACE_CPUPROFILER_EVENT_SCOPE나 CSV profiler helper를
여기에 모아두면 기능 코드에 profiling 코드를 흩뿌리지 않을 수 있다.
*/
