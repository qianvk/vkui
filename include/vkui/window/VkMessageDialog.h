// SPDX-License-Identifier: MIT

#pragma once

#include <QDialogButtonBox>
#include <QPointer>
#include <vkui/window/VkFramelessDialog.h>

class QAbstractButton;
class QKeyEvent;
class QPushButton;

namespace vkui {

/** A safe, platform-ordered prompt using VkUI's shared frameless chrome. */
class VKUI_WINDOW_EXPORT VkMessageDialog final : public VkFramelessDialog {
    Q_OBJECT

  public:
    enum class Icon {
        Information,
        Warning,
        Critical,
    };
    Q_ENUM(Icon)

    VkMessageDialog(Icon icon, const QString& title, const QString& text,
                    QDialogButtonBox::StandardButtons buttons = QDialogButtonBox::Ok,
                    QWidget* parent = nullptr);

    QPushButton* addButton(const QString& text, QDialogButtonBox::ButtonRole role);
    [[nodiscard]] QPushButton* button(QDialogButtonBox::StandardButton button) const;
    void setDefaultButton(QAbstractButton* button);
    void setDefaultButton(QDialogButtonBox::StandardButton button);
    [[nodiscard]] bool defaultButtonIndicatorVisible() const noexcept;
    /**
     * Controls only the native default-button paint state. Enter keeps
     * activating the logical default even when this indicator is hidden.
     */
    void setDefaultButtonIndicatorVisible(bool visible);
    void setEscapeButton(QAbstractButton* button);
    void setEscapeButton(QDialogButtonBox::StandardButton button);
    [[nodiscard]] QAbstractButton* clickedButton() const;
    [[nodiscard]] QDialogButtonBox::StandardButton clickedStandardButton() const;

    static QDialogButtonBox::StandardButton
    information(QWidget* parent, const QString& title, const QString& text,
                QDialogButtonBox::StandardButtons buttons = QDialogButtonBox::Ok,
                QDialogButtonBox::StandardButton defaultButton = QDialogButtonBox::Ok);
    static QDialogButtonBox::StandardButton
    warning(QWidget* parent, const QString& title, const QString& text,
            QDialogButtonBox::StandardButtons buttons = QDialogButtonBox::Ok,
            QDialogButtonBox::StandardButton defaultButton = QDialogButtonBox::Ok);
    static QDialogButtonBox::StandardButton
    critical(QWidget* parent, const QString& title, const QString& text,
             QDialogButtonBox::StandardButtons buttons = QDialogButtonBox::Ok,
             QDialogButtonBox::StandardButton defaultButton = QDialogButtonBox::Ok);

    /**
     * Runs a destructive prompt with Cancel as both default and Escape.
     * Enter can therefore never confirm deletion accidentally, even when the
     * destructive button has keyboard focus.
     */
    static bool confirmDestructive(QWidget* parent, const QString& title, const QString& text,
                                   const QString& confirmText = {});

  public slots:
    void reject() override;

  protected:
    void keyPressEvent(QKeyEvent* event) override;

  private:
    static QDialogButtonBox::StandardButton run(Icon icon, QWidget* parent, const QString& title,
                                                const QString& text,
                                                QDialogButtonBox::StandardButtons buttons,
                                                QDialogButtonBox::StandardButton defaultButton);

    QDialogButtonBox* buttons_ = nullptr;
    QPointer<QAbstractButton> clickedButton_;
    QPointer<QPushButton> defaultButton_;
    QPointer<QAbstractButton> escapeButton_;
    bool defaultButtonIndicatorVisible_ = true;
};

} // namespace vkui
