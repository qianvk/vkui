# Nerd symbol catalog generator

This temporary developer tool converts the symbol mappings present in a Nerd Font into batched SVG
sprites. It deliberately excludes normal text characters. The generated catalog is consumed only by
the standalone `icon-chosen` application.

The generator reads `hb-info --list-unicodes` output from standard input:

```sh
cmake -S tools/icon-chosen/generator -B build/icon-chosen-generator -G Ninja
cmake --build build/icon-chosen-generator
hb-info --quiet --list-unicodes /path/to/FiraCodeNerdFont-Regular.ttf \
  | build/icon-chosen-generator/nerd_symbol_generator \
      /path/to/FiraCodeNerdFont-Regular.ttf tools/icon-chosen/catalog
```

The source font is a generation-time input and is not linked, embedded, or shipped by VkUI or
`icon-chosen`.
