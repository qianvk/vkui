// SPDX-License-Identifier: MIT

#include "private/VStylePainter_p.h"
#include "private/VStyle_p.h"
#include "private/VkPopupSurfaceStyler_p.h"
#include "private/VkThemeRefreshCoordinator_p.h"
#include "private/VkWidgetTypographyController_p.h"

#include <QAbstractButton>
#include <QAbstractItemView>
#include <QAbstractSpinBox>
#include <QApplication>
#include <QComboBox>
#include <QCursor>
#include <QDialogButtonBox>
#include <QFontMetrics>
#include <QFrame>
#include <QLayout>
#include <QPainter>
#include <QSlider>
#include <QStyleFactory>
#include <QStyleHintReturn>
#include <QStyleOptionButton>
#include <QStyleOptionComboBox>
#include <QStyleOptionComplex>
#include <QStyleOptionFrame>
#include <QStyleOptionGroupBox>
#include <QStyleOptionHeader>
#include <QStyleOptionMenuItem>
#include <QStyleOptionProgressBar>
#include <QStyleOptionSlider>
#include <QStyleOptionSpinBox>
#include <QStyleOptionTab>
#include <QStyleOptionToolButton>
#include <QStyleOptionViewItem>
#include <QWidget>
#include <algorithm>
#include <array>
#include <cmath>
#include <core/icons/private/VkResourceInitializer_p.h>
#include <vkui/core/VkIcon.h>
#include <vkui/core/VkTheme.h>
#include <vkui/core/VkThemeManager.h>
#include <vkui/widgets/VCombobox.h>
#include <vkui/widgets/VControlSize.h>
#include <vkui/widgets/style/VStyle.h>

namespace {

bool hasState(const QStyleOption* option, QStyle::StateFlag state) {
    return option && option->state.testFlag(state);
}

struct InteractionState final {
    qreal hover = 0.0;
    qreal press = 0.0;
    qreal focus = 0.0;
    qreal selection = 0.0;
};

InteractionState interactionState(const QStyleOption* option, bool selected = false) {
    const bool keyboardFocus = hasState(option, QStyle::State_HasFocus) &&
                               hasState(option, QStyle::State_KeyboardFocusChange);
    return {
        hasState(option, QStyle::State_MouseOver) ? 1.0 : 0.0,
        hasState(option, QStyle::State_Sunken) ? 1.0 : 0.0,
        keyboardFocus ? 1.0 : 0.0,
        selected ? 1.0 : 0.0,
    };
}

bool usesDiscreteSliderStyle(const QStyleOptionSlider& slider) {
    return slider.tickPosition != QSlider::NoTicks && slider.maximum > slider.minimum;
}

int discreteSliderGrooveOffset(const QStyleOptionSlider& slider,
                               const vkui::VkMetricTokens& metrics) {
    const int offset = std::max(1, qRound(metrics.spacing2));
    if (slider.tickPosition == QSlider::TicksBelow) {
        return -offset;
    }
    if (slider.tickPosition == QSlider::TicksAbove) {
        return offset;
    }
    return 0;
}

bool isDestructiveDialogButton(const QWidget* widget) {
    auto* button = qobject_cast<const QAbstractButton*>(widget);
    if (!button) {
        return false;
    }
    for (const QWidget* ancestor = widget->parentWidget(); ancestor;
         ancestor = ancestor->parentWidget()) {
        if (auto* buttonBox = qobject_cast<const QDialogButtonBox*>(ancestor)) {
            return buttonBox->buttonRole(const_cast<QAbstractButton*>(button)) ==
                   QDialogButtonBox::DestructiveRole;
        }
    }
    return false;
}

bool usesVkUiFocusRing(const QWidget* widget) {
    if (!widget) {
        return false;
    }
    static constexpr std::array classes = {
        "QPushButton",  "QToolButton", "QLineEdit",        "QCheckBox",
        "QRadioButton", "QSlider",     "QAbstractSpinBox", "QScrollBar",
    };
    return qobject_cast<const vkui::VCombobox*>(widget) ||
           std::ranges::any_of(
               classes, [widget](const char* className) { return widget->inherits(className); });
}

QRectF insetRect(const QRect& rect, qreal inset) {
    return QRectF(rect).adjusted(inset, inset, -inset, -inset);
}

bool isEmbeddedEditor(const QWidget* widget) {
    const QWidget* parent = widget ? widget->parentWidget() : nullptr;
    return widget && widget->inherits("QLineEdit") && parent &&
           (qobject_cast<const QAbstractSpinBox*>(parent) ||
            qobject_cast<const vkui::VCombobox*>(parent));
}

Qt::ArrowType arrowTypeForPrimitive(QStyle::PrimitiveElement element) {
    switch (element) {
    case QStyle::PE_IndicatorArrowLeft:
        return Qt::LeftArrow;
    case QStyle::PE_IndicatorArrowRight:
        return Qt::RightArrow;
    case QStyle::PE_IndicatorArrowUp:
        return Qt::UpArrow;
    case QStyle::PE_IndicatorArrowDown:
    default:
        return Qt::DownArrow;
    }
}

QColor textSelectionColor(const vkui::VkTheme& theme, bool active) {
    QColor color = active ? theme.colors().accent : theme.colors().accentHovered;
    const bool dark = theme.effectiveAppearance() == vkui::VkAppearance::Dark;
    color.setAlphaF(dark ? (active ? 0.34F : 0.24F) : (active ? 0.22F : 0.16F));
    return color;
}

bool indexIsUnderCursor(const QStyleOptionViewItem& option, const QWidget* widget) {
    const auto* viewport = widget;
    const auto* view =
        qobject_cast<const QAbstractItemView*>(viewport ? viewport->parentWidget() : nullptr);
    if (!viewport || !view) {
        return hasState(&option, QStyle::State_MouseOver);
    }
    const QPoint localPos = viewport->mapFromGlobal(QCursor::pos());
    return viewport->rect().contains(localPos) && view->indexAt(localPos) == option.index;
}

struct ComboBoxMenuMetrics final {
    int leadingMargin = 0;
    int leadingColumnWidth = 0;
    int columnGap = 0;
    int rightMargin = 0;
    int rowHeight = 0;
    qreal checkmarkExtent = 0.0;
    qreal checkmarkStrokeWidth = 0.0;
};

ComboBoxMenuMetrics comboBoxMenuMetrics(const QFontMetrics& fontMetrics,
                                        const vkui::VCombobox& comboBox,
                                        const vkui::VkMetricTokens& metrics) {
    const qreal fontHeight = std::max<qreal>(1.0, fontMetrics.height());
    const int defaultIconExtent = qRound(metrics.controlHeightSmall * 0.67);
    const QSize configuredIconSize = comboBox.iconSize().isValid()
                                         ? comboBox.iconSize()
                                         : QSize(defaultIconExtent, defaultIconExtent);
    const int leadingColumnWidth = std::max(qCeil(fontHeight * 0.82), configuredIconSize.width());

    return {
        std::max(qRound(metrics.spacing4), qCeil(fontHeight * 0.28)),
        leadingColumnWidth,
        std::max(qRound(metrics.spacing2), qCeil(fontHeight * 0.18)),
        std::max(qRound(metrics.spacing8), qCeil(fontHeight * 0.48)),
        std::max(qRound(metrics.controlHeightRegular), qCeil(fontHeight + metrics.spacing12)),
        std::min<qreal>(leadingColumnWidth * 0.68,
                        std::max(metrics.fixedControlExtentSmall * 0.55, fontHeight * 0.62)),
        std::max<qreal>(1.25, fontHeight * 0.09),
    };
}

int comboBoxMenuHorizontalPadding(const ComboBoxMenuMetrics& metrics) {
    return metrics.leadingMargin + metrics.leadingColumnWidth + metrics.columnGap +
           metrics.rightMargin;
}

int comboBoxCollapsedHorizontalPadding(const vkui::VkMetricTokens& metrics) {
    // Keep the label clear of the dedicated double-chevron column. This must mirror
    // SC_ComboBoxEditField so Qt's contents-based size hints describe the painted control.
    return qRound(metrics.spacing8 + metrics.controlHeightRegular + metrics.spacing4);
}

QString comboBoxMenuText(const QString& source) {
    QString text = source.section(u'\t', 0, 0);
    text.replace(QStringLiteral("&&"), QStringLiteral("&"));
    return text;
}

QFont fontWithSizeOf(QFont font, const QFont& sizeSource) {
    if (sizeSource.pointSizeF() > 0.0) {
        font.setPointSizeF(sizeSource.pointSizeF());
    } else if (sizeSource.pixelSize() > 0) {
        font.setPixelSize(sizeSource.pixelSize());
    }
    return font;
}

enum class PopupItemChrome : quint8 {
    Normal,
    Highlighted,
    Separator,
};

PopupItemChrome drawPopupItemChrome(const QStyleOptionMenuItem& option, QPainter& painter,
                                    const vkui::VkTheme& theme) {
    const auto& colors = theme.colors();
    const auto& metrics = theme.metrics();

    painter.save();
    painter.setClipRect(option.rect);
    if (option.menuItemType == QStyleOptionMenuItem::Separator) {
        const qreal y = option.rect.center().y() + 0.5;
        vkui::VStylePainter::drawHairline(
            painter, QPointF(option.rect.left() + metrics.spacing8, y),
            QPointF(option.rect.right() - metrics.spacing8, y), colors.separator);
        painter.restore();
        return PopupItemChrome::Separator;
    }

    const bool highlighted = option.state.testFlag(QStyle::State_Enabled) &&
                             option.state.testFlag(QStyle::State_Selected);
    if (highlighted) {
        vkui::VStylePainter::drawRoundedPanel(painter, insetRect(option.rect, metrics.spacing2),
                                              metrics.cornerRadiusSmall, colors.accent,
                                              Qt::transparent, 0.0);
    }
    painter.restore();
    return highlighted ? PopupItemChrome::Highlighted : PopupItemChrome::Normal;
}

QColor popupItemForeground(const vkui::VkTheme& theme, PopupItemChrome chrome, bool enabled,
                           const QColor& normalForeground) {
    if (!enabled) {
        return theme.colors().textDisabled;
    }
    return chrome == PopupItemChrome::Highlighted
               ? vkui::VStylePainter::contrastingText(theme.colors().accent)
               : normalForeground;
}

void drawComboBoxMenuItem(const QStyleOptionMenuItem& sourceOption, QPainter& painter,
                          const vkui::VCombobox& comboBox, const vkui::VkTheme& theme) {
    QStyleOptionMenuItem option(sourceOption);
    option.font = comboBox.font();
    option.fontMetrics = QFontMetrics(option.font);
    const auto& colors = theme.colors();
    const auto& metrics = theme.metrics();
    const bool enabled = option.state.testFlag(QStyle::State_Enabled);
    const PopupItemChrome chrome = drawPopupItemChrome(option, painter, theme);
    const bool highlighted = chrome == PopupItemChrome::Highlighted;

    painter.save();
    painter.setClipRect(option.rect);
    if (chrome == PopupItemChrome::Separator) {
        painter.restore();
        return;
    }

    const ComboBoxMenuMetrics itemMetrics =
        comboBoxMenuMetrics(option.fontMetrics, comboBox, metrics);
    const int defaultIconExtent = qRound(metrics.controlHeightSmall * 0.67);
    const QSize configuredIconSize = comboBox.iconSize().isValid()
                                         ? comboBox.iconSize()
                                         : QSize(defaultIconExtent, defaultIconExtent);
    int logicalX = option.rect.left() + itemMetrics.leadingMargin;
    const QRect logicalLeadingColumn(logicalX, option.rect.top(), itemMetrics.leadingColumnWidth,
                                     option.rect.height());
    logicalX += itemMetrics.leadingColumnWidth + itemMetrics.columnGap;

    const QRect logicalTextRect(
        logicalX, option.rect.top(),
        std::max(0, option.rect.right() - itemMetrics.rightMargin - logicalX + 1),
        option.rect.height());

    const bool checked = option.checked || option.state.testFlag(QStyle::State_On);
    const QColor foreground = popupItemForeground(
        theme, chrome, enabled,
        option.palette.color(option.palette.currentColorGroup(), QPalette::Text));
    if (checked) {
        const qreal markExtent =
            std::min<qreal>(itemMetrics.checkmarkExtent, option.rect.height() - metrics.spacing8);
        QRectF markRect(0.0, 0.0, markExtent, markExtent);
        const QRect visualLeadingColumn =
            QStyle::visualRect(option.direction, option.rect, logicalLeadingColumn);
        markRect.moveCenter(QRectF(visualLeadingColumn).center());
        const QColor checkmarkColor = !enabled      ? colors.textDisabled
                                      : highlighted ? foreground
                                                    : colors.accent;
        vkui::VStylePainter::drawCheckmark(painter, markRect, checkmarkColor,
                                           itemMetrics.checkmarkStrokeWidth);
    } else if (!option.icon.isNull()) {
        const QRect visualLeadingColumn =
            QStyle::visualRect(option.direction, option.rect, logicalLeadingColumn);
        const QSize requested = configuredIconSize.boundedTo(visualLeadingColumn.size());
        const QIcon::Mode mode = !enabled      ? QIcon::Disabled
                                 : highlighted ? QIcon::Selected
                                               : QIcon::Normal;
        const QIcon::State state = QIcon::Off;
        const QSize actual = option.icon.actualSize(requested, mode, state);
        const QRect iconRect =
            QStyle::alignedRect(option.direction, Qt::AlignCenter, actual, visualLeadingColumn);
        option.icon.paint(&painter, iconRect, Qt::AlignCenter, mode, state);
    }

    QString text = comboBoxMenuText(option.text);
    const QRect textRect = QStyle::visualRect(option.direction, option.rect, logicalTextRect);
    text = option.fontMetrics.elidedText(text, comboBox.elideMode(), textRect.width());
    painter.setFont(option.font);
    painter.setPen(enabled ? foreground : colors.textDisabled);
    painter.drawText(
        textRect, Qt::AlignLeading | Qt::AlignVCenter | Qt::TextSingleLine | Qt::TextHideMnemonic,
        text);
    painter.restore();
}

} // namespace

