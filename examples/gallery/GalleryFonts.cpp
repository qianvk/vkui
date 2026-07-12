// SPDX-License-Identifier: MIT

#include "GalleryFonts.h"

#include <QFontDatabase>
#include <QIconEngine>
#include <QPainter>
#include <QPixmap>
#include <QStringList>
#include <algorithm>
#include <optional>
#include <utility>
#include <vkui/core/VkTheme.h>
#include <vkui/core/VkThemeManager.h>

namespace {

QString& fontFamily() {
    static QString family;
    return family;
}

QColor iconColor(vkui::VkIconRole role, QIcon::Mode mode) {
    const auto& colors = vkui::VkThemeManager::instance()->theme().colors();
    if (mode == QIcon::Disabled || role == vkui::VkIconRole::Disabled) {
        return colors.symbolDisabled;
    }
    switch (role) {
    case vkui::VkIconRole::Accent:
        return mode == QIcon::Active ? colors.accentHovered : colors.accent;
    case vkui::VkIconRole::Destructive:
        return colors.destructive;
    case vkui::VkIconRole::Secondary:
        return colors.symbolSecondary;
    case vkui::VkIconRole::Primary:
    default:
        return colors.symbolPrimary;
    }
}

class NerdGlyphIconEngine final : public QIconEngine {
  public:
    NerdGlyphIconEngine(char32_t codePoint, vkui::VkIconRole role)
        : codePoint_(codePoint), text_(QString::fromUcs4(&codePoint, 1)), role_(role) {}

    NerdGlyphIconEngine(char32_t codePoint, QColor color)
        : codePoint_(codePoint), text_(QString::fromUcs4(&codePoint, 1)), color_(std::move(color)) {
    }

    QIconEngine* clone() const override {
        if (color_) {
            return new NerdGlyphIconEngine(codePoint_, *color_);
        }
        return new NerdGlyphIconEngine(codePoint_, role_);
    }

    QString key() const override {
        if (color_) {
            return QStringLiteral("vkui-gallery-nerd-color-%1").arg(color_->rgba());
        }
        return QStringLiteral("vkui-gallery-nerd-%1")
            .arg(vkui::VkThemeManager::instance()->theme().generation());
    }

    void paint(QPainter* painter, const QRect& rect, QIcon::Mode mode,
               QIcon::State state) override {
        Q_UNUSED(state)
        if (!painter || rect.isEmpty()) {
            return;
        }
        QFont font = gallery::codeFont();
        font.setPixelSize(std::max(1, qRound(std::min(rect.width(), rect.height()) * 0.82)));
        painter->save();
        painter->setRenderHint(QPainter::TextAntialiasing, true);
        painter->setFont(font);
        QColor color = color_ ? *color_ : iconColor(role_, mode);
        if (color_ && mode == QIcon::Disabled) {
            color.setAlphaF(color.alphaF() * 0.38F);
        }
        painter->setPen(color);
        painter->drawText(rect, Qt::AlignCenter, text_);
        painter->restore();
    }

    QPixmap pixmap(const QSize& size, QIcon::Mode mode, QIcon::State state) override {
        if (size.isEmpty()) {
            return {};
        }
        QPixmap result(size);
        result.fill(Qt::transparent);
        QPainter painter(&result);
        paint(&painter, result.rect(), mode, state);
        return result;
    }

  private:
    char32_t codePoint_;
    QString text_;
    vkui::VkIconRole role_ = vkui::VkIconRole::Secondary;
    std::optional<QColor> color_;
};

} // namespace

namespace gallery {

bool initializeFonts() {
    static const bool loaded = [] {
        const int id = QFontDatabase::addApplicationFont(
            QStringLiteral(":/vkui/gallery/fonts/FiraCodeNerdFont-Regular.ttf"));
        const QStringList families = QFontDatabase::applicationFontFamilies(id);
        if (families.isEmpty()) {
            return false;
        }
        fontFamily() = families.constFirst();
        return true;
    }();
    return loaded;
}

QFont codeFont(qreal pointSize) {
    QFont font;
    if (initializeFonts()) {
        font.setFamily(fontFamily());
    } else {
        font = QFontDatabase::systemFont(QFontDatabase::FixedFont);
    }
    font.setStyleHint(QFont::Monospace);
    font.setFixedPitch(true);
    if (pointSize > 0.0) {
        font.setPointSizeF(pointSize);
    }
    return font;
}

QIcon nerdIcon(char32_t codePoint, vkui::VkIconRole role) {
    initializeFonts();
    return QIcon(new NerdGlyphIconEngine(codePoint, role));
}

QIcon nerdIcon(char32_t codePoint, const QColor& color) {
    initializeFonts();
    return QIcon(new NerdGlyphIconEngine(codePoint, color));
}

} // namespace gallery
