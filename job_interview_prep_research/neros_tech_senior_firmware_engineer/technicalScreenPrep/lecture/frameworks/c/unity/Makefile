# Unity + CMock without Ceedling: generate the mock and the test runners with CMock/Unity's Ruby scripts, then cc.
V      := ../_vendor
CC     ?= cc
CFLAGS := -std=c11 -Wall -Wextra -g -I. -I../fw -I$(V)/unity/src -I$(V)/cmock/src -Ibuild/mocks
RUBY   ?= ruby

.PHONY: test clean
test: build/test_rb build/test_sensor
	./build/test_rb
	./build/test_sensor

build/mocks/mock_hal_i2c.c: ../fw/hal_i2c.h cmock.yml
	@mkdir -p build/mocks
	$(RUBY) $(V)/cmock/lib/cmock.rb -ocmock.yml ../fw/hal_i2c.h

build/%_runner.c: %.c
	@mkdir -p build
	$(RUBY) $(V)/unity/auto/generate_test_runner.rb $< $@ cmock.yml

build/test_rb: test_rb.c build/test_rb_runner.c ../fw/rb.c
	$(CC) $(CFLAGS) $^ $(V)/unity/src/unity.c -o $@

build/test_sensor: test_sensor.c build/test_sensor_runner.c ../fw/sensor.c build/mocks/mock_hal_i2c.c
	$(CC) $(CFLAGS) $^ $(V)/unity/src/unity.c $(V)/cmock/src/cmock.c -o $@

clean:
	rm -rf build