namespace vkui {

VStylePrivate::VStylePrivate(VStyle* owner)
    : q(owner), popupSurfaces(new VkPopupSurfaceStyler(owner)),
      typography(new VkWidgetTypographyController(owner)) {}

VStylePrivate::~VStylePrivate() = default;

VStyle::VStyle()
    : QProxyStyle(QStyleFactory::create(QStringLiteral("Fusion"))),
      d(std::make_unique<VStylePrivate>(this)) {}

VStyle::~VStyle() = default;

void VStyle::drawPrimitive(PrimitiveElement element, const QStyleOption* option, QPainter* painter,
                           const QWidget* widget) const {
    if (!option || !painter) {
        return;
    }

    const VkTheme& theme = VkThemeManager::instance()->theme();
    const auto& colors = theme.colors();
    const auto& metrics = theme.metrics();
    if ((element == PE_PanelLineEdit || element == PE_FrameLineEdit) && isEmbeddedEditor(widget)) {
        // QAbstractSpinBox and editable QComboBox own the only frame. Their private QLineEdit is
        // deliberately surface-less so nested editors never produce a second rounded rectangle.
        return;
    }
    const bool isStyledPopupPart = d->popupSurfaces->isPopupPart(widget);
    if (isStyledPopupPart &&
        (element == PE_Frame || element == PE_FrameMenu || element == PE_PanelMenu)) {
        if (element == PE_PanelMenu && VkPopupSurfaceStyler::isPopupContainer(widget)) {
            d->popupSurfaces->drawPopupSurface(*widget, *painter);
        }
        // PE_PanelMenu owns the surface. Suppressing QFrame's frame primitives prevents a
        // second border from being composited over the same popup window.
        return;
    }
    const bool enabled = hasState(option, State_Enabled);
    const bool selected = hasState(option, State_On) || hasState(option, State_Selected);
    const bool selectionTarget = selected || hasState(option, State_NoChange);
    const InteractionState progress = interactionState(option, selectionTarget);

    switch (element) {
    case PE_PanelButtonCommand: {
        const auto* button = qstyleoption_cast<const QStyleOptionButton*>(option);
        const bool destructiveButton = isDestructiveDialogButton(widget);
        const bool defaultButton =
            button && button->features.testFlag(QStyleOptionButton::DefaultButton);
        const qreal emphasis = (defaultButton || destructiveButton) ? 1.0 : progress.selection;
        const QColor emphasizedFill = destructiveButton ? colors.destructive : colors.accent;
        const QColor emphasizedHover =
            destructiveButton ? VStylePainter::mix(colors.destructive, colors.textPrimary, 0.12)
                              : colors.accentHovered;
        const QColor emphasizedPress =
            destructiveButton
                ? VStylePainter::mix(colors.destructive, colors.contentBackground, 0.18)
                : colors.accentPressed;
        QColor fill = VStylePainter::mix(colors.controlFill, emphasizedFill, emphasis);
        const QColor hoverFill =
            VStylePainter::mix(colors.controlFillHovered, emphasizedHover, emphasis);
        const QColor pressFill =
            VStylePainter::mix(colors.controlFillPressed, emphasizedPress, emphasis);
        fill = VStylePainter::mix(fill, hoverFill, progress.hover);
        fill = VStylePainter::mix(fill, pressFill, progress.press);
        QColor border = VStylePainter::mix(
            colors.border, VStylePainter::mix(emphasizedFill, colors.borderStrong, 0.22), emphasis);
        if (!enabled) {
            fill = colors.controlFillDisabled;
            border = VStylePainter::multiplyAlpha(colors.border, 0.7);
        }
        VStylePainter::drawRoundedPanel(*painter, QRectF(option->rect), metrics.cornerRadiusRegular,
                                        fill, border, metrics.borderWidth);
        if (progress.focus > 0.0) {
            VStylePainter::drawFocusRing(
                *painter, insetRect(option->rect, metrics.spacing2), metrics.cornerRadiusRegular,
                VStylePainter::multiplyAlpha(VStylePainter::neutralFocusColor(colors.borderStrong),
                                             progress.focus),
                std::max<qreal>(1.0, metrics.borderWidth));
        }
        return;
    }
    case PE_PanelButtonTool: {
        const bool autoRaise = hasState(option, State_AutoRaise);
        if (autoRaise && progress.hover <= 0.0 && progress.press <= 0.0 &&
            progress.selection <= 0.0) {
            return;
        }
        QColor fill =
            VStylePainter::mix(Qt::transparent, colors.controlFillPressed, progress.selection);
        fill = VStylePainter::mix(fill, colors.controlFillHovered, progress.hover);
        fill = VStylePainter::mix(fill, colors.controlFillPressed, progress.press);
        if (!enabled) {
            fill = VStylePainter::multiplyAlpha(colors.controlFillDisabled, 0.55);
        }
        VStylePainter::drawRoundedPanel(*painter, insetRect(option->rect, metrics.spacing2),
                                        metrics.cornerRadiusRegular, fill, Qt::transparent, 0.0);
        if (progress.focus > 0.0) {
            VStylePainter::drawFocusRing(
                *painter, insetRect(option->rect, metrics.spacing2), metrics.cornerRadiusRegular,
                VStylePainter::multiplyAlpha(VStylePainter::neutralFocusColor(colors.borderStrong),
                                             progress.focus),
                std::max<qreal>(1.0, metrics.borderWidth));
        }
        return;
    }
    case PE_PanelLineEdit: {
        QColor fill = enabled ? colors.contentBackground : colors.controlFillDisabled;
        QColor border = VStylePainter::mix(colors.border, colors.borderStrong,
                                           std::max(progress.hover * 0.55, progress.focus));
        VStylePainter::drawRoundedPanel(*painter, QRectF(option->rect), metrics.cornerRadiusRegular,
                                        fill, border, metrics.borderWidth);
        if (progress.focus > 0.0) {
            VStylePainter::drawFocusRing(
                *painter, insetRect(option->rect, metrics.spacing2), metrics.cornerRadiusRegular,
                VStylePainter::multiplyAlpha(VStylePainter::neutralFocusColor(colors.borderStrong),
                                             progress.focus),
                std::max<qreal>(1.0, metrics.borderWidth));
        }
        return;
    }
    case PE_IndicatorCheckBox:
    case PE_IndicatorItemViewItemCheck: {
        const bool partial = hasState(option, State_NoChange);
        QColor fill =
            VStylePainter::mix(colors.contentBackground, colors.accent, progress.selection);
        const QColor hoverFill =
            VStylePainter::mix(colors.controlFillHovered, colors.accentHovered, progress.selection);
        const QColor pressFill =
            VStylePainter::mix(colors.controlFillPressed, colors.accentPressed, progress.selection);
        fill = VStylePainter::mix(fill, hoverFill, progress.hover);
        fill = VStylePainter::mix(fill, pressFill, progress.press);
        if (!enabled) {
            fill = colors.controlFillDisabled;
        }
        const QColor selectedBorder = enabled ? colors.accent : colors.border;
        const QColor border =
            VStylePainter::mix(colors.borderStrong, selectedBorder, progress.selection);
        VStylePainter::drawRoundedPanel(*painter, QRectF(option->rect), metrics.cornerRadiusSmall,
                                        fill, border, metrics.borderWidth);
        if ((selected || progress.selection > 0.0) && !partial) {
            QColor mark =
                enabled ? QColor(Qt::white) : VStylePainter::multiplyAlpha(Qt::white, 0.48);
            mark = VStylePainter::multiplyAlpha(mark, progress.selection);
            VStylePainter::drawCheckmark(*painter, insetRect(option->rect, 2.5), mark,
                                         std::max<qreal>(1.5, metrics.borderWidth * 1.7));
        } else if (partial) {
            QColor mark =
                enabled ? QColor(Qt::white) : VStylePainter::multiplyAlpha(Qt::white, 0.48);
            mark = VStylePainter::multiplyAlpha(mark, progress.selection);
            VStylePainter::drawHairline(*painter,
                                        QPointF(option->rect.left() + option->rect.width() * 0.27,
                                                option->rect.center().y()),
                                        QPointF(option->rect.right() - option->rect.width() * 0.27,
                                                option->rect.center().y()),
                                        mark);
        }
        if (progress.focus > 0.0) {
            VStylePainter::drawFocusRing(
                *painter, QRectF(option->rect), metrics.cornerRadiusSmall,
                VStylePainter::multiplyAlpha(VStylePainter::neutralFocusColor(colors.borderStrong),
                                             progress.focus),
                std::max<qreal>(1.0, metrics.borderWidth));
        }
        return;
    }
    case PE_IndicatorRadioButton: {
        QColor fill =
            VStylePainter::mix(colors.contentBackground, colors.accent, progress.selection);
        const QColor hoverFill =
            VStylePainter::mix(colors.controlFillHovered, colors.accentHovered, progress.selection);
        const QColor pressFill =
            VStylePainter::mix(colors.controlFillPressed, colors.accentPressed, progress.selection);
        fill = VStylePainter::mix(fill, hoverFill, progress.hover);
        fill = VStylePainter::mix(fill, pressFill, progress.press);
        if (!enabled) {
            fill = colors.controlFillDisabled;
        }
        painter->save();
        painter->setRenderHint(QPainter::Antialiasing, true);
        const QRectF radioRect = insetRect(option->rect, metrics.borderWidth * 0.5);
        const QColor selectedBorder = enabled ? colors.accent : colors.border;
        const QColor border =
            VStylePainter::mix(colors.borderStrong, selectedBorder, progress.selection);
        painter->setBrush(fill);
        painter->setPen(QPen(border, metrics.borderWidth));
        painter->drawEllipse(radioRect);
        if (selected || progress.selection > 0.0) {
            painter->setPen(Qt::NoPen);
            QColor mark =
                enabled ? QColor(Qt::white) : VStylePainter::multiplyAlpha(Qt::white, 0.48);
            mark = VStylePainter::multiplyAlpha(mark, progress.selection);
            painter->setBrush(mark);
            const qreal dotInset =
                radioRect.width() * (0.50 - 0.22 * std::clamp(progress.selection, 0.0, 1.0));
            painter->drawEllipse(radioRect.adjusted(dotInset, dotInset, -dotInset, -dotInset));
        }
        painter->restore();
        if (progress.focus > 0.0) {
            VStylePainter::drawFocusRing(
                *painter, QRectF(option->rect), option->rect.width() * 0.5,
                VStylePainter::multiplyAlpha(VStylePainter::neutralFocusColor(colors.borderStrong),
                                             progress.focus),
                std::max<qreal>(1.0, metrics.borderWidth));
        }
        return;
    }
    case PE_FrameFocusRect:
        // Focus is rendered by each supported control using focus-reason-aware,
        // neutral treatment. Suppress Fusion's rectangular blue focus box.
        if (!usesVkUiFocusRing(widget)) {
            QProxyStyle::drawPrimitive(element, option, painter, widget);
        }
        return;
    case PE_PanelMenu:
        VStylePainter::drawRoundedPanel(*painter, QRectF(option->rect), metrics.menuCornerRadius,
                                        colors.elevatedBackground, colors.border,
                                        metrics.borderWidth);
        return;
    case PE_PanelTipLabel:
        VStylePainter::drawRoundedPanel(*painter, QRectF(option->rect), metrics.cornerRadiusRegular,
                                        colors.popoverBackground, colors.border,
                                        metrics.borderWidth);
        return;
    case PE_Frame:
    case PE_FrameGroupBox:
        VStylePainter::drawRoundedPanel(*painter, QRectF(option->rect), metrics.cornerRadiusRegular,
                                        Qt::transparent, colors.border, metrics.borderWidth);
        return;
    case PE_IndicatorArrowLeft:
    case PE_IndicatorArrowRight:
    case PE_IndicatorArrowUp:
    case PE_IndicatorArrowDown:
        VStylePainter::drawChevron(*painter, QRectF(option->rect), arrowTypeForPrimitive(element),
                                   enabled ? colors.symbolSecondary : colors.symbolDisabled,
                                   std::max<qreal>(1.25, metrics.borderWidth * 1.5));
        return;
    default:
        QProxyStyle::drawPrimitive(element, option, painter, widget);
        return;
    }
}

void VStyle::drawControl(ControlElement element, const QStyleOption* option, QPainter* painter,
                         const QWidget* widget) const {
    if (!option || !painter) {
        return;
    }

    const VkTheme& theme = VkThemeManager::instance()->theme();
    const auto& colors = theme.colors();
    const auto& metrics = theme.metrics();
    const bool enabled = hasState(option, State_Enabled);
    const bool hovered = hasState(option, State_MouseOver);
    const bool selected = hasState(option, State_Selected) || hasState(option, State_On);

    switch (element) {
    case CE_PushButton:
        drawControl(CE_PushButtonBevel, option, painter, widget);
        drawControl(CE_PushButtonLabel, option, painter, widget);
        return;
    case CE_PushButtonBevel:
        drawPrimitive(PE_PanelButtonCommand, option, painter, widget);
        return;
    case CE_PushButtonLabel: {
        const auto* button = qstyleoption_cast<const QStyleOptionButton*>(option);
        if (!button) {
            QProxyStyle::drawControl(element, option, painter, widget);
            return;
        }
        QStyleOptionButton copy = *button;
        const bool emphasized = selected || isDestructiveDialogButton(widget) ||
                                copy.features.testFlag(QStyleOptionButton::DefaultButton);
        const QColor emphasizedFill =
            isDestructiveDialogButton(widget) ? colors.destructive : colors.accent;
        copy.palette.setColor(QPalette::ButtonText,
                              enabled ? (emphasized ? VStylePainter::contrastingText(emphasizedFill)
                                                    : colors.textPrimary)
                                      : colors.textDisabled);
        QProxyStyle::drawControl(element, &copy, painter, widget);
        return;
    }
    case CE_ComboBoxLabel: {
        const auto* combo = qstyleoption_cast<const QStyleOptionComboBox*>(option);
        const auto* comboWidget = qobject_cast<const VCombobox*>(widget);
        if (!combo || combo->editable || !comboWidget) {
            QProxyStyle::drawControl(element, option, painter, widget);
            return;
        }

        QRect textRect = subControlRect(CC_ComboBox, combo, SC_ComboBoxEditField, widget);
        painter->save();
        painter->setClipRect(textRect);
        if (!combo->currentIcon.isNull()) {
            const int defaultIconExtent = qRound(metrics.controlHeightSmall * 0.67);
            const QSize requested = combo->iconSize.isValid()
                                        ? combo->iconSize
                                        : QSize(defaultIconExtent, defaultIconExtent);
            const QSize actual = combo->currentIcon.actualSize(requested);
            const QRect iconRect = alignedRect(
                option->direction, Qt::AlignLeading | Qt::AlignVCenter, actual, textRect);
            combo->currentIcon.paint(painter, iconRect, Qt::AlignCenter,
                                     enabled ? QIcon::Normal : QIcon::Disabled);
            const int iconGap = qRound(metrics.spacing6);
            if (option->direction == Qt::LeftToRight) {
                textRect.setLeft(iconRect.right() + 1 + iconGap);
            } else {
                textRect.setRight(iconRect.left() - 1 - iconGap);
            }
        }

        const QFont resolvedFont = comboWidget->font();
        const QFontMetrics resolvedFontMetrics(resolvedFont);
        const QString text = resolvedFontMetrics.elidedText(
            combo->currentText, comboWidget->elideMode(), std::max(0, textRect.width()));
        painter->setFont(resolvedFont);
        painter->setPen(enabled ? colors.textPrimary : colors.textDisabled);
        painter->drawText(textRect, Qt::AlignLeading | Qt::AlignVCenter | Qt::TextSingleLine, text);
        painter->restore();
        return;
    }
    case CE_ProgressBarGroove:
        VStylePainter::drawRoundedPanel(*painter, insetRect(option->rect, metrics.spacing4),
                                        metrics.cornerRadiusSmall, colors.controlFill,
                                        colors.border, metrics.borderWidth);
        return;
    case CE_ProgressBarContents: {
        const auto* progress = qstyleoption_cast<const QStyleOptionProgressBar*>(option);
        if (!progress || progress->maximum <= progress->minimum) {
            QProxyStyle::drawControl(element, option, painter, widget);
            return;
        }
        const qreal ratio = std::clamp(qreal(progress->progress - progress->minimum) /
                                           qreal(progress->maximum - progress->minimum),
                                       0.0, 1.0);
        QRectF groove = insetRect(option->rect, metrics.spacing4 + metrics.borderWidth);
        QRectF fill = groove;
        if (progress->state.testFlag(State_Horizontal)) {
            fill.setWidth(groove.width() * ratio);
            const bool reverse =
                progress->invertedAppearance != (option->direction == Qt::RightToLeft);
            if (reverse) {
                fill.moveRight(groove.right());
            }
        } else {
            fill.setHeight(groove.height() * ratio);
            if (!progress->invertedAppearance) {
                fill.moveBottom(groove.bottom());
            }
        }
        painter->save();
        painter->setRenderHint(QPainter::Antialiasing, true);
        painter->setClipPath(VStylePainter::roundedRectPath(groove, metrics.cornerRadiusSmall));
        painter->fillRect(fill, enabled ? colors.accent : colors.controlFillDisabled);
        painter->restore();
        return;
    }
    case CE_ProgressBarLabel: {
        const auto* progress = qstyleoption_cast<const QStyleOptionProgressBar*>(option);
        if (!progress) {
            QProxyStyle::drawControl(element, option, painter, widget);
            return;
        }
        QStyleOptionProgressBar copy = *progress;
        copy.palette.setColor(QPalette::Text, enabled ? colors.textPrimary : colors.textDisabled);
        QProxyStyle::drawControl(element, &copy, painter, widget);
        return;
    }
    case CE_TabBarTabShape: {
        QColor fill = selected ? colors.elevatedBackground : Qt::transparent;
        if (!selected && hovered) {
            fill = colors.controlFillHovered;
        }
        VStylePainter::drawRoundedPanel(
            *painter, insetRect(option->rect, metrics.spacing2), metrics.cornerRadiusRegular, fill,
            selected ? colors.border : Qt::transparent, selected ? metrics.borderWidth : 0.0);
        if (selected) {
            const QRectF indicator(option->rect.left() + metrics.spacing8,
                                   option->rect.bottom() -
                                       std::max<qreal>(2.0, metrics.borderWidth * 2.0),
                                   option->rect.width() - metrics.spacing16,
                                   std::max<qreal>(2.0, metrics.borderWidth * 2.0));
            painter->fillRect(indicator, colors.accent);
        }
        return;
    }
    case CE_TabBarTabLabel: {
        const auto* tab = qstyleoption_cast<const QStyleOptionTab*>(option);
        if (!tab) {
            QProxyStyle::drawControl(element, option, painter, widget);
            return;
        }
        QStyleOptionTab copy = *tab;
        copy.palette.setColor(QPalette::WindowText,
                              enabled ? (selected ? colors.textPrimary : colors.textSecondary)
                                      : colors.textDisabled);
        QProxyStyle::drawControl(element, &copy, painter, widget);
        return;
    }
    case CE_MenuItem: {
        const auto* menuItem = qstyleoption_cast<const QStyleOptionMenuItem*>(option);
        if (!menuItem) {
            break;
        }
        if (const auto* comboBox = VkPopupSurfaceStyler::owningVCombobox(widget)) {
            drawComboBoxMenuItem(*menuItem, *painter, *comboBox, theme);
            return;
        }
        const PopupItemChrome chrome = drawPopupItemChrome(*menuItem, *painter, theme);
        if (chrome == PopupItemChrome::Separator) {
            return;
        }
        QStyleOptionMenuItem copy = *menuItem;
        copy.font = fontWithSizeOf(copy.font, theme.typography().body);
        copy.fontMetrics = QFontMetrics(copy.font);
        const QColor foreground =
            popupItemForeground(theme, chrome, enabled, colors.textPrimary);
        copy.state.setFlag(State_Selected, false);
        copy.state.setFlag(State_MouseOver, false);
        copy.state.setFlag(State_HasFocus, false);
        copy.palette.setColor(QPalette::Text, foreground);
        copy.palette.setColor(QPalette::ButtonText, foreground);
        copy.palette.setColor(QPalette::Highlight, Qt::transparent);
        copy.palette.setColor(QPalette::HighlightedText, foreground);
        QProxyStyle::drawControl(element, &copy, painter, widget);
        return;
    }
    case CE_HeaderSection: {
        QColor fill = hovered ? colors.controlFillHovered : colors.controlFill;
        if (hasState(option, State_Sunken)) {
            fill = colors.controlFillPressed;
        }
        painter->fillRect(option->rect, fill);
        const qreal x =
            option->direction == Qt::LeftToRight ? option->rect.right() : option->rect.left();
        VStylePainter::drawHairline(*painter, QPointF(x, option->rect.top() + metrics.spacing4),
                                    QPointF(x, option->rect.bottom() - metrics.spacing4),
                                    colors.separator);
        return;
    }
    case CE_HeaderLabel: {
        const auto* header = qstyleoption_cast<const QStyleOptionHeader*>(option);
        if (!header) {
            QProxyStyle::drawControl(element, option, painter, widget);
            return;
        }
        QStyleOptionHeader copy = *header;
        copy.palette.setColor(QPalette::ButtonText,
                              enabled ? colors.textSecondary : colors.textDisabled);
        QProxyStyle::drawControl(element, &copy, painter, widget);
        return;
    }
    case CE_ItemViewItem: {
        const auto* viewItem = qstyleoption_cast<const QStyleOptionViewItem*>(option);
        if (!viewItem) {
            break;
        }
        QStyleOptionViewItem copy = *viewItem;
        if (const auto* comboBox = VkPopupSurfaceStyler::owningVCombobox(widget)) {
            copy.font = comboBox->font();
            copy.fontMetrics = QFontMetrics(copy.font);
        }
        if (selected) {
            QColor selection = colors.accent;
            selection.setAlphaF(theme.effectiveAppearance() == VkAppearance::Dark ? 0.28F : 0.16F);
            VStylePainter::drawRoundedPanel(*painter, insetRect(option->rect, metrics.spacing2),
                                            metrics.cornerRadiusSmall, selection, Qt::transparent,
                                            0.0);
        } else if (hovered && indexIsUnderCursor(*viewItem, widget)) {
            if (d->popupSurfaces->isPopupPart(widget)) {
                VStylePainter::drawRoundedPanel(*painter, insetRect(option->rect, metrics.spacing2),
                                                metrics.cornerRadiusSmall,
                                                colors.controlFillHovered, Qt::transparent, 0.0);
            } else {
                painter->fillRect(option->rect, colors.controlFillHovered);
            }
        }
        copy.state.setFlag(State_Selected, false);
        copy.state.setFlag(State_MouseOver, false);
        if (d->popupSurfaces->isPopupPart(widget)) {
            copy.state.setFlag(State_HasFocus, false);
        }
        copy.palette.setColor(QPalette::Text, enabled ? colors.textPrimary : colors.textDisabled);
        copy.palette.setColor(QPalette::HighlightedText,
                              enabled ? colors.textPrimary : colors.textDisabled);
        QProxyStyle::drawControl(element, &copy, painter, widget);
        return;
    }
    default:
        QProxyStyle::drawControl(element, option, painter, widget);
        return;
    }

    QProxyStyle::drawControl(element, option, painter, widget);
}

void VStyle::drawComplexControl(ComplexControl control, const QStyleOptionComplex* option,
                                QPainter* painter, const QWidget* widget) const {
    if (!option || !painter) {
        return;
    }

    const VkTheme& theme = VkThemeManager::instance()->theme();
    const auto& colors = theme.colors();
    const auto& metrics = theme.metrics();
    const bool enabled = hasState(option, State_Enabled);
    const bool selected = hasState(option, State_On);
    const InteractionState progress = interactionState(option, selected);

    switch (control) {
    case CC_ToolButton: {
        const auto* toolButton = qstyleoption_cast<const QStyleOptionToolButton*>(option);
        if (!toolButton) {
            break;
        }
        drawPrimitive(PE_PanelButtonTool, toolButton, painter, widget);
        QStyleOptionToolButton label = *toolButton;
        label.palette.setColor(QPalette::ButtonText,
                               enabled ? colors.textPrimary : colors.textDisabled);
        drawControl(CE_ToolButtonLabel, &label, painter, widget);
        if (toolButton->features.testFlag(QStyleOptionToolButton::MenuButtonPopup) ||
            toolButton->features.testFlag(QStyleOptionToolButton::HasMenu)) {
            const QRect arrowRect =
                subControlRect(CC_ToolButton, toolButton, SC_ToolButtonMenu, widget);
            VStylePainter::drawChevron(*painter, QRectF(arrowRect), Qt::DownArrow,
                                       enabled ? colors.symbolSecondary : colors.symbolDisabled,
                                       std::max<qreal>(1.25, metrics.borderWidth * 1.5));
        }
        return;
    }
    case CC_ComboBox: {
        const auto* combo = qstyleoption_cast<const QStyleOptionComboBox*>(option);
        if (!combo || !qobject_cast<const VCombobox*>(widget)) {
            break;
        }
        const QRect arrowRect = subControlRect(CC_ComboBox, combo, SC_ComboBoxArrow, widget);
        if (combo->editable) {
            QColor fill = VStylePainter::mix(colors.contentBackground, colors.controlFillHovered,
                                             progress.hover);
            fill = VStylePainter::mix(fill, colors.controlFillPressed, progress.press);
            if (!enabled) {
                fill = colors.controlFillDisabled;
            }
            VStylePainter::drawRoundedPanel(
                *painter, QRectF(option->rect), metrics.comboBoxCornerRadius, fill,
                VStylePainter::mix(colors.border, colors.borderStrong, progress.focus),
                metrics.borderWidth);
        } else {
            const QColor restingSurface = enabled ? colors.controlFill : colors.controlFillDisabled;
            const bool drawFullSurface = enabled && (progress.hover > 0.0 || progress.press > 0.0);
            if (drawFullSurface) {
                const QColor fullSurface =
                    progress.press > 0.0 ? colors.controlFillPressed : restingSurface;
                VStylePainter::drawRoundedPanel(*painter, QRectF(option->rect),
                                                metrics.comboBoxCornerRadius, fullSurface,
                                                Qt::transparent, 0.0);
            } else {
                const qreal arrowSurfaceExtent =
                    std::max<qreal>(0.0, std::min({metrics.controlHeightSmall,
                                                   static_cast<qreal>(arrowRect.width()),
                                                   static_cast<qreal>(arrowRect.height())}));
                QRectF arrowSurface(0.0, 0.0, arrowSurfaceExtent, arrowSurfaceExtent);
                arrowSurface.moveCenter(QRectF(arrowRect).center());
                VStylePainter::drawRoundedPanel(*painter, arrowSurface, arrowSurfaceExtent * 0.5,
                                                restingSurface, Qt::transparent, 0.0);
            }
        }
        const QRectF glyphColumn =
            QRectF(arrowRect).adjusted(metrics.spacing6, 0.0, -metrics.spacing6, 0.0);
        const QRectF upChevron(glyphColumn.left(), glyphColumn.center().y() - 8.0,
                               glyphColumn.width(), 10.0);
        const QRectF downChevron(glyphColumn.left(), glyphColumn.center().y() - 2.0,
                                 glyphColumn.width(), 10.0);
        const QColor symbol = enabled ? colors.symbolSecondary : colors.symbolDisabled;
        const qreal chevronWidth = std::max<qreal>(1.1, metrics.borderWidth * 1.35);
        VStylePainter::drawChevron(*painter, upChevron, Qt::UpArrow, symbol, chevronWidth);
        VStylePainter::drawChevron(*painter, downChevron, Qt::DownArrow, symbol, chevronWidth);
        if (combo->editable && progress.focus > 0.0) {
            VStylePainter::drawFocusRing(
                *painter, insetRect(option->rect, metrics.spacing2), metrics.comboBoxCornerRadius,
                VStylePainter::multiplyAlpha(VStylePainter::neutralFocusColor(colors.borderStrong),
                                             progress.focus),
                std::max<qreal>(1.0, metrics.borderWidth));
        }
        return;
    }
    case CC_SpinBox: {
        const auto* spinBox = qstyleoption_cast<const QStyleOptionSpinBox*>(option);
        if (!spinBox) {
            break;
        }
        QColor fill = VStylePainter::mix(colors.contentBackground, colors.controlFillHovered,
                                         progress.hover * 0.5);
        if (!enabled) {
            fill = colors.controlFillDisabled;
        }
        VStylePainter::drawRoundedPanel(
            *painter, QRectF(option->rect), metrics.cornerRadiusRegular, fill,
            VStylePainter::mix(colors.border, colors.borderStrong, progress.focus),
            metrics.borderWidth);
        const QRect upRect = subControlRect(CC_SpinBox, spinBox, SC_SpinBoxUp, widget);
        const QRect downRect = subControlRect(CC_SpinBox, spinBox, SC_SpinBoxDown, widget);
        VStylePainter::drawChevron(*painter, QRectF(upRect), Qt::UpArrow,
                                   enabled ? colors.symbolSecondary : colors.symbolDisabled,
                                   std::max<qreal>(1.0, metrics.borderWidth * 1.35));
        VStylePainter::drawChevron(*painter, QRectF(downRect), Qt::DownArrow,
                                   enabled ? colors.symbolSecondary : colors.symbolDisabled,
                                   std::max<qreal>(1.0, metrics.borderWidth * 1.35));
        const qreal buttonBoundary =
            option->direction == Qt::LeftToRight ? upRect.left() : upRect.right();
        VStylePainter::drawHairline(
            *painter, QPointF(buttonBoundary, option->rect.top() + metrics.spacing4),
            QPointF(buttonBoundary, option->rect.bottom() - metrics.spacing4), colors.separator);
        if (progress.focus > 0.0) {
            VStylePainter::drawFocusRing(
                *painter, insetRect(option->rect, metrics.spacing2), metrics.cornerRadiusRegular,
                VStylePainter::multiplyAlpha(VStylePainter::neutralFocusColor(colors.borderStrong),
                                             progress.focus),
                std::max<qreal>(1.0, metrics.borderWidth));
        }
        return;
    }
    case CC_Slider: {
        const auto* slider = qstyleoption_cast<const QStyleOptionSlider*>(option);
        if (!slider) {
            break;
        }
        const QRectF handle = subControlRect(CC_Slider, slider, SC_SliderHandle, widget);
        QRectF groove = subControlRect(CC_Slider, slider, SC_SliderGroove, widget);
        if (slider->orientation == Qt::Horizontal) {
            groove.adjust(handle.width() * 0.5, 0.0, -handle.width() * 0.5, 0.0);
        } else {
            groove.adjust(0.0, handle.height() * 0.5, 0.0, -handle.height() * 0.5);
        }
        if (usesDiscreteSliderStyle(*slider)) {
            QColor trackColor = enabled ? colors.borderStrong : colors.controlFillDisabled;
            trackColor.setAlpha(enabled ? 105 : 70);
            VStylePainter::drawRoundedPanel(*painter, groove,
                                            std::min(groove.width(), groove.height()) * 0.5,
                                            trackColor, Qt::transparent, 0.0);

            const int interval =
                slider->tickInterval > 0 ? slider->tickInterval : std::max(1, slider->singleStep);
            const qint64 tickCount =
                (static_cast<qint64>(slider->maximum) - slider->minimum) / interval + 1;
            if (tickCount <= 256) {
                QColor tickColor = enabled ? colors.borderStrong : colors.controlFillDisabled;
                tickColor.setAlpha(enabled ? 95 : 60);
                painter->save();
                painter->setRenderHint(QPainter::Antialiasing, true);
                painter->setPen(Qt::NoPen);
                painter->setBrush(tickColor);
                const int axisSpan = slider->orientation == Qt::Horizontal
                                         ? qRound(groove.width())
                                         : qRound(groove.height());
                for (qint64 value = slider->minimum; value <= slider->maximum; value += interval) {
                    const int position = QStyle::sliderPositionFromValue(
                        slider->minimum, slider->maximum, static_cast<int>(value),
                        std::max(0, axisSpan), slider->upsideDown);
                    const auto drawTick = [&](const qreal direction) {
                        const QPointF center =
                            slider->orientation == Qt::Horizontal
                                ? QPointF(groove.left() + position,
                                          groove.center().y() + direction * metrics.spacing6)
                                : QPointF(groove.center().x() + direction * metrics.spacing6,
                                          groove.top() + position);
                        painter->drawEllipse(center, 1.0, 1.0);
                    };
                    if (slider->tickPosition == QSlider::TicksBothSides) {
                        drawTick(-1.0);
                        drawTick(1.0);
                    } else {
                        drawTick(slider->tickPosition == QSlider::TicksAbove ? -1.0 : 1.0);
                    }
                }
                painter->restore();
            }

            const qreal expansion = progress.hover * 0.5 + progress.press * 0.35;
            const QRectF drawnHandle =
                handle.adjusted(-expansion, -expansion, expansion, expansion);
            QColor shadow = colors.shadow;
            shadow.setAlpha(enabled ? 38 : 18);
            VStylePainter::drawRoundedPanel(
                *painter, drawnHandle.translated(0.0, 1.0).adjusted(-1.0, -1.0, 1.0, 1.0),
                std::min(drawnHandle.width(), drawnHandle.height()) * 0.5 + 1.0, shadow,
                Qt::transparent, 0.0);
            QColor handleBorder = colors.borderStrong;
            handleBorder.setAlpha(enabled ? 72 : 46);
            VStylePainter::drawRoundedPanel(
                *painter, drawnHandle, std::min(drawnHandle.width(), drawnHandle.height()) * 0.5,
                enabled ? colors.elevatedBackground : colors.controlFillDisabled, handleBorder,
                std::max<qreal>(1.0, metrics.borderWidth));
            if (progress.focus > 0.0) {
                VStylePainter::drawFocusRing(
                    *painter,
                    drawnHandle.adjusted(-metrics.spacing2, -metrics.spacing2, metrics.spacing2,
                                         metrics.spacing2),
                    std::min(drawnHandle.width(), drawnHandle.height()) * 0.5 + metrics.spacing2,
                    VStylePainter::multiplyAlpha(
                        VStylePainter::neutralFocusColor(colors.borderStrong), progress.focus),
                    std::max<qreal>(1.0, metrics.borderWidth));
            }
            return;
        }

        QRectF active = groove;
        if (slider->orientation == Qt::Horizontal) {
            if (slider->upsideDown) {
                active.setLeft(handle.center().x());
            } else {
                active.setRight(handle.center().x());
            }
        } else if (slider->upsideDown) {
            active.setTop(handle.center().y());
        } else {
            active.setBottom(handle.center().y());
        }
        VStylePainter::drawRoundedPanel(*painter, groove,
                                        std::min(groove.width(), groove.height()) * 0.5,
                                        colors.controlFill, Qt::transparent, 0.0);
        VStylePainter::drawRoundedPanel(
            *painter, active, std::min(active.width(), active.height()) * 0.5,
            enabled ? colors.accent : colors.controlFillDisabled, Qt::transparent, 0.0);
        const qreal expansion = progress.hover * 1.0 + progress.press * 0.5;
        const QRectF drawnHandle = handle.adjusted(-expansion, -expansion, expansion, expansion);
        VStylePainter::drawRoundedPanel(
            *painter, drawnHandle, std::min(drawnHandle.width(), drawnHandle.height()) * 0.5,
            enabled ? colors.elevatedBackground : colors.controlFillDisabled, colors.borderStrong,
            metrics.borderWidth);
        if (progress.focus > 0.0) {
            VStylePainter::drawFocusRing(
                *painter,
                drawnHandle.adjusted(-metrics.spacing2, -metrics.spacing2, metrics.spacing2,
                                     metrics.spacing2),
                std::min(drawnHandle.width(), drawnHandle.height()) * 0.5 + metrics.spacing2,
                VStylePainter::multiplyAlpha(VStylePainter::neutralFocusColor(colors.borderStrong),
                                             progress.focus),
                std::max<qreal>(1.0, metrics.borderWidth));
        }
        return;
    }
    case CC_ScrollBar: {
        const auto* scrollBar = qstyleoption_cast<const QStyleOptionSlider*>(option);
        if (!scrollBar) {
            break;
        }
        const QRect groove = subControlRect(CC_ScrollBar, scrollBar, SC_ScrollBarGroove, widget);
        const QRect slider = subControlRect(CC_ScrollBar, scrollBar, SC_ScrollBarSlider, widget);
        painter->fillRect(groove, VStylePainter::multiplyAlpha(colors.controlFill, 0.45));
        const qreal inset = progress.hover > 0.0 ? metrics.spacing2 : metrics.spacing4;
        const QRectF thumb = scrollBar->orientation == Qt::Horizontal
                                 ? QRectF(slider).adjusted(0.0, inset, 0.0, -inset)
                                 : QRectF(slider).adjusted(inset, 0.0, -inset, 0.0);
        QColor thumbColor =
            VStylePainter::mix(colors.borderStrong, colors.textTertiary, progress.hover);
        if (progress.press > 0.0) {
            thumbColor = VStylePainter::mix(thumbColor, colors.textSecondary, progress.press);
        }
        VStylePainter::drawRoundedPanel(
            *painter, thumb, std::min(thumb.width(), thumb.height()) * 0.5,
            enabled ? thumbColor : colors.controlFillDisabled, Qt::transparent, 0.0);
        return;
    }
    default:
        QProxyStyle::drawComplexControl(control, option, painter, widget);
        return;
    }

    QProxyStyle::drawComplexControl(control, option, painter, widget);
}

int VStyle::pixelMetric(PixelMetric metric, const QStyleOption* option,
                        const QWidget* widget) const {
    const auto& tokens = VkThemeManager::instance()->theme().metrics();
    switch (metric) {
    case PM_DefaultFrameWidth:
    case PM_SpinBoxFrameWidth:
    case PM_MenuPanelWidth:
        return std::max(1, qRound(tokens.borderWidth));
    case PM_ComboBoxFrameWidth:
        return qobject_cast<const VCombobox*>(widget)
                   ? std::max(1, qRound(tokens.borderWidth))
                   : QProxyStyle::pixelMetric(metric, option, widget);
    case PM_ButtonMargin:
        return qRound(tokens.spacing8);
    case PM_ButtonDefaultIndicator:
        return 0;
    case PM_ButtonIconSize:
    case PM_SmallIconSize:
        return qRound(tokens.controlHeightSmall * 0.67);
    case PM_LargeIconSize:
        return qRound(tokens.controlHeightRegular * 0.86);
    case PM_IndicatorWidth:
    case PM_IndicatorHeight:
    case PM_ExclusiveIndicatorWidth:
    case PM_ExclusiveIndicatorHeight:
        return widget ? vkui::controlExtent(*widget) : vkui::controlExtent(VControlSize::Regular);
    case PM_LayoutLeftMargin:
    case PM_LayoutTopMargin:
    case PM_LayoutRightMargin:
    case PM_LayoutBottomMargin:
        return qRound(tokens.spacing12);
    case PM_LayoutHorizontalSpacing:
    case PM_LayoutVerticalSpacing:
        return qRound(tokens.spacing8);
    case PM_ScrollBarExtent:
        return qRound(tokens.controlHeightSmall * 0.58);
    case PM_ScrollBarSliderMin:
        return qRound(tokens.spacing24);
    case PM_SliderThickness:
        return qRound(tokens.controlHeightSmall);
    case PM_SliderLength:
        return qRound(tokens.controlHeightSmall * 0.72);
    case PM_ProgressBarChunkWidth:
        return qRound(tokens.spacing8);
    case PM_TabBarTabHSpace:
        return qRound(tokens.spacing16);
    case PM_TabBarTabVSpace:
        return qRound(tokens.spacing8);
    case PM_MenuHMargin:
    case PM_MenuVMargin:
        return qRound(tokens.spacing6);
    case PM_MenuButtonIndicator:
        return qRound(tokens.controlHeightSmall * 0.60);
    case PM_HeaderMargin:
        return qRound(tokens.spacing8);
    case PM_FocusFrameHMargin:
    case PM_FocusFrameVMargin:
        return qRound(tokens.spacing2 + std::max<qreal>(1.0, tokens.borderWidth));
    default:
        return QProxyStyle::pixelMetric(metric, option, widget);
    }
}

int VStyle::layoutSpacing(QSizePolicy::ControlType control1, QSizePolicy::ControlType control2,
                          Qt::Orientation orientation, const QStyleOption* option,
                          const QWidget* widget) const {
    Q_UNUSED(control1)
    Q_UNUSED(control2)
    Q_UNUSED(orientation)
    Q_UNUSED(option)
    Q_UNUSED(widget)
    return qRound(VkThemeManager::instance()->theme().metrics().spacing8);
}

QSize VStyle::sizeFromContents(ContentsType type, const QStyleOption* option,
                               const QSize& contentsSize, const QWidget* widget) const {
    QSize result = QProxyStyle::sizeFromContents(type, option, contentsSize, widget);
    const VkTheme& theme = VkThemeManager::instance()->theme();
    const auto& metrics = theme.metrics();
    switch (type) {
    case CT_PushButton:
        result.rwidth() =
            std::max(result.width(), contentsSize.width() + qRound(metrics.spacing24));
        result.rheight() = std::max(result.height(), qRound(metrics.controlHeightRegular));
        break;
    case CT_ToolButton:
        result = result.expandedTo(
            QSize(qRound(metrics.controlHeightRegular), qRound(metrics.controlHeightRegular)));
        break;
    case CT_LineEdit:
        result.rheight() = std::max(result.height(), qRound(metrics.controlHeightRegular));
        result.rwidth() =
            std::max(result.width(), contentsSize.width() + qRound(metrics.spacing16));
        break;
    case CT_ComboBox:
        if (const auto* comboBox = qobject_cast<const VCombobox*>(widget)) {
            const QFontMetrics fontMetrics(comboBox->font());
            if (const auto* comboOption = qstyleoption_cast<const QStyleOptionComboBox*>(option)) {
                QStyleOptionComboBox resolvedOption(*comboOption);
                resolvedOption.fontMetrics = fontMetrics;
                result = QProxyStyle::sizeFromContents(type, &resolvedOption, contentsSize, widget);
            }
            const ComboBoxMenuMetrics itemMetrics =
                comboBoxMenuMetrics(fontMetrics, *comboBox, metrics);
            result.rheight() = std::max(result.height(), qRound(metrics.controlHeightRegular));
            // QComboBox supplies the widest full item text. Reserve both the popup's state
            // column and the collapsed control's double-chevron column; the larger budget wins.
            const int horizontalPadding = std::max(comboBoxMenuHorizontalPadding(itemMetrics),
                                                   comboBoxCollapsedHorizontalPadding(metrics));
            int collapsedContentWidth = fontMetrics.horizontalAdvance(comboBox->currentText());
            if (const auto* comboOption = qstyleoption_cast<const QStyleOptionComboBox*>(option);
                comboOption && !comboOption->currentIcon.isNull()) {
                const int iconWidth = comboOption->iconSize.isValid()
                                          ? comboOption->iconSize.width()
                                          : qRound(metrics.controlHeightSmall * 0.67);
                collapsedContentWidth += iconWidth + qRound(metrics.spacing6);
            }
            // Qt's generic hint uses glyph bounds on some platforms, while elision uses text
            // advance. Include the current label's advance so native font bearings cannot clip it.
            const int contentWidth = std::max(contentsSize.width(), collapsedContentWidth);
            result.rwidth() = std::max(result.width(), contentWidth + horizontalPadding + 1);
        }
        break;
    case CT_SpinBox:
        result.rheight() = std::max(result.height(), qRound(metrics.controlHeightRegular));
        result.rwidth() = std::max(
            result.width(),
            contentsSize.width() + qRound(metrics.controlHeightRegular * 0.78 + metrics.spacing20));
        break;
    case CT_CheckBox:
    case CT_RadioButton:
        result.rheight() =
            std::max(result.height(), (widget ? vkui::controlExtent(*widget)
                                              : vkui::controlExtent(VControlSize::Regular)) +
                                          qRound(metrics.spacing8));
        break;
    case CT_ProgressBar:
        result.rheight() = std::max(result.height(), qRound(metrics.controlHeightSmall));
        break;
    case CT_Slider:
        if (const auto* slider = qstyleoption_cast<const QStyleOptionSlider*>(option);
            slider && usesDiscreteSliderStyle(*slider)) {
            const int crossExtent = qRound(metrics.controlHeightSmall + metrics.spacing8);
            if (slider->orientation == Qt::Horizontal) {
                result.rheight() = std::max(result.height(), crossExtent);
            } else {
                result.rwidth() = std::max(result.width(), crossExtent);
            }
        }
        break;
    case CT_TabBarTab:
        result.rheight() = std::max(result.height(), qRound(metrics.controlHeightRegular));
        result.rwidth() =
            std::max(result.width(), contentsSize.width() + qRound(metrics.spacing16));
        break;
    case CT_MenuItem: {
        const auto* menuItem = qstyleoption_cast<const QStyleOptionMenuItem*>(option);
        const auto* comboBox = VkPopupSurfaceStyler::owningVCombobox(widget);
        if (menuItem) {
            QStyleOptionMenuItem resolvedOption(*menuItem);
            resolvedOption.font = comboBox
                                      ? comboBox->font()
                                      : fontWithSizeOf(menuItem->font, theme.typography().body);
            resolvedOption.fontMetrics = QFontMetrics(resolvedOption.font);
            result = QProxyStyle::sizeFromContents(type, &resolvedOption, contentsSize, widget);
            const QFontMetrics& fontMetrics = resolvedOption.fontMetrics;
            if (!comboBox) {
                result.rheight() = std::max(result.height(), qRound(metrics.controlHeightRegular));
                result.rwidth() += qRound(metrics.spacing8);
                break;
            }
            const ComboBoxMenuMetrics itemMetrics =
                comboBoxMenuMetrics(fontMetrics, *comboBox, metrics);
            const int textWidth = fontMetrics.horizontalAdvance(comboBoxMenuText(menuItem->text));
            const int preferredWidth = itemMetrics.leadingMargin + itemMetrics.leadingColumnWidth +
                                       itemMetrics.columnGap + textWidth + itemMetrics.rightMargin;
            result.rwidth() = std::max(result.width(), preferredWidth);
            result.rheight() = std::max(result.height(), itemMetrics.rowHeight);
        } else {
            result.rheight() = std::max(result.height(), qRound(metrics.controlHeightRegular));
            result.rwidth() += qRound(metrics.spacing8);
        }
        break;
    }
    default:
        break;
    }
    return result;
}

QRect VStyle::subElementRect(SubElement element, const QStyleOption* option,
                             const QWidget* widget) const {
    if (!option) {
        return {};
    }
    const auto& metrics = VkThemeManager::instance()->theme().metrics();
    switch (element) {
    case SE_PushButtonContents:
        return option->rect.adjusted(qRound(metrics.spacing12), qRound(metrics.spacing4),
                                     -qRound(metrics.spacing12), -qRound(metrics.spacing4));
    case SE_LineEditContents:
        return option->rect.adjusted(qRound(metrics.spacing8), qRound(metrics.spacing4),
                                     -qRound(metrics.spacing8), -qRound(metrics.spacing4));
    case SE_CheckBoxIndicator:
    case SE_RadioButtonIndicator: {
        const int extent = pixelMetric(element == SE_CheckBoxIndicator ? PM_IndicatorWidth
                                                                       : PM_ExclusiveIndicatorWidth,
                                       option, widget);
        const QRect logical(option->rect.left(), option->rect.center().y() - extent / 2, extent,
                            extent);
        return visualRect(option->direction, option->rect, logical);
    }
    case SE_CheckBoxContents:
    case SE_RadioButtonContents: {
        const bool checkBox = element == SE_CheckBoxContents;
        const int extent =
            pixelMetric(checkBox ? PM_IndicatorWidth : PM_ExclusiveIndicatorWidth, option, widget);
        const int gap = qRound(metrics.spacing6);
        const QRect logical(option->rect.left() + extent + gap, option->rect.top(),
                            std::max(0, option->rect.width() - extent - gap),
                            option->rect.height());
        return visualRect(option->direction, option->rect, logical);
    }
    case SE_CheckBoxFocusRect:
    case SE_RadioButtonFocusRect:
        return option->rect;
    case SE_FrameContents:
        return option->rect.adjusted(qRound(metrics.borderWidth), qRound(metrics.borderWidth),
                                     -qRound(metrics.borderWidth), -qRound(metrics.borderWidth));
    default:
        return QProxyStyle::subElementRect(element, option, widget);
    }
}

QRect VStyle::subControlRect(ComplexControl control, const QStyleOptionComplex* option,
                             SubControl subControl, const QWidget* widget) const {
    if (!option) {
        return {};
    }
    const auto& metrics = VkThemeManager::instance()->theme().metrics();
    switch (control) {
    case CC_ComboBox: {
        if (!qobject_cast<const VCombobox*>(widget)) {
            break;
        }
        if (subControl == SC_ComboBoxListBoxPopup) {
            QRect popup = QProxyStyle::subControlRect(control, option, subControl, widget);
            const int horizontalInset = pixelMetric(PM_MenuHMargin, option, widget);
            const int verticalInset = pixelMetric(PM_MenuVMargin, option, widget);
            // QComboBox aligns the popup's viewport coordinates with the current control. Account
            // for the transparent margins in the outer window geometry so the selected row,
            // rather than the popup edge, aligns with the collapsed widget.
            popup.adjust(-horizontalInset, 0, horizontalInset, 0);
            popup.translate(0, -verticalInset);
            return popup;
        }
        const int arrowWidth = qRound(metrics.controlHeightRegular);
        const QRect logicalArrow(option->rect.right() - arrowWidth + 1, option->rect.top(),
                                 arrowWidth, option->rect.height());
        const QRect arrow = visualRect(option->direction, option->rect, logicalArrow);
        if (subControl == SC_ComboBoxArrow) {
            return arrow;
        }
        if (subControl == SC_ComboBoxEditField) {
            const QRect logical(
                option->rect.left() + qRound(metrics.spacing8),
                option->rect.top() + qRound(metrics.borderWidth),
                std::max(0, option->rect.width() - arrowWidth - qRound(metrics.spacing12)),
                option->rect.height() - qRound(metrics.borderWidth * 2.0));
            return visualRect(option->direction, option->rect, logical);
        }
        break;
    }
    case CC_SpinBox: {
        const int buttonWidth = qRound(metrics.controlHeightRegular * 0.78);
        const QRect logicalButtons(option->rect.right() - buttonWidth + 1, option->rect.top(),
                                   buttonWidth, option->rect.height());
        const QRect buttons = visualRect(option->direction, option->rect, logicalButtons);
        if (subControl == SC_SpinBoxUp) {
            return QRect(buttons.left(), buttons.top(), buttons.width(), buttons.height() / 2);
        }
        if (subControl == SC_SpinBoxDown) {
            return QRect(buttons.left(), buttons.top() + buttons.height() / 2, buttons.width(),
                         buttons.height() - buttons.height() / 2);
        }
        if (subControl == SC_SpinBoxEditField) {
            const QRect logical(
                option->rect.left() + qRound(metrics.spacing8),
                option->rect.top() + qRound(metrics.borderWidth),
                std::max(0, option->rect.width() - buttonWidth - qRound(metrics.spacing12)),
                option->rect.height() - qRound(metrics.borderWidth * 2.0));
            return visualRect(option->direction, option->rect, logical);
        }
        break;
    }
    case CC_Slider: {
        const auto* slider = qstyleoption_cast<const QStyleOptionSlider*>(option);
        if (!slider) {
            break;
        }
        const bool discrete = usesDiscreteSliderStyle(*slider);
        const int handleExtent = pixelMetric(PM_SliderLength, slider, widget);
        const int handleAxisExtent =
            discrete ? std::max(10, qRound(metrics.spacing12)) : handleExtent;
        const int handleCrossExtent =
            discrete ? std::max(18, qRound(metrics.controlHeightSmall * 0.90)) : handleExtent;
        const int grooveExtent = discrete ? std::max(2, qRound(metrics.borderWidth * 2.0))
                                          : std::max(4, qRound(metrics.spacing4));
        const int grooveOffset = discrete ? discreteSliderGrooveOffset(*slider, metrics) : 0;
        if (subControl == SC_SliderGroove) {
            if (slider->orientation == Qt::Horizontal) {
                return QRect(option->rect.left(),
                             option->rect.center().y() - grooveExtent / 2 + grooveOffset,
                             option->rect.width(), grooveExtent);
            }
            return QRect(option->rect.center().x() - grooveExtent / 2 + grooveOffset,
                         option->rect.top(), grooveExtent, option->rect.height());
        }
        if (subControl == SC_SliderHandle) {
            const int span = (slider->orientation == Qt::Horizontal ? option->rect.width()
                                                                    : option->rect.height()) -
                             handleAxisExtent;
            const int position = QStyle::sliderPositionFromValue(
                slider->minimum, slider->maximum, slider->sliderPosition, std::max(0, span),
                slider->upsideDown);
            if (slider->orientation == Qt::Horizontal) {
                return QRect(option->rect.left() + position,
                             option->rect.center().y() - handleCrossExtent / 2 + grooveOffset,
                             handleAxisExtent, handleCrossExtent);
            }
            return QRect(option->rect.center().x() - handleCrossExtent / 2 + grooveOffset,
                         option->rect.top() + position, handleCrossExtent, handleAxisExtent);
        }
        break;
    }
    default:
        break;
    }
    return QProxyStyle::subControlRect(control, option, subControl, widget);
}

QStyle::SubControl VStyle::hitTestComplexControl(ComplexControl control,
                                                 const QStyleOptionComplex* option,
                                                 const QPoint& position,
                                                 const QWidget* widget) const {
    if (!option) {
        return SC_None;
    }
    if (control == CC_ComboBox && !qobject_cast<const VCombobox*>(widget)) {
        return QProxyStyle::hitTestComplexControl(control, option, position, widget);
    }
    if (control == CC_Slider) {
        // QSlider leaves subControls at SC_None for mouse-press hit testing. Resolve the
        // interactive geometry directly so it always matches VStyle's painted geometry.
        const QRect handle = subControlRect(control, option, SC_SliderHandle, widget);
        const int hitExtent = pixelMetric(PM_SliderThickness, option, widget);
        QRect hitTarget(0, 0, std::max(handle.width(), hitExtent),
                        std::max(handle.height(), hitExtent));
        hitTarget.moveCenter(handle.center());
        if (hitTarget.contains(position)) {
            return SC_SliderHandle;
        }
        if (subControlRect(control, option, SC_SliderGroove, widget).contains(position)) {
            return SC_SliderGroove;
        }
    }
    const auto controlsFor = [control]() {
        switch (control) {
        case CC_ComboBox:
            return std::array{SC_ComboBoxArrow, SC_ComboBoxEditField, SC_None, SC_None};
        case CC_SpinBox:
            return std::array{SC_SpinBoxUp, SC_SpinBoxDown, SC_SpinBoxEditField, SC_None};
        case CC_Slider:
            return std::array{SC_SliderHandle, SC_SliderGroove, SC_None, SC_None};
        default:
            return std::array{SC_None, SC_None, SC_None, SC_None};
        }
    }();
    for (SubControl subControl : controlsFor) {
        if (subControl != SC_None && option->subControls.testFlag(subControl) &&
            subControlRect(control, option, subControl, widget).contains(position)) {
            return subControl;
        }
    }
    return QProxyStyle::hitTestComplexControl(control, option, position, widget);
}

int VStyle::styleHint(StyleHint hint, const QStyleOption* option, const QWidget* widget,
                      QStyleHintReturn* returnData) const {
    switch (hint) {
    case SH_Widget_Animate:
        return VkThemeManager::instance()->animationsEnabled() ? 1 : 0;
    case SH_EtchDisabledText:
    case SH_DitherDisabledText:
        return 0;
    case SH_ItemView_ShowDecorationSelected:
        return 1;
    case SH_ComboBox_Popup:
        // Qt's menu-popup path aligns the selected row with the collapsed combo box and keeps
        // placement, scrolling, keyboard behavior, and custom delegates inside QComboBox.
        return qobject_cast<const VCombobox*>(widget)
                   ? 1
                   : QProxyStyle::styleHint(hint, option, widget, returnData);
    case SH_ComboBox_PopupFrameStyle:
        // PE_PanelMenu paints the single macOS-style hairline. The private QFrame must not add a
        // second rectangular or rounded frame around the same popup surface.
        return qobject_cast<const VCombobox*>(widget)
                   ? static_cast<int>(QFrame::NoFrame)
                   : QProxyStyle::styleHint(hint, option, widget, returnData);
#if defined(Q_OS_MACOS) || defined(Q_OS_MAC)
    case SH_DialogButtonLayout:
        // VStyle delegates to Fusion for unsupported controls, but dialog action order is a
        // platform convention rather than a visual theme detail. Preserve the native macOS
        // ordering for every QDialogButtonBox and QMessageBox using semantic button roles.
        return QDialogButtonBox::MacLayout;
#endif
    default:
        return QProxyStyle::styleHint(hint, option, widget, returnData);
    }
}

QIcon VStyle::standardIcon(StandardPixmap standardIcon, const QStyleOption* option,
                           const QWidget* widget) const {
    const Qt::LayoutDirection direction =
        option ? option->direction : (widget ? widget->layoutDirection() : Qt::LeftToRight);
    switch (standardIcon) {
    case SP_ArrowLeft:
        return icon(VkSymbol::ChevronLeft);
    case SP_ArrowRight:
        return icon(VkSymbol::ChevronRight);
    case SP_ArrowUp:
        return icon(VkSymbol::ChevronUp);
    case SP_ArrowDown:
        return icon(VkSymbol::ChevronDown);
    case SP_ArrowBack:
        return icon(direction == Qt::RightToLeft ? VkSymbol::ChevronRight : VkSymbol::ChevronLeft);
    case SP_ArrowForward:
        return icon(direction == Qt::RightToLeft ? VkSymbol::ChevronLeft : VkSymbol::ChevronRight);
    case SP_TitleBarCloseButton:
    case SP_DialogCloseButton:
    case SP_DialogCancelButton:
        return icon(VkSymbol::Close);
    case SP_DialogApplyButton:
    case SP_DialogYesButton:
        return icon(VkSymbol::Checkmark, VkIconRole::Accent);
    case SP_FileIcon:
        return icon(VkSymbol::Document);
    case SP_DirIcon:
    case SP_DirOpenIcon:
        return icon(VkSymbol::Folder);
    case SP_MessageBoxInformation:
        return icon(VkSymbol::Information, VkIconRole::Accent);
    case SP_MessageBoxWarning:
        return icon(VkSymbol::Warning);
    default:
        return QProxyStyle::standardIcon(standardIcon, option, widget);
    }
}

QPalette VStyle::standardPalette() const {
    const VkTheme& theme = VkThemeManager::instance()->theme();
    const auto& colors = theme.colors();
    QPalette palette;
    palette.setColor(QPalette::Window, colors.windowBackground);
    palette.setColor(QPalette::WindowText, colors.textPrimary);
    palette.setColor(QPalette::Base, colors.contentBackground);
    palette.setColor(QPalette::AlternateBase, colors.elevatedBackground);
    palette.setColor(QPalette::ToolTipBase, colors.elevatedBackground);
    palette.setColor(QPalette::ToolTipText, colors.textPrimary);
    palette.setColor(QPalette::Text, colors.textPrimary);
    palette.setColor(QPalette::Button, colors.controlFill);
    palette.setColor(QPalette::ButtonText, colors.textPrimary);
    palette.setColor(QPalette::BrightText, colors.destructive);
    palette.setColor(QPalette::Light, colors.elevatedBackground);
    palette.setColor(QPalette::Midlight, colors.separator);
    palette.setColor(QPalette::Dark, colors.borderStrong);
    palette.setColor(QPalette::Mid, colors.border);
    palette.setColor(QPalette::Shadow, colors.shadow);
    palette.setColor(QPalette::Highlight, textSelectionColor(theme, true));
    palette.setColor(QPalette::HighlightedText, colors.textPrimary);
    palette.setColor(QPalette::Link, colors.accent);
    palette.setColor(QPalette::LinkVisited, colors.accentPressed);
    palette.setColor(QPalette::PlaceholderText, colors.textTertiary);
    palette.setColor(QPalette::Accent, colors.accent);

    palette.setColor(QPalette::Inactive, QPalette::Highlight, textSelectionColor(theme, false));
    palette.setColor(QPalette::Inactive, QPalette::HighlightedText, colors.textPrimary);
    palette.setColor(QPalette::Disabled, QPalette::WindowText, colors.textDisabled);
    palette.setColor(QPalette::Disabled, QPalette::Text, colors.textDisabled);
    palette.setColor(QPalette::Disabled, QPalette::Button, colors.controlFillDisabled);
    palette.setColor(QPalette::Disabled, QPalette::ButtonText, colors.textDisabled);
    palette.setColor(QPalette::Disabled, QPalette::BrightText, colors.textDisabled);
    palette.setColor(QPalette::Disabled, QPalette::Highlight, colors.controlFillDisabled);
    palette.setColor(QPalette::Disabled, QPalette::HighlightedText, colors.textDisabled);
    palette.setColor(QPalette::Disabled, QPalette::Link, colors.textDisabled);
    palette.setColor(QPalette::Disabled, QPalette::LinkVisited, colors.textDisabled);
    palette.setColor(QPalette::Disabled, QPalette::PlaceholderText, colors.textDisabled);
    palette.setColor(QPalette::Disabled, QPalette::Accent, colors.textDisabled);
    return palette;
}

void VStyle::polish(QPalette& palette) {
    QProxyStyle::polish(palette);
    palette = standardPalette();
}

void VStyle::polish(QApplication* application) {
    QProxyStyle::polish(application);
}

void VStyle::polish(QWidget* widget) {
    QProxyStyle::polish(widget);
    d->typography->polish(widget);
    if (usesVkUiFocusRing(widget)) {
        // QStyleOption::State_MouseOver is defined for hover-aware widgets. Keep event delivery in
        // the style, as recommended by Qt, so every control receives the same behavior.
        widget->setAttribute(Qt::WA_Hover, true);
    }
    if (VkPopupSurfaceStyler::isVComboboxPopup(widget)) {
        // Breeze uses the same narrow private-container seam. Translucency is a window-system
        // prerequisite for antialiased corners; item painting remains owned by Qt's view/delegate.
        if (QLayout* layout = widget->layout()) {
            const int margin = qRound(VkThemeManager::instance()->theme().metrics().spacing6);
            layout->setContentsMargins(margin, 0, margin, 0);
        }
    }
    d->popupSurfaces->polish(widget);
}

void VStyle::unpolish(QApplication* application) {
    d->typography->restoreAll();
    QProxyStyle::unpolish(application);
}

void VStyle::unpolish(QWidget* widget) {
    d->popupSurfaces->unpolish(widget);
    if (VkPopupSurfaceStyler::isVComboboxPopup(widget)) {
        if (QLayout* layout = widget->layout()) {
            layout->setContentsMargins(0, 0, 0, 0);
        }
    }
    if (usesVkUiFocusRing(widget)) {
        widget->setAttribute(Qt::WA_Hover, false);
    }
    QProxyStyle::unpolish(widget);
}

void installVkUi(QApplication& application) {
    detail::ensureResourcesInitialized();

    // Creating the manager resolves Auto appearance and installs system-change listeners.
    auto* const themeManager = VkThemeManager::instance();
    Q_UNUSED(themeManager)
    if (!qobject_cast<VStyle*>(application.style())) {
        application.setStyle(new VStyle);
    }

    constexpr auto coordinatorName = "_vkui_theme_refresh_coordinator";
    if (!application.findChild<QObject*>(QString::fromLatin1(coordinatorName),
                                         Qt::FindDirectChildrenOnly)) {
        auto* coordinator = new VkThemeRefreshCoordinator(application);
        coordinator->setObjectName(QString::fromLatin1(coordinatorName));
    }
}

} // namespace vkui
