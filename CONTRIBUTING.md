# Contributing to vkui

Thank you for helping improve vkui. Bug reports, accessibility findings, platform-specific fixes,
documentation, and focused design proposals are welcome.

## Before opening a change

Open an issue for changes that add public API or alter visual behavior across multiple controls.
The core project boundary is intentional: standard Qt Widgets should be styled, not wrapped or
replaced, and application-specific compositions do not belong in the library.

## Development workflow

1. Configure a Debug build with examples and tests enabled.
2. Keep public Core headers free of QWidget dependencies.
3. Put implementation-only painting and animation helpers under a `private` directory.
4. Add behavior-oriented Qt Test coverage. Avoid making screenshot comparisons the primary test.
5. Run `cmake --build`, `ctest --output-on-failure`, and clang-format validation.
6. Verify both light and dark appearance, keyboard use, high DPI, and right-to-left layout when the
   change affects UI behavior.

Every C++ source and header begins with `// SPDX-License-Identifier: MIT`. Comments are written in
English and should explain ownership, lifetime, design constraints, or non-obvious geometry—not
restate the next line of code.

The development preset refreshes a root `compile_commands.json` for clangd and Neovim. Editor setup
and header-indexing details are documented in [docs/development.md](docs/development.md).

By contributing, you agree that your contribution is licensed under the MIT License and that you
will follow the [Code of Conduct](CODE_OF_CONDUCT.md).
