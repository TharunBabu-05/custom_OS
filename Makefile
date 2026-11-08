SHELL := /bin/bash
.DEFAULT_GOAL := help

.PHONY: help check-tools check-toolchain
help:
	@printf '%s\n' 'Optional Ubuntu checks. Native Windows kernel: scripts/build.ps1 -Target virt' \
	  'make check-tools      Verify required Ubuntu development tools' \
	  'make check-toolchain  Compile and inspect freestanding probes for both boards'

check-tools:
	bash scripts/check-tools.sh

check-toolchain:
	bash scripts/check-toolchain.sh
