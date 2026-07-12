// SPDX-License-Identifier: MIT

#pragma once

#include <QIcon>
#include <QWidget>
#include <memory>
#include <vkui/VkUiGlobal.h>

class QFocusEvent;
class QKeyEvent;
class QPaintEvent;
class QResizeEvent;

namespace vkui {

class VkSegmentedControlPrivate;

/** A connected, single-selection group of private checkable buttons. */
class VKUI_WIDGETS_EXPORT VkSegmentedControl final : public QWidget {
    Q_OBJECT
    Q_PROPERTY(int currentIndex READ currentIndex WRITE setCurrentIndex NOTIFY currentIndexChanged)

  public:
    explicit VkSegmentedControl(QWidget* parent = nullptr);
    ~VkSegmentedControl() override;

    int count() const noexcept;
    int currentIndex() const noexcept;

    int addSegment(const QString& text);
    int addSegment(const QIcon& icon, const QString& text = {});
    int insertSegment(int index, const QIcon& icon, const QString& text = {});

    void removeSegment(int index);
    void clear();

    QString segmentText(int index) const;
    void setSegmentText(int index, const QString& text);

    QIcon segmentIcon(int index) const;
    void setSegmentIcon(int index, const QIcon& icon);

    bool isSegmentEnabled(int index) const;
    void setSegmentEnabled(int index, bool enabled);

    /**
     * Returns the accessible name reported by a segment's private button.
     * Icon-only segments should be assigned a meaningful name by the caller.
     */
    QString segmentAccessibleName(int index) const;
    void setSegmentAccessibleName(int index, const QString& name);

    QSize sizeHint() const override;
    QSize minimumSizeHint() const override;

  public slots:
    void setCurrentIndex(int index);

  signals:
    void currentIndexChanged(int index);
    void segmentActivated(int index);

  protected:
    void paintEvent(QPaintEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    void changeEvent(QEvent* event) override;
    void focusInEvent(QFocusEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;

  private:
    std::unique_ptr<VkSegmentedControlPrivate> d;
};

} // namespace vkui
