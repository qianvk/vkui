# Development with clangd and Neovim

vkui exports a complete CMake compilation database by default. Public headers are listed on their
owning targets, so clangd can infer the correct Qt, export-definition, language-standard, and private
include context when a header is opened.

Configure the development preset once:

```sh
cmake --preset dev
cmake --build --preset dev
```

For Ninja and Makefile builds, the top-level `vkui_clangd_database` target copies the generated
database to the ignored source-root `compile_commands.json`. The checked-in `.clangd` file enables
background indexing, strict missing/unused-include diagnostics, inlay hints, and detailed hover type
aliases. Embedded `add_subdirectory` consumers never write into vkui's source tree.

To use a custom build directory, either keep `VKUI_SYNC_COMPILE_COMMANDS=ON` or point clangd at that
directory manually. Disable database generation with `VKUI_EXPORT_COMPILE_COMMANDS=OFF` when an IDE
provides its own project model.

## Neovim

Neovim 0.11 can start clangd without project-specific Lua:

```lua
vim.lsp.config("clangd", {
  cmd = {
    "clangd",
    "--background-index",
    "--clang-tidy",
    "--completion-style=detailed",
    "--header-insertion=iwyu",
  },
})
vim.lsp.enable("clangd")
```

Open Neovim from the repository root after configuring CMake. `:LspInfo` confirms the active server,
and `:checkhealth vim.lsp` reports missing binaries or configuration problems. Common navigation is
`gd` for definitions, `grr` for references, `K` for hover details, and `<C-]>` for tags-compatible
definition jumps. clangd stores its background index outside the repository.

For Neovim 0.10 and earlier, use the equivalent `clangd.setup` call from `nvim-lspconfig`; the CMake
and `.clangd` portions remain the same.
