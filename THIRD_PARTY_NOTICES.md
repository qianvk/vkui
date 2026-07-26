# Third-party notices

## Fira Code Nerd Font

`VkUI::Core` embeds `FiraCodeNerdFont-Regular.ttf` from the official Nerd Fonts
release archive. It is derived from Fira Code and includes glyphs from the Nerd
Fonts aggregation project.

The font is registered as an application font on demand and is not installed
into the operating system. The public file-icon API keeps upstream private code
points out of application code. The upstream license and release notes are
preserved in `resources/fonts/`.

- Nerd Fonts: <https://github.com/ryanoasis/nerd-fonts>
- Fira Code: <https://github.com/tonsky/FiraCode>

## Integrated window implementation

`VkUI::Window` contains a migrated and adapted implementation derived from
QWindowKit. The source is compiled directly as a VkUI module; VkUI does not
fetch, link, or install QWindowKit as a separate dependency.

The migrated files retain their original copyright and Apache-2.0 SPDX
headers. A copy of the upstream license is preserved at
`src/window/qwindowkit/LICENSE`. The migration baseline is qianvk/qwindowkit
commit `f593b4d8e13f6c4b954b913758abc9d49bfeb8f2`.

- QWindowKit: <https://github.com/qianvk/qwindowkit>
