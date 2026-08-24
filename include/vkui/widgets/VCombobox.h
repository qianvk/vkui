// SPDX-License-Identifier: MIT

#pragma once

#include <QtCore/Qt>
#include <QtWidgets/QComboBox>
#include <vkui/VkUiGlobal.h>

namespace vkui {

/** A QComboBox with VkUI's macOS-inspired collapsed control and menu presentation. */
class VKUI_WIDGETS_EXPORT VCombobox : public QComboBox {
    Q_OBJECT
    Q_PROPERTY(
        Qt::TextElideMode elideMode READ elideMode WRITE setElideMode NOTIFY elideModeChanged)

  public:
    explicit VCombobox(QWidget* parent = nullptr);

    /** Sets the truncation strategy used by both the collapsed label and popup rows. */
    void setElideMode(Qt::TextElideMode mode);
    [[nodiscard]] Qt::TextElideMode elideMode() const noexcept;

    void showPopup() override;

  signals:
    void elideModeChanged(Qt::TextElideMode mode);

  private:
    void synchronizePopupCurrentIndex();

    Qt::TextElideMode elideMode_ = Qt::ElideRight;
};

} // namespace vkui
