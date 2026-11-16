# fw_size_check.cmake — POST_BUILD Flash budget gate (script mode, no project):
#   cmake -DSIZE_TOOL=arm-none-eabi-size -DELF=fw.elf -DBUDGET=8192 -P fw_size_check.cmake
# Berkeley size: text (incl. .rodata/.isr_vector) + data (initial values, stored in Flash) = Flash.
execute_process(COMMAND ${SIZE_TOOL} ${ELF} OUTPUT_VARIABLE out RESULT_VARIABLE rc)
if(NOT rc EQUAL 0)
    message(FATAL_ERROR "fw size: '${SIZE_TOOL} ${ELF}' failed (${rc})")
endif()
if(NOT out MATCHES "\n *([0-9]+)[ \t]+([0-9]+)[ \t]+([0-9]+)")
    message(FATAL_ERROR "fw size: cannot parse:\n${out}")
endif()
math(EXPR flash "${CMAKE_MATCH_1} + ${CMAKE_MATCH_2}")
math(EXPR ram "${CMAKE_MATCH_2} + ${CMAKE_MATCH_3}")
if(flash GREATER BUDGET)
    math(EXPR over "${flash} - ${BUDGET}")
    message(FATAL_ERROR "fw size: flash ${flash} / ${BUDGET} B -> over budget by ${over} B")
endif()
message(STATUS "fw size: flash ${flash} / ${BUDGET} B, ram ${ram} B")
