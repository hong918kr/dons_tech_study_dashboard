/*
일자	주요 초점	핵심 개념	추천 연습
1일	RAII & 스마트 포인터	std::unique_ptr, std::shared_ptr	C-스타일 클래스를 스마트 포인터로 리팩토링
2일	이동 의미론	L/R-value, std::move	복사/이동 생성자 호출 로깅
3일	동시성 기초	std::thread, std::mutex, std::condition_variable	간단한 생산자-소비자 프로그램 작성
4.일	Google Test	TEST_F, ASSERT_*, EXPECT_*	Calculator 클래스 단위 테스트 작성
5일	CMake	add_library, target_link_libraries	Calculator + gtest 프로젝트 CMake 구성
6일	임베디드 개념	비트 연산, RTOS 기초	LeetCode 비트 연산 문제 풀이
7일	종합 복습	1주차 전체	스레드 안전한 카운터 클래스 + gtest + CMake
8-9일	종합 실습	전체 워크플로	메시지 큐 문제 풀이 (시간 제한 없음)
10일	1차 모의 테스트	시간 관리, 속도	2시간 타이머 설정 후 메시지 큐 구현
11일	약점 보완	10일차 분석 기반	관련 개념 재학습 및 LeetCode 문제 풀이
12일	2차 모의 테스트	완성도 향상	2시간 타이머 설정 후 메시지 큐 재구현
13일	코드 리뷰	설계 설명 능력	자신의 코드 리뷰 및 예상 질문 답변 연습
14일	최종 복습 & 휴식	마인드 컨트롤	핵심 개념 가볍게 복습, 컨디션 조절
*/


/*
1일차: RAII와 스마트 포인터
학습: std::unique_ptr, std::shared_ptr, std::make_unique의 개념과 사용법을 익힌다.24
연습: 생성자/소멸자에서 new/delete를 사용하는 C 스타일 클래스를 std::unique_ptr를 사용하도록 리팩토링한다.
2일차: 이동 의미론 (Move Semantics)
학습: L-value/R-value 참조, std::move의 원리를 이해한다.27
연습: 큰 std::vector를 멤버로 갖는 클래스를 만들고, 복사 생성자와 이동 생성자를 모두 구현한다. 각 생성자가 호출될 때 로그를 출력하여 언제 복사가 일어나고 언제 이동이 일어나는지 확인한다.
3일차: 동시성 프로그래밍 기초
학습: std::thread, std::mutex, std::lock_guard, std::unique_lock, std::condition_variable의 역할을 학습한다.
연습: 뮤텍스로 보호되는 전역 변수를 사용하여, 생산자 스레드가 생성한 정수를 소비자 스레드가 소비하는 간단한 프로그램을 작성한다.
4.일차: Google Test
학습: Google Test Primer 문서를 읽고 TEST, TEST_F, ASSERT_*, EXPECT_* 매크로 사용법을 익힌다.14
연습: 간단한 Calculator 클래스를 만들고, 덧셈, 뺄셈, 곱셈, 나눗셈(0으로 나누는 경우 포함)에 대한 단위 테스트를 작성한다.
5일차: CMake
학습: 기본적인 CMake 튜토리얼을 따라하며 project, add_executable, add_library, target_link_libraries 명령어의 사용법을 익힌다.20
연습: 4일차에 만든 Calculator 프로젝트를 위한 CMakeLists.txt를 작성한다. Calculator 라이브러리와 gtest 실행 파일을 빌드하도록 구성한다.
6일차: 임베디드 개념 복습
학습: 비트 연산(Bit Manipulation) 기술과 RTOS의 핵심 개념(뮤텍스, 세마포어, 우선순위 역전 등)을 복습한다.34
연습: LeetCode에서 비트 연산 관련 쉬운 문제(예: Number of 1 Bits)를 2-3개 푼다.
*/