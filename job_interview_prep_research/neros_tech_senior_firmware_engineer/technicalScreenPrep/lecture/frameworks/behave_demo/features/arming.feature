Feature: Arming over the CLI
  As a pilot I can only arm a disarmed aircraft,
  so that a second "arm" never re-triggers the motors.

  Background:
    Given a connected DUT in state DISARMED

  Scenario: Arm a disarmed DUT
    When I send "arm"
    Then the reply is "OK"
    And the status is "ARMED"

  Scenario: Arming twice is rejected
    Given the DUT is armed
    When I send "arm"
    Then the reply starts with "ERR"

  Scenario: A silent DUT is reported as a timeout
    Given the DUT stops responding
    When I send "ver"
    Then the command times out

  Scenario Outline: Temperature readout matches the sensor
    Given the sensor reads <temp> C
    Then the reported temperature is <temp> C

    Examples:
      | temp  |
      | -40.0 |
      | 23.5  |
      | 85.0  |
