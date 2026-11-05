# ==============================================================================
# QNX-CortexCage: Master Multi-Architecture Makefile
# Supported Architectures: x86_64, aarch64le (ARM64)
# Target OS: QNX Neutrino RTOS SDP 8.0
# ==============================================================================

CC_X86     = qcc -V12.2.0,gcc_ntox86_64
CC_ARM     = qcc -V12.2.0,gcc_ntoaarch64le
CFLAGS     = -Wall -Wextra -O2 -Iinclude
LDFLAGS    = -lm

SRC_DIR    = src
BIN_X86    = bin/x86_64
BIN_ARM    = bin/aarch64le

TARGETS    = cortex_gate_resmgr ai_neural_planner safety_actuator_loop watchdog_supervisor telemetry_bridge

X86_BINS   = $(addprefix $(BIN_X86)/, $(TARGETS))
ARM_BINS   = $(addprefix $(BIN_ARM)/, $(TARGETS))

.PHONY: all x86_64 aarch64le verify clean

all: x86_64 aarch64le verify

x86_64: $(X86_BINS)

aarch64le: $(ARM_BINS)

$(BIN_X86)/%: $(SRC_DIR)/%.c
	@mkdir -p $(BIN_X86)
	$(CC_X86) $(CFLAGS) $< -o $@ $(LDFLAGS)
	@echo "[BUILD-x86_64] Compiled $@"

$(BIN_ARM)/%: $(SRC_DIR)/%.c
	@mkdir -p $(BIN_ARM)
	$(CC_ARM) $(CFLAGS) $< -o $@ $(LDFLAGS)
	@echo "[BUILD-aarch64le] Compiled $@"

verify: x86_64 aarch64le
	@echo "=========================================================="
	@echo "Verifying QNX 8.0 ELF Architecture & Symbols..."
	@echo "=========================================================="
	@ntox86_64-readelf -h $(BIN_X86)/cortex_gate_resmgr > $(BIN_X86)/ELF_VERIFICATION_PROOF.txt
	@echo "\n--- Dynamic Symbols & Imports ---" >> $(BIN_X86)/ELF_VERIFICATION_PROOF.txt
	@ntox86_64-readelf -s $(BIN_X86)/cortex_gate_resmgr | grep -E "ClockCycles|resmgr|MsgSend|shm" >> $(BIN_X86)/ELF_VERIFICATION_PROOF.txt || true
	@ntoaarch64-readelf -h $(BIN_ARM)/cortex_gate_resmgr > $(BIN_ARM)/ELF_VERIFICATION_PROOF.txt
	@echo "\n--- Dynamic Symbols & Imports ---" >> $(BIN_ARM)/ELF_VERIFICATION_PROOF.txt
	@ntoaarch64-readelf -s $(BIN_ARM)/cortex_gate_resmgr | grep -E "ClockCycles|resmgr|MsgSend|shm" >> $(BIN_ARM)/ELF_VERIFICATION_PROOF.txt || true
	@echo "[VERIFY] Generated ELF proof files in bin/x86_64 and bin/aarch64le."

clean:
	rm -f $(BIN_X86)/* $(BIN_ARM)/*
	@echo "[CLEAN] Cleaned all target binaries."
