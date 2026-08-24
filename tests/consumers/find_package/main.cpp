// SPDX-License-Identifier: MIT

#include <QApplication>
#include <QCheckBox>
#include <QLabel>
#include <QWidget>
#include <vkui/Core.h>
#include <vkui/Widgets.h>
#if defined(VKUI_CONSUMER_HAS_WINDOW)
#include <vkui/Window.h>
#endif

int main(int argc, char* argv[]) {
    QApplication application(argc, argv);
    vkui::installVkUi(application);

    auto* themeManager = vkui::VkThemeManager::instance();
    QLabel sectionTitle(QStringLiteral("Installed typography API"));
    vkui::setTextStyle(sectionTitle, vkui::VTextStyle::Title);
    themeManager->setTextSizeLevel(6);
    const bool typographyApiValid =
        themeManager->theme().textSizeLevel() == 6 &&
        themeManager->theme().textScale() == vkui::textScaleForTextSizeLevel(6) &&
        vkui::textStyle(sectionTitle) == vkui::VTextStyle::Title &&
        sectionTitle.font() == vkui::textStyleFont(vkui::VTextStyle::Title);
    themeManager->resetTextSizeLevel();

    const quint64 previousColorGeneration = themeManager->theme().colorGeneration();
    vkui::VkThemeChanges observedChanges;
    QObject::connect(
        themeManager, &vkui::VkThemeManager::themeChanged, &application,
        [&observedChanges](quint64, vkui::VkThemeChanges changes) { observedChanges |= changes; });
    const vkui::VkAccentColor nextAccent =
        themeManager->accentColor() == vkui::VkAccentColor::Purple ? vkui::VkAccentColor::Blue
                                                                   : vkui::VkAccentColor::Purple;
    themeManager->setAccentColor(nextAccent);

#if defined(VKUI_CONSUMER_HAS_WINDOW)
    QWidget host;
    QWidget firstTitleBar(&host);
    QWidget secondTitleBar(&host);
    vkui::VWindowAgent windowAgent(host);
    windowAgent.setSystemButtons(vkui::VSystemButton::Close);
    if (!windowAgent.addTitleBar(&firstTitleBar) || !windowAgent.addTitleBar(&secondTitleBar)) {
        return 1;
    }
    vkui::VMessageDialog prompt(vkui::VMessageDialog::Icon::Information,
                                QStringLiteral("Installed API"),
                                QStringLiteral("The installed Window component is available."),
                                QDialogButtonBox::NoButton);
    auto* dismiss = prompt.addButton(QDialogButtonBox::Cancel);
    prompt.setDefaultButton(dismiss);
    prompt.setEscapeButton(dismiss);
    const bool windowApiValid = windowAgent.titleBars().size() == 2 &&
                                windowAgent.systemButtons() == vkui::VSystemButton::Close &&
                                prompt.buttons() == QList<QAbstractButton*>({dismiss}) &&
                                prompt.defaultButton() == dismiss && prompt.escapeButton() == dismiss;
#else
    const bool windowApiValid = true;
#endif

    vkui::VSwitch control;
    control.setChecked(true);
    QCheckBox checkBox;
    vkui::setControlSize(checkBox, vkui::VControlSize::Large);
    vkui::setControlExtent(checkBox, 27);
    return windowApiValid && typographyApiValid && control.isChecked() &&
                   vkui::controlSize(checkBox) == vkui::VControlSize::Large &&
                   vkui::controlExtent(checkBox) == 27 &&
                   themeManager->theme().colorGeneration() > previousColorGeneration &&
                   observedChanges == vkui::VkThemeChanges(vkui::VkThemeChange::Colors)
               ? 0
               : 1;
}
