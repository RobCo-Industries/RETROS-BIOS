# Contributing to RETROS-BIOS

## Reporting bugs

A good report holds:

- What you did, what you expected and what happened.
- The board, or the QEMU board and QEMU version.
- The commit of RETROS-BIOS and of `external/robco-core`.
- The serial output, with the `@RC` marker lines.

## Changes

1. Open an issue first for a large change.
2. Branch from `main`.
3. Make the change and its tests.
4. Run `make CI=1 all test-host` and `make test`. Both must pass with no warning.
5. Update the docs and `CHANGELOG.md`.
6. Open a pull request.

Commit messages: a short summary in the imperative, a blank line, then what and why.

## Rules

- The boot contract is RobCo-Core's [BOOT_PROTOCOL.md](https://github.com/RobCo-Industries/RobCo-Core/blob/25f36d82972858554274b6799c00d0259522d56b/docs/spec/BOOT_PROTOCOL.md). Change the spec in RobCo-Core first, then follow it here.
- Code with no hardware access goes in a pure file listed in `PURE_SRCS`, with a host test.
- Screen text lives in `src/bootlines.c`. Mark lore text as lore.
- A test that starts a subprocess builds its environment with `minimal_env()` from RobCo-Core's `tests/harness/env.py`.
- After you move the submodule, update the commit and the image tag in `.github/workflows/ci.yml` and the links in the docs. `make check-pins` checks them.
