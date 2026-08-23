# Tree view architecture

`VTreeView` is VkUI's framework-level tree component. It installs the shared
row style, removes native focus frames and branch arrows, expands only from a
single click on an expandable row's leading icon, and owns all structural
animation.

The responsibilities follow Qt's model/view design:

| Layer | Responsibility | Must not own |
| --- | --- | --- |
| Model | Hierarchy, flags, text, icons, and application semantic roles | Pixel geometry, hover state, animation clocks |
| `VTreeView` | Selection and input routing, expansion state, icon hit testing, branch connectors, disclosure and row-mutation animation | Application data formatting, row-specific painting |
| `VTreeItemDelegate` | Per-index presentation, layout, painting, size hints, and editor geometry | Expansion state mutation, timers, animation overlays |

Animation deliberately belongs to the view. A Qt delegate is shared between
many indexes, and `paint()` can be called in any order for any visible item.
Putting timers or per-index animation state in a delegate makes the paint
object lifetime disagree with the model-index lifetime. `VTreeView` instead
captures rows through the delegate currently installed for each index, so a
custom delegate receives the same reversible animation without inheriting any
animation implementation.

Repeated icon activation reverses the active timeline at its current time.
The same overlay, cached rows, scroll interpolation, and eased progress are
retained, including when Qt reports the rapid second press as a double-click
event. No intermediate frame is snapped to either terminal state.

## Standard use

```cpp
auto* tree = new vkui::VTreeView(parent);
auto* model = new QStandardItemModel(tree);

auto* group = new QStandardItem("Foundation");
group->appendRow(new QStandardItem("Theme"));
model->appendRow(group);

tree->setModel(model);
tree->setBranchLinesVisible(true);
```

Use Qt's standard roles wherever they express the data: `DisplayRole`,
`DecorationRole`, `FontRole`, `ForegroundRole`, `SizeHintRole`, and item flags.
`VTreeTrailingTextRole` adds a secondary trailing label. Models should expose
semantic values rather than rectangles or paint instructions.

`BackgroundRole`, `TextAlignmentRole`, and `CheckStateRole` are also honored.
The delegate reserves a separate `checkRect` and handles checkbox input there;
the leading icon remains exclusively responsible for disclosure.

## Custom presentation and layout

For common variants, subclass `VTreeItemDelegate` and override only
`treeItemPresentation()`. For a genuinely different row structure, override
`layoutTreeItem()` and `paintTreeItem()` together:

```cpp
class ProjectTreeDelegate final : public vkui::VTreeItemDelegate {
  public:
    using VTreeItemDelegate::VTreeItemDelegate;

  protected:
    vkui::VTreeItemPresentation treeItemPresentation(
        const QStyleOptionViewItem& option,
        const QModelIndex& index) const override {
        auto result = VTreeItemDelegate::treeItemPresentation(option, index);
        result.trailingText = index.data(ProjectStatusRole).toString();
        return result;
    }

    vkui::VTreeItemLayout layoutTreeItem(
        const QStyleOptionViewItem& option,
        const QModelIndex& index,
        const vkui::VTreeItemPresentation& presentation) const override {
        auto layout = VTreeItemDelegate::layoutTreeItem(option, index, presentation);
        // Move or resize leadingRect, textRect, and trailingRect as one contract.
        return layout;
    }
};

tree->setTreeItemDelegate(new ProjectTreeDelegate(tree, tree));
```

`VTreeView` asks the installed `VTreeItemDelegate` for `leadingRect` during
hit testing. The exact geometry returned by `layoutTreeItem()` is therefore
used for both painting and input, even when every index has a different row
layout. An arbitrary `QAbstractItemDelegate` can still be installed for normal
Qt compatibility, but icon disclosure is intentionally disabled when it does
not implement this geometry contract.

Set `uniformRowHeights(true)` only when every row really has the same height.
It lets `QTreeView` avoid calling `sizeHint()` for every item. Leave it disabled
for per-index heights.
