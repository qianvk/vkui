// SPDX-License-Identifier: MIT

#include <QEnterEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QSplitterHandle>
#include <vkui/core/VkThemeManager.h>
#include <vkui/widgets/controls/VSplitter.h>

namespace vkui {

namespace {

constexpr int SeparatorExtent = 1;

class VSplitterHandle final : public QSplitterHandle {
  public:
    VSplitterHandle(const Qt::Orientation orientation, QSplitter* parent)
        : QSplitterHandle(orientation, parent) {
        setAttribute(Qt::WA_Hover, true);
        setAutoFillBackground(false);

        connect(VkThemeManager::instance(), &VkThemeManager::themeChanged, this,
                [this](quint64, const VkThemeChanges changes) {
                    if (changes.testFlag(VkThemeChange::Colors)) {
                        update();
                    }
                });
    }

  protected:
    void paintEvent(QPaintEvent*) override {
        if (!isEnabled() || (!hovered_ && !pressed_)) {
            return;
        }

        QRect separator = rect();
        if (orientation() == Qt::Horizontal) {
            separator.setLeft((width() - SeparatorExtent) / 2);
            separator.setWidth(SeparatorExtent);
        } else {
            separator.setTop((height() - SeparatorExtent) / 2);
            separator.setHeight(SeparatorExtent);
        }

        const auto& colors = VkThemeManager::instance()->theme().colors();
        QPainter painter(this);
        painter.fillRect(separator, pressed_ ? colors.accentPressed : colors.accent);
    }

    void enterEvent(QEnterEvent* event) override {
        hovered_ = true;
        update();
        QSplitterHandle::enterEvent(event);
    }

    void leaveEvent(QEvent* event) override {
        hovered_ = false;
        update();
        QSplitterHandle::leaveEvent(event);
    }

    void mousePressEvent(QMouseEvent* event) override {
        if (event->button() == Qt::LeftButton) {
            pressed_ = true;
            update();
        }
        QSplitterHandle::mousePressEvent(event);
    }

    void mouseReleaseEvent(QMouseEvent* event) override {
        QSplitterHandle::mouseReleaseEvent(event);
        if (event->button() == Qt::LeftButton) {
            pressed_ = false;
            update();
        }
    }

  private:
    bool hovered_{false};
    bool pressed_{false};
};

} // namespace

VSplitter::VSplitter(QWidget* parent) : VSplitter(Qt::Horizontal, parent) {}

VSplitter::VSplitter(const Qt::Orientation orientation, QWidget* parent)
    : QSplitter(orientation, parent) {
    // Qt enlarges handles with a width of zero or one over the adjacent widgets.
    // Keeping the layout width at one preserves a crisp separator and a broad grab area.
    setHandleWidth(SeparatorExtent);
}

VSplitter::~VSplitter() = default;

QSplitterHandle* VSplitter::createHandle() {
    return new VSplitterHandle(orientation(), this);
}

} // namespace vkui
