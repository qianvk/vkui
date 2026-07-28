// SPDX-License-Identifier: MIT

#pragma once

#include <QtCore/QMetaType>
#include <QtCore/QRectF>
#include <QtCore/QSize>
#include <QtCore/QString>
#include <QtCore/QtTypes>
#include <QtGui/QColor>
#include <QtGui/QFont>
#include <QtGui/QIcon>
#include <QtGui/QPalette>
#include <vkui/VkUiGlobal.h>
#include <vkui/core/VkIcon.h>

QT_BEGIN_NAMESPACE
class QPainter;
QT_END_NAMESPACE

namespace vkui {

/**
 * Stable names for the bundled file-system glyphs.
 *
 * The backing font and its private code points are implementation details.
 */
enum class VkFileGlyph {
    FolderClosed,
    FolderOpen,
    File,
    TextFile,
    CodeFile,
    ImageFile,
    PdfFile,
    ArchiveFile,
    BookFile,
};

/**
 * Recommended logical and physical layout values for a file-system row.
 *
 * Recompute these values after a widget receives QEvent::FontChange,
 * QEvent::ApplicationFontChange, or a device-pixel-ratio change.
 */
struct VKUI_CORE_EXPORT VkFileIconMetrics final {
    int glyphPixelSize = 0;
    QSize glyphSlotSize;
    QSize devicePixelGlyphSlotSize;
    int textGap = 0;
    int rowHeight = 0;
    qreal devicePixelRatio = 1.0;
};

/**
 * Registers the bundled file icon font on first use.
 *
 * File icon font and rendering APIs require an existing QGuiApplication.
 * The first successful call must happen after QGuiApplication construction.
 * Calls made earlier, or from a non-GUI thread before registration, return
 * false without consuming the one-time registration attempt.
 */
[[nodiscard]] VKUI_CORE_EXPORT bool initializeFileIconFont();

/** Returns the bundled family name, or an empty string if registration failed. */
[[nodiscard]] VKUI_CORE_EXPORT QString fileIconFontFamily();

/**
 * Returns the bundled file icon font at a logical pixel size.
 *
 * A platform fixed-width font is returned if the bundled font is unavailable.
 */
[[nodiscard]] VKUI_CORE_EXPORT QFont fileIconFont(int pixelSize);

/**
 * Calculates stable file-tree layout metrics for the current interface font.
 *
 * All values except devicePixelGlyphSlotSize are logical pixels. The supplied
 * device pixel ratio is normalized to a safe positive value.
 */
[[nodiscard]] VKUI_CORE_EXPORT VkFileIconMetrics fileIconMetrics(const QFont& interfaceFont,
                                                                 qreal devicePixelRatio = 1.0);

/** Creates an icon whose color follows the current VkUI theme. */
[[nodiscard]] VKUI_CORE_EXPORT QIcon fileIcon(VkFileGlyph glyph,
                                              VkIconRole role = VkIconRole::Secondary);

/**
 * Creates an icon that resolves an application palette role when it renders.
 *
 * Keeping only the role, rather than a color snapshot, lets an existing icon
 * follow QApplication palette changes.
 */
[[nodiscard]] VKUI_CORE_EXPORT QIcon fileIcon(VkFileGlyph glyph, QPalette::ColorRole role,
                                              QPalette::ColorGroup group = QPalette::Active);

/** Creates an icon with an explicit color. Disabled mode lowers its opacity. */
[[nodiscard]] VKUI_CORE_EXPORT QIcon fileIcon(VkFileGlyph glyph, const QColor& color);

/** Draws a glyph using a color resolved from the supplied palette. */
VKUI_CORE_EXPORT void drawFileGlyph(QPainter& painter, const QRectF& bounds, VkFileGlyph glyph,
                                    const QPalette& palette,
                                    QPalette::ColorRole role = QPalette::Text,
                                    QPalette::ColorGroup group = QPalette::Active);

/** Draws a glyph using an explicit color. */
VKUI_CORE_EXPORT void drawFileGlyph(QPainter& painter, const QRectF& bounds, VkFileGlyph glyph,
                                    const QColor& color);

} // namespace vkui

Q_DECLARE_METATYPE(vkui::VkFileGlyph)
