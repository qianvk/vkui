// SPDX-License-Identifier: MIT

#pragma once

#include <QSplitter>
#include <vkui/VkUiGlobal.h>

class QSplitterHandle;

namespace vkui {

/**
 * A theme-aware splitter with a minimal visual footprint and a practical grab area.
 *
 * The handle occupies one logical pixel in the layout. Qt expands the mouse-sensitive
 * area of such a thin handle over the adjacent widgets, so resizing remains comfortable
 * without adding a visible gutter between panels.
 */
class VKUI_WIDGETS_EXPORT VSplitter : public QSplitter {
    Q_OBJECT

  public:
    explicit VSplitter(QWidget* parent = nullptr);
    explicit VSplitter(Qt::Orientation orientation, QWidget* parent = nullptr);
    ~VSplitter() override;

  protected:
    QSplitterHandle* createHandle() override;
};

} // namespace vkui
