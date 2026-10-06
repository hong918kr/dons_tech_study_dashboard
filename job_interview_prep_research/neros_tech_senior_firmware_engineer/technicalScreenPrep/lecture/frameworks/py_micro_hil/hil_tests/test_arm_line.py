from py_micro_hil.assertions import *
from py_micro_hil.framework_API import *


def setup_group():
    TEST_INFO_MESSAGE("ARM line group: drive pin 17, watch pin 27")


def teardown_group():
    get_RPiGPIO_peripheral().write("arm_led", 0)      # 끝나면 항상 안전 상태로


def test_arm_line_drives_high():
    gpio = get_RPiGPIO_peripheral()
    gpio.write("arm_led", 1)
    TEST_ASSERT_EQUAL(1, gpio.read("arm_led"))


def test_dut_reports_ready_when_armed():
    gpio = get_RPiGPIO_peripheral()
    gpio.write("arm_led", 1)
    TEST_ASSERT_EQUAL(1, gpio.read("dut_ready"))    # 배선을 따라 입력이 올라와야 한다


def test_toggle_returns_low():
    gpio = get_RPiGPIO_peripheral()
    gpio.write("arm_led", 1)
    gpio.toggle("arm_led")
    TEST_ASSERT_EQUAL(0, gpio.read("arm_led"))
