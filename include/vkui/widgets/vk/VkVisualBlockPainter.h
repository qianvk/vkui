#pragma once

#include <vkui/vk/VkTypes.h>

#include <QFont>
#include <QPalette>
#include <QRect>

#include <functional>
#include <span>

class QPaintEvent;
class QPainter;
class QTextCursor;
class QTextDocument;
class QWidget;

namespace vkui::vk::widgets {

/**
 * Widget-neutral input needed to paint a Core display-cell block selection.
 *
 * QTextEdit and QPlainTextEdit expose the same document/cursor geometry but
 * do not share a public concrete base for cursorRect(). Keeping that one
 * lookup as a callback lets both surfaces use exactly the same renderer.
 */
struct VkVisualBlockPaintContext final
{
    QTextDocument *document = nullptr;
    QWidget *viewport = nullptr;
    QFont font;
    QPalette palette;
    bool enabled = true;
    bool activeWindow = true;
    int leadingDocumentBlocks = 0;
    std::function<QRect(const QTextCursor &)> cursorRect;
};

void paintVkVisualBlockRows(
    QPainter &painter,
    const QPaintEvent *event,
    const VkVisualBlockPaintContext &context,
    std::span<const vkui::vk::VisualBlockRow> rows);

} // namespace vkui::vk::widgets
