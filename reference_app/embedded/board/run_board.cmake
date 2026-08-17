# SPDX-License-Identifier: MIT
# Boot the firmware under system emulation and read its verdict. The board reports through
# semihosting, and the emulator does not carry the program's exit code, so the printed
# verdict is the result. Semihosting output can arrive on either stream depending on how
# the emulator was built, so both are read.
execute_process(
  COMMAND ${QEMU} -M mps2-an385 -cpu cortex-m3 -nographic -semihosting -kernel ${IMAGE}
  OUTPUT_VARIABLE out ERROR_VARIABLE err TIMEOUT 120)
set(report "${out}${err}")
message("${report}")
if(NOT report MATCHES "storecheck: PASS")
  message(FATAL_ERROR "the board did not report a passing run")
endif()
