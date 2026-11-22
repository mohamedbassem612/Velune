#pragma once
#include <QString>
namespace velune::theme {
inline constexpr auto background = "#000000";
inline constexpr auto surface = "#0A0A0A";
inline constexpr auto elevated = "#141414";
inline constexpr auto active = "#242424";
inline constexpr auto text = "#FFFFFF";
inline constexpr auto secondary = "#A3A3A3";
inline constexpr auto disabled = "#666666";
inline constexpr auto border = "#303030";
inline constexpr auto inactiveProgress = "#484848";
inline QString style() {
    return QString(R"(
 QWidget { background: %1; color: %2; font-family: 'Inter', 'DejaVu Sans', sans-serif; font-size: 16px; }
 QPushButton { background: %3; border: 1px solid %4; border-radius: 10px; padding: 10px 18px; }
 QPushButton:hover { background: %5; border-color: #777777; }
 QPushButton:focus, QLineEdit:focus, QListWidget:focus { border: 1px solid white; }
 QPushButton:disabled { color: %6; }
 QLineEdit { background: %3; border: 1px solid %4; border-radius: 12px; padding: 14px; selection-background-color: #484848; }
 QScrollArea { border: none; background: transparent; }
 QScrollBar:vertical { background: #000000; width: 7px; margin: 0; }
 QScrollBar::handle:vertical { background: #303030; border-radius: 3px; min-height: 36px; }
 QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height: 0; }
 QScrollBar::add-page:vertical, QScrollBar::sub-page:vertical { background: transparent; }
 QSlider::groove:horizontal { height: 5px; background: #484848; border-radius: 2px; }
 QSlider::sub-page:horizontal { background: white; border-radius: 2px; }
 QSlider::handle:horizontal { background: white; width: 13px; height: 13px; margin: -4px 0; border-radius: 6px; }
 QSlider:focus { border: 1px solid #777777; border-radius: 4px; }
 QListWidget { background: #000000; border: none; outline: none; }
 QListWidget::item { padding: 18px; border-bottom: 1px solid #141414; }
 QListWidget::item:selected { background: #242424; }
 QToolTip { background: #141414; color: white; border: 1px solid #484848; padding: 6px; }
 )")
        .arg(background, text, surface, border, active, disabled);
}
} // namespace velune::theme
