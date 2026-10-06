"""Gherkin 문장 → Python 함수. {name} 은 문자열 인자, {temp:g} 는 숫자로 파싱."""
from behave import given, then, when

from fake_dut import DutTimeout


@given("a connected DUT in state DISARMED")
def step_connected(context):
    assert context.dut.send("status") == "DISARMED"


@given("the DUT is armed")
def step_armed(context):
    assert context.dut.send("arm") == "OK"


@given("the DUT stops responding")
def step_silent(context):
    context.fw.mute = True


@given("the sensor reads {temp:g} C")
def step_sensor(context, temp):
    context.fw.temp_c = temp


@when('I send "{cmd}"')
def step_send(context, cmd):
    try:
        context.reply, context.error = context.dut.send(cmd), None
    except DutTimeout as e:
        context.reply, context.error = None, e


@then('the reply is "{expected}"')
def step_reply_is(context, expected):
    assert context.reply == expected, f"got {context.reply!r}"


@then('the reply starts with "{prefix}"')
def step_reply_starts(context, prefix):
    assert context.reply.startswith(prefix), f"got {context.reply!r}"


@then('the status is "{state}"')
def step_status(context, state):
    assert context.dut.send("status") == state


@then("the command times out")
def step_timeout(context):
    assert isinstance(context.error, DutTimeout)


@then("the reported temperature is {temp:g} C")
def step_temp(context, temp):
    assert abs(context.dut.temperature() - temp) < 0.05
