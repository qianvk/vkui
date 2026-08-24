# Third-party notices

## Nerd Font-derived SVG symbols

VkUI's compact file symbols and the temporary standalone `icon-chosen` catalog
were converted to SVG outlines from Fira Code Nerd Font v3.4.0. No font file is
linked, embedded, installed, or loaded at runtime. The converted outlines remain
covered by the upstream license preserved in
`resources/fonts/LICENSE-FIRACODE-NERD-FONT.txt`.

- Nerd Fonts: <https://github.com/ryanoasis/nerd-fonts>
- Fira Code: <https://github.com/tonsky/FiraCode>

## Retained QWindowKit reference source

The repository retains the previous migrated QWindowKit implementation for
history and implementation comparison. It is not compiled, linked, installed,
or used by the active `VkUI::Window` implementation.

The migrated files retain their original copyright and Apache-2.0 SPDX
headers. A copy of the upstream license is preserved at
`src/window/qwindowkit/LICENSE`. The migration baseline is qianvk/qwindowkit
commit `f593b4d8e13f6c4b954b913758abc9d49bfeb8f2`.

- QWindowKit: <https://github.com/qianvk/qwindowkit>

## Liquid-glass architecture reference

VkUI's liquid-glass module uses an original C++/Qt renderer. Its explicit backdrop-provider and
surface separation, local capture model, and rounded-rectangle lens terminology were informed by
Kyant0's AndroidLiquidGlass project. No AndroidLiquidGlass source or binary is copied, linked, or
distributed by VkUI.

- AndroidLiquidGlass: <https://github.com/Kyant0/AndroidLiquidGlass> (Apache-2.0)
