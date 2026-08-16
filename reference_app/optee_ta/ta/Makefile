# SPDX-License-Identifier: MIT
CFG_TEE_TA_LOG_LEVEL ?= 3
# C++ in a trusted application needs libgcc, which is what carries the unwinder the arm
# drives. The dev kit refuses a .cpp source without it.
CFG_TA_LIBGCC = y

BINARY = 7b3f9a10-5c62-4d18-9e21-6a4c8f0d37b5

-include $(TA_DEV_KIT_DIR)/mk/ta_dev_kit.mk

# Linking libstdc++ and libgcc's unwinder marks the output ELFOSABI_GNU, which OP-TEE's
# loader refuses before it reads a program header. The identifier is corrected on the
# stripped ELF and the trusted application is signed again from it, both with the
# development kit's own tools. normalize_osabi.py explains why one byte is the whole fix.
JMPXX_UUID = $(BINARY)
JMPXX_OUT ?= $(O)

.PHONY: osabi
osabi: all
	$(PYTHON3) $(CURDIR)/normalize_osabi.py $(JMPXX_OUT)/$(JMPXX_UUID).stripped.elf
	$(PYTHON3) $(TA_DEV_KIT_DIR)/scripts/sign_encrypt.py \
		--key $(TA_DEV_KIT_DIR)/keys/default_ta.pem \
		--uuid $(JMPXX_UUID) --ta-version 0 \
		--in $(JMPXX_OUT)/$(JMPXX_UUID).stripped.elf \
		--out $(JMPXX_OUT)/$(JMPXX_UUID).ta

ifeq ($(wildcard $(TA_DEV_KIT_DIR)/mk/ta_dev_kit.mk), )
clean:
	@echo 'TA_DEV_KIT_DIR=$(TA_DEV_KIT_DIR) does not hold ta_dev_kit.mk'
endif
