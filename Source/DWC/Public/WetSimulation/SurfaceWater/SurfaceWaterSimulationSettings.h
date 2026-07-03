// dummy
/*
Surface Water Simulation 계산에 필요한 설정값을 정의할 예정이다.

예상 내용:
- accumulation rate: 표면에 물이 쌓이는 정도
- runoff strength: 중력 방향으로 흘러내리는 강도
- dripping threshold: 일정량 이상 모이면 물방울이 떨어지는 기준
- evaporation rate: 표면 물이 사라지는 속도
- transfer to absorbed wetness rate: 표면 물이 내부 젖음으로 전환되는 비율
- surface tension / flow damping: 물막이 급격히 퍼지지 않도록 안정화하는 값

주의:
- 이 값들은 WetSimulationStage 전용이다.
- Absorbed Wetness의 absorption/spread/dry와 섞지 않는다.
*/
