# File-tree frontend

`VFileTreeView` is the filesystem specialization of VkUI's unified tree
framework:

```text
VFileTreeView -> VTreeView -> QTreeView
VFileTreeDelegate -> VTreeItemDelegate -> QStyledItemDelegate
```

`VTreeView` remains the only expansion, input, connector, and animation
authority. `VFileTreeView` adds font-relative SVG metrics, open and closed
folder symbols, path-based file symbols, optional tags and drop-target visuals,
and enables hierarchy connector lines by default. It owns no filesystem I/O.

Applications provide a model and semantic file roles:

```cpp
auto* tree = new vkui::VFileTreeView(parent);
auto* model = new QStandardItemModel(tree);

auto* folder = new QStandardItem("Source");
folder->setData(true, vkui::VFileTreeDirectoryRole);

auto* file = new QStandardItem("VTreeView.cpp");
file->setData(false, vkui::VFileTreeDirectoryRole);
file->setData("src/widgets/views/VTreeView.cpp", vkui::VFileTreePathRole);
folder->appendRow(file);

model->appendRow(folder);
tree->setModel(model);
```

The delegate resolves a directory glyph from `isExpanded()` at paint time, so
the model never duplicates view state by manually swapping open and closed
icons. A single click inside `fileTreeIconRect()` triggers the reversible
disclosure animation; clicking the label only selects or activates the item.

Domain delegates may derive from `VFileTreeDelegate` and override
`fileTreePresentation()` for semantic leading text, glyph, tags, or drop state.
They inherit the shared layout and painting template, while animation remains
automatically supplied by `VTreeView`.

Titlebars and other chrome belong outside the item view. Row geometry stays in
the viewport coordinate system owned by `QTreeView`; applications must not
translate rows in `drawRow()` and compensate with an additional paint pass,
because that would make Qt's culling, `visualRect()`, `indexAt()`, hover, and
input geometry disagree.
