#pragma once

#include "CoreMinimal.h"

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

-----------------------------
[Optimization Note]
언리얼 기본 API는 전체 FLinearColor 배열을 FColor로 변환하고, 기존 override color buffer를
블로킹 release 경로로 해제한 뒤, 새 FColorVertexBuffer를 생성/초기화하고 mesh render state를
dirty 처리한다. 젖음 값이 자주 갱신되는 상황에서는 실제 dirty vertex 수가 적더라도 이 전체
override 경로가 Game Thread의 주요 병목이 될 수 있다.

따라서 런타임 갱신에서는 FColorVertexBuffer를 직접 생성한 뒤
LODInfo[LODIndex].OverrideVertexColors에 교체한다. 기존 버퍼는 render thread에서 비동기 release한다.
유효하지 않거나 지원되지 않는 상태에서는 기존 엔진 API를 fallback으로 사용한다.

*/

class USkeletalMeshComponent;

class DWC_API FWetVertexColorBuffer
{
  public:
    static void ApplyVertexColorOverride(
        USkeletalMeshComponent&     TargetSkeletalMesh,
        int32                       LODIndex,
        const TArray<FLinearColor>& VertexColors);

  private:
    static bool ApplyVertexColorOverrideByDirectBufferSwap(
        USkeletalMeshComponent&     TargetSkeletalMesh,
        int32                       LODIndex,
        const TArray<FLinearColor>& VertexColors);
};
