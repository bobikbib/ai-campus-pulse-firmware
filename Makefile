# Host build (laptop): unit tests + simulator.
# The board build is done by ModusToolbox — see docs/INTEGRATION.md.

CC      ?= cc
CFLAGS  ?= -DCP_ENABLE_MASK_COUNTER=1 -std=c11 -O2 -g -Wall -Wextra -Wpedantic -Wshadow -Wconversion -Wno-sign-conversion
SAN     ?= -fsanitize=address,undefined
BUILD   := build

CORE_SRC := src/core/person_counter.c src/core/presence.c src/core/protocol.c src/app/cp_app.c

.PHONY: all test sim cam clean

all: $(BUILD)/cp_tests $(BUILD)/cp_sim

$(BUILD):
	mkdir -p $(BUILD)

$(BUILD)/cp_tests: $(CORE_SRC) tests/test_main.c | $(BUILD)
	$(CC) $(CFLAGS) $(SAN) $^ -o $@

$(BUILD)/cp_sim: $(CORE_SRC) host/sim_main.c | $(BUILD)
	$(CC) $(CFLAGS) $^ -o $@

$(BUILD)/cp_cam: $(CORE_SRC) host/cam_main.c | $(BUILD)
	$(CC) $(CFLAGS) $^ -o $@

cam: $(BUILD)/cp_cam

test: $(BUILD)/cp_tests
	./$(BUILD)/cp_tests

sim: $(BUILD)/cp_sim
	./$(BUILD)/cp_sim

clean:
	rm -rf $(BUILD)
