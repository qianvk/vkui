# File-tree frontend

`VkFileTreeView` is VkUI's reusable, model-agnostic filesystem frontend. It
owns compact `QTreeView` configuration, DPI-aware Nerd Font metrics, canonical
icon/text/selection geometry, and the ordinary file-row renderer.

There is intentionally no second disclosure implementation:

```
VkFileTreeView -> VkDisclosureTreeView -> QTreeView
```

`VkDisclosureTreeView` remains the only authority for reversible folder
disclosure and model row insertion, removal, and movement animation. Reader
tables of contents may use it directly; filesystem products use
`VkFileTreeView` and receive exactly the same animation pipeline.

Application modules retain domain policy. They provide models, asynchronous
directory loading, filesystem mutations, metadata, tags, and specialized
source-list or summary-card renderers. A domain delegate can call
`VkFileTreeDelegate::paintFileTreeRow()` for ordinary hierarchy rows without
copying VkUI's geometry or paint code.
