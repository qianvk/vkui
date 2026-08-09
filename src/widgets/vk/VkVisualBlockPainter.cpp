#include <vkui/widgets/vk/VkVisualBlockPainter.h>

#include <QFontMetricsF>
#include <QPaintEvent>
#include <QPainter>
#include <QTextBlock>
#include <QTextCursor>
#include <QTextDocument>
#include <QTextLayout>
#include <QWidget>

#include <algorithm>
#include <cmath>
#include <limits>
#include <vector>

namespace vkui::vk::widgets {
namespace {

struct BoundaryGeometry final
{
    QPointF point;
    QPointF layoutOrigin;
    QTextLine textLine;
};

[[nodiscard]] BoundaryGeometry geometryFor(
    const VkVisualBlockPaintContext &context,
    const QTextBlock &block,
    const vkui::vk::DisplayBoundary &boundary,
    const qreal virtualCellWidth)
{
    BoundaryGeometry geometry;
    QTextLayout *const layout = block.layout();
    if (layout == nullptr || context.document == nullptr
        || !context.cursorRect) {
        return geometry;
    }
    const int length = static_cast<int>(
        std::min<qsizetype>(
            block.text().size(),
            std::numeric_limits<int>::max()));
    const int column = std::clamp(
        static_cast<int>(boundary.bufferColumn),
        0,
        length);
    QTextCursor cursor(context.document);
    cursor.setPosition(block.position() + column);
    const QRect caret = context.cursorRect(cursor);
    QTextLine line = layout->lineForTextPosition(column);
    if (!line.isValid() && layout->lineCount() > 0) {
        line = layout->lineAt(layout->lineCount() - 1);
    }
    if (!line.isValid()) {
        return geometry;
    }
    const qreal cursorX = line.cursorToX(column);
    geometry.layoutOrigin = QPointF(
        caret.left() - cursorX,
        caret.top() - line.y());
    geometry.point = QPointF(
        geometry.layoutOrigin.x() + cursorX,
        geometry.layoutOrigin.y() + line.y());
    geometry.textLine = line;

    if (boundary.cellBufferEnd > boundary.bufferColumn
        && boundary.cellDisplayEnd
            > boundary.cellDisplayStart) {
        const int cellEnd = std::clamp(
            static_cast<int>(boundary.cellBufferEnd),
            column,
            length);
        const qreal endX = line.cursorToX(cellEnd);
        const qreal ratio = static_cast<qreal>(
            boundary.displayColumn
            - boundary.cellDisplayStart)
            / static_cast<qreal>(
                boundary.cellDisplayEnd
                - boundary.cellDisplayStart);
        geometry.point.setX(
            geometry.layoutOrigin.x()
            + cursorX
            + (endX - cursorX) * ratio);
    } else if (
        boundary.displayColumn
        > boundary.cellDisplayStart) {
        geometry.point.rx() += static_cast<qreal>(
            boundary.displayColumn
            - boundary.cellDisplayStart)
            * virtualCellWidth;
    }
    return geometry;
}

} // namespace

void paintVkVisualBlockRows(
    QPainter &painter,
    const QPaintEvent *const event,
    const VkVisualBlockPaintContext &context,
    const std::span<const vkui::vk::VisualBlockRow> rows)
{
    if (rows.empty() || context.document == nullptr
        || context.viewport == nullptr || !context.cursorRect) {
        return;
    }
    const QPalette::ColorGroup colorGroup =
        !context.enabled
        ? QPalette::Disabled
        : context.activeWindow
        ? QPalette::Active
        : QPalette::Inactive;
    const QColor background = context.palette.color(
        colorGroup, QPalette::Highlight);
    const QColor foreground = context.palette.color(
        colorGroup, QPalette::HighlightedText);
    const qreal virtualCellWidth = std::max<qreal>(
        1.0,
        QFontMetricsF(context.font, context.viewport)
            .horizontalAdvance(QLatin1Char(' ')));

    for (const vkui::vk::VisualBlockRow &row : rows) {
        const QTextBlock block =
            context.document->findBlockByNumber(
                static_cast<int>(row.line)
                + context.leadingDocumentBlocks);
        if (!block.isValid() || block.layout() == nullptr) {
            continue;
        }
        const BoundaryGeometry left = geometryFor(
            context, block, row.left, virtualCellWidth);
        const BoundaryGeometry right = geometryFor(
            context, block, row.right, virtualCellWidth);
        if (!left.textLine.isValid()
            || !right.textLine.isValid()) {
            continue;
        }

        std::vector<QRectF> rectangles;
        if (left.textLine.lineNumber()
            == right.textLine.lineNumber()) {
            rectangles.emplace_back(
                std::min(left.point.x(), right.point.x()),
                left.point.y(),
                std::abs(right.point.x() - left.point.x()),
                std::max(
                    left.textLine.height(),
                    right.textLine.height()));
        } else {
            QTextLayout *const layout = block.layout();
            for (int lineNumber = left.textLine.lineNumber();
                 lineNumber <= right.textLine.lineNumber();
                 ++lineNumber) {
                const QTextLine line = layout->lineAt(lineNumber);
                if (!line.isValid()) {
                    continue;
                }
                const qreal lineLeft =
                    left.layoutOrigin.x() + line.x();
                const qreal lineRight =
                    lineNumber
                            == right.textLine.lineNumber()
                    ? right.point.x()
                    : left.layoutOrigin.x()
                        + std::max(
                            line.naturalTextWidth(),
                            line.width());
                const qreal selectedLeft =
                    lineNumber
                            == left.textLine.lineNumber()
                    ? left.point.x()
                    : lineLeft;
                rectangles.emplace_back(
                    std::min(selectedLeft, lineRight),
                    left.layoutOrigin.y() + line.y(),
                    std::abs(lineRight - selectedLeft),
                    line.height());
            }
        }

        QTextLayout::FormatRange invertedRange;
        invertedRange.start = static_cast<int>(
            row.selectedBufferStart);
        invertedRange.length = static_cast<int>(
            row.selectedBufferEnd
            - row.selectedBufferStart);
        invertedRange.format.setForeground(foreground);
        for (QRectF rectangle : rectangles) {
            rectangle = rectangle.normalized();
            if (rectangle.width() < 1.0) {
                rectangle.setWidth(1.0);
            }
            const QRect paintRect = rectangle.toAlignedRect();
            if (event != nullptr
                && !event->region().intersects(paintRect)) {
                continue;
            }
            painter.fillRect(rectangle, background);
            if (invertedRange.length <= 0) {
                continue;
            }
            painter.save();
            painter.setClipRect(
                rectangle, Qt::IntersectClip);
            block.layout()->draw(
                &painter,
                left.layoutOrigin,
                {invertedRange});
            painter.restore();
        }
    }
}

} // namespace vkui::vk::widgets
