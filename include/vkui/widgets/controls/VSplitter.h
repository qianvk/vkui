// SPDX-License-Identifier: MIT

#pragma once

#include <QCursor>
#include <QSet>
#include <QSplitter>
#include <vkui/VkUiGlobal.h>

class QSplitterHandle;

namespace vkui {

class VSplitterHandle;

/**
 * A theme-aware splitter with a minimal visual footprint and a practical grab area.
 *
 * The handle owns a stable five-pixel interaction surface while VSplitter paints only
 * its center pixel. Separating input geometry from painting avoids overlapping panel hit regions.
 */
class VKUI_WIDGETS_EXPORT VSplitter : public QSplitter {
    Q_OBJECT

  public:
    explicit VSplitter(QWidget* parent = nullptr);
    explicit VSplitter(Qt::Orientation orientation, QWidget* parent = nullptr);
    ~VSplitter() override;

  signals:
    /** Emitted for a primary or secondary click that did not resize the splitter. */
    void handleClicked(int handleIndex, Qt::MouseButton button);

  protected:
    QSplitterHandle* createHandle() override;

  private:
    friend class VSplitterHandle;
    void acquireCursorGuard(QSplitterHandle* handle);
    void releaseCursorGuard(QSplitterHandle* handle);
    void queueHandleClicked(QSplitterHandle* handle, Qt::MouseButton button);

    QSet<QSplitterHandle*> cursorGuardOwners_;
    QCursor cursorBeforeGuard_;
    bool hadExplicitCursorBeforeGuard_{false};
};

} // namespace vkui
