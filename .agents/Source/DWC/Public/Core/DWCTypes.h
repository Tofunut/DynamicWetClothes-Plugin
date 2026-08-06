#pragma once

#include "CoreMinimal.h"

using FDWCInstanceID = int32;
using FDWCFrameNumber = int32;

/*
DWC 전체에서 공유하는 아주 작은 공통 타입만 정의한다.

현재 설계 규칙:
- ReceiverContext, RequestData, Task metadata 같은 공용 실행 묶음은 두지 않는다.
- RuntimeData, SimulationState, MaterialInstance, SkeletalMeshComponent 같은 도메인 데이터는
  각 Stage가 필요한 함수 인자로 직접 받는다.
- 이 파일에는 플러그인 전체에서 반복 사용되는 ID, enum, 작은 값 타입만 둔다.

추후 멀티스레드 TaskQueue를 도입할 때도, 작업 단위 메타데이터는 별도 Task 계층에 둔다.
*/
enum class EDWCResultCode : uint8
{
    Succeeded,
    Failed,
    Skipped
};
