// SPDX-License-Identifier: MIT

#include <vkui/widgets/views/VkFileTreeView.h>

#include <QAbstractItemView>
#include <QEvent>
#include <QFontMetrics>
#include <QHeaderView>
#include <QPainter>
#include <QPainterPath>
#include <QStyle>
#include <QStyleOptionViewItem>
#include <QVariant>
#include <algorithm>
#include <utility>
#include <vkui/core/VkThemeManager.h>

namespace vkui {

VkFileTreeDelegate::VkFileTreeDelegate(QTreeView* view, QObject* parent)
    : QStyledItemDelegate(parent), m_view(view) {
    Q_ASSERT(m_view != nullptr);
}

VkFileTreeRowPresentation
VkFileTreeDelegate::fileTreePresentation(const QModelIndex& index) const {
    VkFileTreeRowPresentation presentation;
    presentation.directory = index.data(VkFileTreeDirectoryRole).toBool();
    presentation.dropTarget = index.data(VkFileTreeDropTargetRole).toBool();

    const QVariant explicitGlyph = index.data(VkFileTreeGlyphRole);
    if (explicitGlyph.isValid()) {
        presentation.glyph = static_cast<VkFileGlyph>(explicitGlyph.toInt());
    } else if (presentation.directory) {
        presentation.glyph = m_view != nullptr && m_view->isExpanded(index)
                                 ? VkFileGlyph::FolderOpen
                                 : VkFileGlyph::FolderClosed;
    } else if (index.data(VkFileTreeBookRole).toBool()) {
        presentation.glyph = VkFileGlyph::BookFile;
    } else {
        presentation.glyph = fileGlyphForPath(index.data(VkFileTreePathRole).toString());
    }

    const QVariantList tags = index.data(VkFileTreeTagColorsRole).toList();
    presentation.tagColors.reserve(std::min<qsizetype>(3, tags.size()));
    for (const QVariant& tag : tags) {
        const QColor color = tag.value<QColor>();
        if (color.isValid()) {
            presentation.tagColors.append(color);
            if (presentation.tagColors.size() == 3) {
                break;
            }
        }
    }
    return presentation;
}

void VkFileTreeDelegate::paint(QPainter* painter, const QStyleOptionViewItem& option,
                               const QModelIndex& index) const {
    if (painter == nullptr || !index.isValid()) {
        return;
    }
    QStyleOptionViewItem resolved(option);
    initStyleOption(&resolved, index);
    resolved.state &= ~QStyle::State_HasFocus;
    paintFileTreeRow(painter, resolved, fileTreePresentation(index));
}

void VkFileTreeDelegate::paintFileTreeRow(
    QPainter* painter, const QStyleOptionViewItem& option,
    const VkFileTreeRowPresentation& presentation) const {
    if (painter == nullptr) {
        return;
    }

    const auto* fileTree = qobject_cast<const VkFileTreeView*>(m_view.data());
    const qreal devicePixelRatio = painter->device() == nullptr
                                       ? 1.0
                                       : painter->device()->devicePixelRatioF();
    const VkFileIconMetrics metrics =
        fileTree != nullptr && option.font == fileTree->font()
                && qFuzzyCompare(devicePixelRatio,
                                 fileTree->fileTreeMetrics().devicePixelRatio)
            ? fileTree->fileTreeMetrics()
            : fileIconMetrics(option.font, devicePixelRatio);
    const VkTreeItemGeometry geometry = treeItemGeometry(
        option, metrics, HorizontalInset, PillHorizontalPadding);

    constexpr int tagDiameter = 6;
    constexpr int tagGap = 3;
    const int tagCount = static_cast<int>(presentation.tagColors.size());
    const int tagWidth = tagCount == 0
                             ? 0
                             : tagCount * tagDiameter + (tagCount - 1) * tagGap + 5;
    QRect availableTextRect = geometry.availableTextRect;
    QRect paintedTextRect = geometry.textRect;
    if (tagWidth > 0) {
        availableTextRect.adjust(0, 0, -tagWidth, 0);
        paintedTextRect.adjust(0, 0, -tagWidth, 0);
    }

    const QFontMetrics fontMetrics(option.font);
    const QString text = fontMetrics.elidedText(
        option.text, option.textElideMode, std::max(0, availableTextRect.width()));
    const bool selected = option.state.testFlag(QStyle::State_Selected);
    const bool hovered = option.state.testFlag(QStyle::State_MouseOver);
    QColor background = Qt::transparent;
    if (presentation.dropTarget) {
        background = option.palette.color(QPalette::Highlight);
        background.setAlphaF(option.palette.color(QPalette::Window).lightnessF() < 0.5F
                                 ? 0.42F
                                 : 0.22F);
    } else if (selected) {
        background = option.palette.color(QPalette::Highlight);
        background.setAlphaF(option.palette.color(QPalette::Window).lightnessF() < 0.5F
                                 ? 0.34F
                                 : 0.17F);
    } else if (hovered) {
        background = option.palette.color(QPalette::Text);
        background.setAlpha(12);
    }

    painter->save();
    painter->setRenderHint(QPainter::Antialiasing);
    if (background.alpha() > 0 && geometry.pillRect.isValid()) {
        QPainterPath path;
        const qreal radius = VkThemeManager::instance()->theme().metrics().cornerRadiusSmall;
        path.addRoundedRect(QRectF(geometry.pillRect), radius, radius);
        painter->fillPath(path, background);
        if (presentation.dropTarget) {
            QColor outline = option.palette.color(QPalette::Highlight);
            outline.setAlpha(220);
            painter->setPen(QPen(outline, 1.25));
            painter->setBrush(Qt::NoBrush);
            painter->drawPath(path);
        }
    }

    QPalette::ColorGroup colorGroup = QPalette::Inactive;
    if (!option.state.testFlag(QStyle::State_Enabled)) {
        colorGroup = QPalette::Disabled;
    } else if (option.state.testFlag(QStyle::State_Active)) {
        colorGroup = QPalette::Active;
    }
    drawFileGlyph(*painter, QRectF(geometry.iconRect), presentation.glyph, option.palette,
                  presentation.directory ? QPalette::Highlight
                                         : QPalette::PlaceholderText,
                  colorGroup);

    painter->setFont(option.font);
    painter->setPen(option.palette.color(QPalette::Text));
    painter->drawText(paintedTextRect,
                      static_cast<int>(Qt::AlignVCenter
                                       | (option.direction == Qt::RightToLeft
                                              ? Qt::AlignRight
                                              : Qt::AlignLeft)),
                      text);
    if (!presentation.tagColors.isEmpty()) {
        qreal x = geometry.pillRect.right() - 5.0 - tagCount * tagDiameter
                  - (tagCount - 1) * tagGap;
        const qreal y = geometry.iconRect.center().y() - tagDiameter / 2.0;
        painter->setPen(Qt::NoPen);
        for (const QColor& color : presentation.tagColors) {
            painter->setBrush(color);
            painter->drawEllipse(QRectF(x, y, tagDiameter, tagDiameter));
            x += tagDiameter + tagGap;
        }
    }
    painter->restore();
}

int VkFileTreeDelegate::fileTreeRowHeight(const QStyleOptionViewItem& option) const {
    const auto* fileTree = qobject_cast<const VkFileTreeView*>(m_view.data());
    const qreal devicePixelRatio = m_view == nullptr ? 1.0 : m_view->devicePixelRatioF();
    const int metricHeight =
        fileTree != nullptr && option.font == fileTree->font()
                && qFuzzyCompare(devicePixelRatio,
                                 fileTree->fileTreeMetrics().devicePixelRatio)
            ? fileTree->fileTreeMetrics().rowHeight
            : fileIconMetrics(option.font, devicePixelRatio).rowHeight;
    return std::max(MinimumRowHeight, metricHeight);
}

QSize VkFileTreeDelegate::sizeHint(const QStyleOptionViewItem& option,
                                   const QModelIndex& index) const {
    Q_UNUSED(index)
    return {0, fileTreeRowHeight(option)};
}

QRect VkFileTreeDelegate::fileTreeEditorRect(const QStyleOptionViewItem& option) const {
    const auto* fileTree = qobject_cast<const VkFileTreeView*>(m_view.data());
    const VkTreeItemGeometry geometry =
        fileTree == nullptr
            ? treeItemGeometry(option,
                               fileIconMetrics(option.font,
                                               m_view == nullptr
                                                   ? 1.0
                                                   : m_view->devicePixelRatioF()),
                               HorizontalInset, PillHorizontalPadding)
            : fileTree->fileTreeItemGeometry(option);
    return geometry.availableTextRect.adjusted(-3, 1, 0, -1);
}

void VkFileTreeDelegate::updateEditorGeometry(QWidget* editor,
                                              const QStyleOptionViewItem& option,
                                              const QModelIndex& index) const {
    Q_UNUSED(index)
    if (editor != nullptr) {
        editor->setGeometry(fileTreeEditorRect(option));
    }
}

QTreeView* VkFileTreeDelegate::fileTreeView() const noexcept {
    return m_view.data();
}

VkFileTreeView::VkFileTreeView(QWidget* parent) : VkDisclosureTreeView(parent) {
    setHeaderHidden(true);
    setFrameShape(QFrame::NoFrame);
    setRootIsDecorated(false);
    setNativeBranchesVisible(false);
    setUniformRowHeights(true);
    setAlternatingRowColors(false);
    setEditTriggers(QAbstractItemView::NoEditTriggers);
    setSelectionMode(QAbstractItemView::SingleSelection);
    setSelectionBehavior(QAbstractItemView::SelectItems);
    setAllColumnsShowFocus(false);
    setExpandsOnDoubleClick(false);
    setAnimated(false);
    setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    setMouseTracking(true);
    viewport()->setMouseTracking(true);
    viewport()->setAttribute(Qt::WA_Hover, true);
    header()->setStretchLastSection(true);
    setItemDelegate(new VkFileTreeDelegate(this, this));
    refreshFileTreeMetrics();
}

VkFileTreeView::~VkFileTreeView() = default;

const VkFileIconMetrics& VkFileTreeView::fileTreeMetrics() const noexcept {
    return m_fileTreeMetrics;
}

VkTreeItemGeometry VkFileTreeView::fileTreeItemGeometry(
    const QStyleOptionViewItem& option, const int horizontalInset,
    const int pillHorizontalPadding) const {
    const VkFileIconMetrics metrics =
        option.font == font()
                && qFuzzyCompare(devicePixelRatioF(), m_fileTreeMetrics.devicePixelRatio)
            ? m_fileTreeMetrics
            : fileIconMetrics(option.font, devicePixelRatioF());
    return treeItemGeometry(option, metrics, horizontalInset, pillHorizontalPadding);
}

QRect VkFileTreeView::fileTreeIconRect(const QModelIndex& index) const {
    if (!index.isValid()) {
        return {};
    }
    QStyleOptionViewItem option;
    initViewItemOption(&option);
    option.rect = visualRect(index);
    option.text = index.data(Qt::DisplayRole).toString();
    return fileTreeItemGeometry(option).iconRect;
}

void VkFileTreeView::changeEvent(QEvent* event) {
    VkDisclosureTreeView::changeEvent(event);
    if (event == nullptr) {
        return;
    }
    if (event->type() == QEvent::FontChange
        || event->type() == QEvent::ApplicationFontChange
        || event->type() == QEvent::StyleChange
        || event->type() == QEvent::DevicePixelRatioChange) {
        refreshFileTreeMetrics();
    }
}

void VkFileTreeView::setFileTreeMetrics(VkFileIconMetrics metrics, const int indentation) {
    m_fileTreeMetrics = std::move(metrics);
    setIndentation(indentation >= 0
                       ? indentation
                       : m_fileTreeMetrics.glyphSlotSize.width()
                             + m_fileTreeMetrics.textGap);
    setIconSize(m_fileTreeMetrics.glyphSlotSize);
    scheduleDelayedItemsLayout();
    viewport()->update();
}

void VkFileTreeView::refreshFileTreeMetrics() {
    setFileTreeMetrics(fileIconMetrics(font(), devicePixelRatioF()));
}

} // namespace vkui
