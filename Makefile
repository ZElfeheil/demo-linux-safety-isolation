# =====================================================================
# Linux Safety Isolation Demo - Root Delegation Makefile
# =====================================================================

SHELL := /usr/bin/env bash

.PHONY: default all build run clean check-deps kernel-static check-userspace xray help

default: build

all:
	@$(MAKE) -f env/Makefile all

build:
	@$(MAKE) -f env/Makefile build

run:
	@$(MAKE) -f env/Makefile run

clean:
	@$(MAKE) -f env/Makefile clean

check-deps:
	@$(MAKE) -f env/Makefile check-deps

kernel-static:
	@$(MAKE) -f env/Makefile kernel-static

check-userspace:
	@$(MAKE) -f env/Makefile check-userspace

xray:
	@$(MAKE) -f env/Makefile xray

help:
	@$(MAKE) -f env/Makefile help
