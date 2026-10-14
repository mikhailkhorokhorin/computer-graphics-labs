ifeq ($(OS),Windows_NT)
PRESET ?= mingw-debug
else
PRESET ?= debug
endif
LAB ?=
APP ?= main

BUILD_DIR := build/$(PRESET)

.DEFAULT_GOAL := help

.PHONY: help configure build run clean

help:
	@echo "make build [PRESET=$(PRESET)] [LAB=labN]   configure and build"
	@echo "make run   LAB=labN [APP=main]            run labN_APP"
	@echo "make clean                                remove build outputs"

configure:
	cmake --preset $(PRESET)

build: configure
	cmake --build --preset $(PRESET) --parallel $(if $(LAB),--target $(LAB))

run:
	@test -n "$(LAB)" || (echo "LAB is required" >&2; exit 2)
	@$(MAKE) --no-print-directory build LAB=$(LAB)
	$(BUILD_DIR)/$(LAB)/$(LAB)_$(APP)

clean:
	rm -rf build
