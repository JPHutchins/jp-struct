# jp-struct developer tasks. Requires `uv` (https://docs.astral.sh/uv/) on PATH.
#
#   make build     compile the C extension in place (record.*.so)
#   make test      run the test suite
#   make bench     run the import / type-creation / instantiation benchmark
#   make compiledb regenerate build/compile_commands.json for clangd
#   make clean     remove build artifacts (keeps .venv)
#
# The package is intentionally NOT installed into the venv; `build_ext --inplace`
# drops the compiled record.*.so under src/, imported from there (tests via the
# root conftest.py, the benchmark via PYTHONPATH). This keeps the edit-rebuild
# loop trivial: edit src/record.c, `make build`, rerun.

VENV := .venv
PY   := $(VENV)/bin/python

.DEFAULT_GOAL := help

.PHONY: help venv build test bench compiledb clean distclean

help: ## Show this help
	@grep -hE '^[a-zA-Z_-]+:.*?## ' $(MAKEFILE_LIST) \
		| awk 'BEGIN{FS=":.*?## "}{printf "  \033[36m%-9s\033[0m %s\n", $$1, $$2}'

venv: ## Create/sync .venv with build, test, and bench dependencies
	uv sync --no-install-project

build: venv ## Compile the C extension in place (record.*.so)
	$(PY) setup.py build_ext --inplace

test: build ## Run the test suite
	$(PY) -m pytest

bench: build ## Run the import / type-creation / instantiation benchmark
	$(PY) bench/bench.py

compiledb: venv ## Regenerate build/compile_commands.json for clangd (forces a rebuild)
	$(PY) tools/compile_commands.py

clean: ## Remove build artifacts (keeps .venv)
	rm -rf build src/record.*.so record.*.so .pytest_cache
	find . -name '__pycache__' -type d -prune -exec rm -rf {} +

distclean: clean ## Also remove the virtual environment
	rm -rf $(VENV)
