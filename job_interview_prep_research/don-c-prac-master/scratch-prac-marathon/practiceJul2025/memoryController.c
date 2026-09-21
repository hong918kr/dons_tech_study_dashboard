/*
    Memory Controller Bringup 과정
    메모리 컨트롤러 Bringup은 HW와 SW가 cooperate해서 메모리가 정상적으로
    작동하도록 하는 과정임. 

    1. Check Architecture and spec
        - 메모리 컨트롤러의 사양과 데이터시트를 확인
        메모리 타입(HBM, DDR, GDDR등) 특징과 요구사항을 이해


    2. 하드웨어 준비
        - 메모리 모듈및 컨트롤러 장착
        - 보드레벨에서의 연결확인 (PCB, signal)
    3. 초기화 및 설정
        - 메모리 컨트롤러 초기화 (초기 레지스터 설정)
        - 메모리 타이밍 파라미터 설정 (CAS latency, RAS to CAS Delay) 등
    4. 메모리 테스트
        - 기본적인 메모리 읽기 / 쓰기 테스트
        - 메모리 접근 페턴 테스트 (순차 접근, 무작위 접근)
    5. 디버깅 및 최적화
        - 하드웨어 및 소프트웨어 디버깅
        - 성능 최적화 및 튜닝
    6. 문서화 및 문서 보고
        - Bringup 과정 및 결과 문서화, 테스트 결과 보고 작성

HBM, DDR< GDDR Bringup 각각의 특징
    1. DDR (Double Data Rate)
        - 가장 일반적 메모리 타입
        - 데이터 송수신이 클락 양쪽의 엣지에서 발생, 높은 데이터 전송률 제공
        - 다양한 용량 속도 지원 (DDR3,4,5)
    Bringup 과정
        - 초기화: 메모리 타이밍 파라미터 설정, 모드 설정등
        - 테스트: 읽기 / 쓰기 테스트, 패턴 테스트
        - 튜닝: CAS latency, RAS to CAS delay 등의 파라미터 조정
    
    2. GDDR (Graphics Double Data Rate)
        - 그래픽 처리기용 고속 메모리
        - 높은 데이터 전송률과 낮은 지연시간 제공
        - 주로 GPU에서 사용 (GDDR5, GDDR5X, GDDR6등)

    

*/