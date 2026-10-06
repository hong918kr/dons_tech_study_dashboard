*** Settings ***
Documentation     FC CLI smoke test — keyword-driven. 테스트 엔지니어가 아닌 사람도 읽고 고칠 수 있게.
Library           DutLibrary.py
Suite Setup       Open DUT    sim
Suite Teardown    Close DUT
Test Teardown     Send Command    disarm

*** Variables ***
${EXPECTED_FW}    1.4.2

*** Test Cases ***
Firmware Version Is Released Version
    [Tags]    smoke
    Firmware Version Should Be    ${EXPECTED_FW}

Arm Then Status Reports Armed
    [Tags]    smoke    arming
    ${reply}=    Send Command    arm
    Should Be Equal    ${reply}    OK
    ${state}=    Send Command    status
    Should Be Equal    ${state}    ARMED

Double Arm Is Rejected
    [Tags]    arming
    Send Command    arm
    ${reply}=    Send Command    arm
    Should Start With    ${reply}    ERR

Temperature Within Operating Range
    [Template]    Temperature Case
    -40    -41    -39
    23.5   20     30
    85     84     86

Silent DUT Times Out
    [Tags]    fault
    Make DUT Silent
    Command Should Time Out    ver
    [Teardown]    Make DUT Responsive

*** Keywords ***
Temperature Case
    [Arguments]    ${set}    ${low}    ${high}
    Set Simulated Temperature    ${set}
    Temperature Should Be Between    ${low}    ${high}
