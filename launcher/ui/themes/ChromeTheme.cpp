// SPDX-License-Identifier: GPL-3.0-only
/*
 *  EMC - built on Prism Launcher
 *
 *  This program is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, version 3.
 */

#include "ChromeTheme.h"

#include <QObject>

namespace {
// Neutral greys, Apple-dark style: the base is near-black, each raised layer a step lighter.
const QColor bg0(0x07, 0x07, 0x08);   // wells: lists, fields
const QColor bg1(0x0E, 0x0E, 0x10);   // window
const QColor bg2(0x17, 0x17, 0x19);   // raised: buttons, alternate rows
const QColor bg3(0x24, 0x24, 0x27);   // tooltip, hover
const QColor sel(0x2C, 0x2C, 0x2F);   // selection: a lift, not a colour
const QColor label(0xF5, 0xF5, 0xF7);
const QColor label2(0xA1, 0xA1, 0xA6);
const QColor label3(0x6E, 0x6E, 0x73);
}  // namespace

QString ChromeTheme::id()
{
    return "emc";
}

QString ChromeTheme::name()
{
    return QObject::tr("EMC Chrome");
}

QPalette ChromeTheme::colorScheme()
{
    QPalette p;
    p.setColor(QPalette::Window, bg1);
    p.setColor(QPalette::WindowText, label);
    p.setColor(QPalette::Base, bg0);
    p.setColor(QPalette::AlternateBase, bg2);
    p.setColor(QPalette::ToolTipBase, bg3);
    p.setColor(QPalette::ToolTipText, label);
    p.setColor(QPalette::Text, label);
    p.setColor(QPalette::Button, bg2);
    p.setColor(QPalette::ButtonText, label);
    p.setColor(QPalette::BrightText, QColor(0xFF, 0x45, 0x3A));
    p.setColor(QPalette::Link, label);
    p.setColor(QPalette::LinkVisited, label2);
    p.setColor(QPalette::Highlight, sel);
    p.setColor(QPalette::HighlightedText, Qt::white);
    p.setColor(QPalette::PlaceholderText, label3);
    p.setColor(QPalette::Light, bg3);
    p.setColor(QPalette::Midlight, bg3);
    p.setColor(QPalette::Mid, bg2);
    p.setColor(QPalette::Dark, bg0);
    p.setColor(QPalette::Shadow, Qt::black);
    p.setColor(QPalette::Disabled, QPalette::WindowText, label3);
    p.setColor(QPalette::Disabled, QPalette::Text, label3);
    p.setColor(QPalette::Disabled, QPalette::ButtonText, label3);
    p.setColor(QPalette::Disabled, QPalette::Highlight, bg2);
    p.setColor(QPalette::Disabled, QPalette::HighlightedText, label2);
    return fadeInactive(p, fadeAmount(), fadeColor());
}

double ChromeTheme::fadeAmount()
{
    return 0.3;
}

QColor ChromeTheme::fadeColor()
{
    return bg1;
}

bool ChromeTheme::hasStyleSheet()
{
    return true;
}

QString ChromeTheme::appStyleSheet()
{
    // Rules of the sheet:
    //  - surfaces get lighter as they rise; borders are 1px white at low alpha, never grey boxes
    //  - controls are "brushed metal": a top-lit vertical gradient with a bright top edge
    //  - the one primary action per view is solid white with black text
    //  - 8px corners on controls, 10px on containers; nothing pill-shaped but the toggles
#ifdef Q_OS_WIN
    const QString font = QStringLiteral(R"(QWidget { font-family: "Segoe UI Variable Text", "Segoe UI", sans-serif; })");
#else
    const QString font;  // the system's own UI font: San Francisco on macOS, the desktop's choice on Linux
#endif
    return font + QStringLiteral(R"(

QToolTip { color: #F5F5F7; background-color: #242427; border: 1px solid rgba(255,255,255,0.10); padding: 5px 8px; border-radius: 6px; }

QMainWindow, QDialog, QWizard { background-color: #0E0E10; }
QMainWindow::separator { background: rgba(255,255,255,0.06); width: 1px; height: 1px; }

/* Toolbars: one quiet strip, no grips, no boxes around buttons until hovered. */
QToolBar { background: #0B0B0D; border: none; border-bottom: 1px solid rgba(255,255,255,0.06); spacing: 4px; padding: 6px 8px; }
QToolBar#instanceToolBar { border-bottom: none; border-left: 1px solid rgba(255,255,255,0.06); padding: 12px 10px; spacing: 4px; }
QToolBar#instanceToolBar QToolButton { padding: 6px 12px; }
QToolBar#instanceToolBar::separator { height: 1px; background: rgba(255,255,255,0.07); margin: 8px 6px; }
QToolBar#newsToolBar { border-bottom: none; border-top: 1px solid rgba(255,255,255,0.06); }
QToolBar::handle { image: none; width: 0px; height: 0px; }
QToolBar::separator { background: rgba(255,255,255,0.06); width: 1px; height: 1px; margin: 6px 6px; }

QToolButton { color: #F5F5F7; background: transparent; border: 1px solid transparent; border-radius: 8px; padding: 5px 10px; }
QToolButton:hover { background: rgba(255,255,255,0.06); }
QToolButton:pressed { background: rgba(255,255,255,0.10); }
QToolButton:checked { background: rgba(255,255,255,0.10); border-color: rgba(255,255,255,0.14); }
QToolButton:disabled { color: #5A5A5F; }
QToolButton::menu-indicator { image: none; width: 0px; }
QToolButton::menu-button { border: none; width: 14px; } /* EMC: room for the split arrow, so the label never runs under it */ QToolButton[popupMode="1"] { padding-right: 24px; }

/* Push buttons: brushed metal. */
QPushButton {
  color: #F5F5F7; border-radius: 8px; padding: 6px 16px; min-height: 20px;
  border: 1px solid rgba(255,255,255,0.09); border-top-color: rgba(255,255,255,0.18);
  background: qlineargradient(x1:0, y1:0, x2:0, y2:1, stop:0 #2A2A2E, stop:1 #1B1B1E);
}
QPushButton:hover { background: qlineargradient(x1:0, y1:0, x2:0, y2:1, stop:0 #323236, stop:1 #212124); }
QPushButton:pressed { background: #18181A; border-top-color: rgba(255,255,255,0.08); }
QPushButton:disabled { color: #5A5A5F; background: #141416; border-color: rgba(255,255,255,0.05); }
QPushButton:default {
  color: #0B0B0C; border: 1px solid #FFFFFF;
  background: qlineargradient(x1:0, y1:0, x2:0, y2:1, stop:0 #FFFFFF, stop:1 #D6D7DC);
}
QPushButton:default:hover { background: qlineargradient(x1:0, y1:0, x2:0, y2:1, stop:0 #FFFFFF, stop:1 #E6E7EB); }
QPushButton:default:pressed { background: #CFD0D5; }
QPushButton:default:disabled { color: #6E6E73; background: #1E1E21; border-color: rgba(255,255,255,0.06); }
QPushButton[accent="true"] {
  color: #0B0B0C; font-weight: 600; border: 1px solid #FFFFFF;
  background: qlineargradient(x1:0, y1:0, x2:0, y2:1, stop:0 #FFFFFF, stop:1 #D6D7DC);
}
QPushButton[accent="true"]:hover { background: qlineargradient(x1:0, y1:0, x2:0, y2:1, stop:0 #FFFFFF, stop:1 #E6E7EB); }
QPushButton[accent="true"]:pressed { background: #CFD0D5; }
QPushButton[accent="true"]:disabled { color: #6E6E73; background: #1E1E21; border-color: rgba(255,255,255,0.06); }
QLabel[role="title"] { font-size: 22px; font-weight: 600; color: #FFFFFF; }
QLabel[role="body"] { font-size: 13px; color: #A1A1A6; }
QLabel[role="note"] { font-size: 11px; color: #6E6E73; }
QLabel[role="cardTitle"] { font-size: 15px; font-weight: 600; color: #F5F5F7; }

/* Servers: one card per server, and the Instances | Servers switch. */
QFrame#serverCard { background: rgba(255,255,255,0.035); border: 1px solid rgba(255,255,255,0.07); border-radius: 14px; }
QFrame#serverCard:hover { background: rgba(255,255,255,0.055); border-color: rgba(255,255,255,0.12); }
QWidget#serverCards { background: transparent; }
QWidget#modeSwitch { background: #070708; border: 1px solid rgba(255,255,255,0.08); border-radius: 9px; }
QToolButton#updateButton { color: #0B0B0C; border: none; border-radius: 7px; padding: 4px 14px; margin-right: 6px; font-weight: 600; background: qlineargradient(x1:0, y1:0, x2:0, y2:1, stop:0 #FFFFFF, stop:1 #D6D7DC); }
QToolButton#updateButton:hover { background: #FFFFFF; }
QToolButton#updateButton:pressed { background: #CFD0D5; }
QToolButton#modeButton { color: #A1A1A6; border-radius: 7px; padding: 4px 14px; font-weight: 600; }
QToolButton#modeButton:hover { color: #F5F5F7; background: rgba(255,255,255,0.05); }
QToolButton#modeButton:checked { color: #0B0B0C; background: qlineargradient(x1:0, y1:0, x2:0, y2:1, stop:0 #FFFFFF, stop:1 #D6D7DC); border-color: transparent; }
QPushButton:focus, QToolButton:focus { outline: none; }
QPushButton:focus { border-color: rgba(255,255,255,0.55); }

/* Fields: sunken wells. */
QLineEdit, QPlainTextEdit, QTextEdit, QSpinBox, QDoubleSpinBox, QComboBox, QDateTimeEdit {
  color: #F5F5F7; background: #070708; border: 1px solid rgba(255,255,255,0.09); border-radius: 8px; padding: 5px 8px;
  selection-background-color: #F5F5F7; selection-color: #0B0B0C; }
QLineEdit:hover, QComboBox:hover, QSpinBox:hover { border-color: rgba(255,255,255,0.16); }
QLineEdit:focus, QPlainTextEdit:focus, QTextEdit:focus, QSpinBox:focus, QDoubleSpinBox:focus, QComboBox:focus { border-color: rgba(255,255,255,0.55); }
QLineEdit:disabled, QComboBox:disabled, QSpinBox:disabled { color: #5A5A5F; }
QComboBox::drop-down { border: none; width: 22px; }
QComboBox::down-arrow { width: 0; height: 0; border-left: 4px solid transparent; border-right: 4px solid transparent; border-top: 5px solid #A1A1A6; margin-right: 8px; }
QComboBox QAbstractItemView { background: #17171A; border: 1px solid rgba(255,255,255,0.10); border-radius: 8px; padding: 4px; selection-background-color: #2C2C2F; outline: none; }
QSpinBox::up-button, QSpinBox::down-button, QDoubleSpinBox::up-button, QDoubleSpinBox::down-button { border: none; width: 16px; }

/* Lists and tables: the content is the hero; no frames around it. */
QListView, QTreeView, QTableView, QListWidget, QTreeWidget, QTableWidget {
  background: #070708; border: 1px solid rgba(255,255,255,0.06); border-radius: 10px; outline: none;
  alternate-background-color: #0C0C0E; }
QListView::item, QTreeView::item { padding: 3px 4px; border-radius: 6px; }
QListView::item:hover, QTreeView::item:hover { background: rgba(255,255,255,0.04); }
QListView::item:selected, QTreeView::item:selected, QTableView::item:selected { background: #2C2C2F; color: #FFFFFF; }
QHeaderView { background: transparent; }
QHeaderView::section { background: #0E0E10; color: #A1A1A6; border: none; border-bottom: 1px solid rgba(255,255,255,0.06); padding: 6px 8px; font-weight: 600; }
QTableCornerButton::section { background: #0E0E10; border: none; }

QTabWidget::pane { border: 1px solid rgba(255,255,255,0.06); border-radius: 10px; top: -1px; }
QTabBar::tab { background: transparent; color: #A1A1A6; padding: 7px 14px; border: none; border-bottom: 2px solid transparent; }
QTabBar::tab:hover { color: #F5F5F7; }
QTabBar::tab:selected { color: #FFFFFF; border-bottom: 2px solid #FFFFFF; }

QGroupBox { border: 1px solid rgba(255,255,255,0.07); border-radius: 10px; margin-top: 18px; padding: 10px 8px 8px 8px; }
QGroupBox::title { subcontrol-origin: margin; left: 12px; padding: 0 4px; color: #A1A1A6; font-weight: 600; }

/* Progress: a thin track with a silver fill; any percentage is shown as text beside it. */
QProgressBar { background: rgba(255,255,255,0.08); border: none; border-radius: 3px; min-height: 6px; max-height: 6px; color: transparent; }
QProgressBar::chunk { border-radius: 3px; background: qlineargradient(x1:0, y1:0, x2:1, y2:0, stop:0 #8E8F96, stop:1 #FFFFFF); }

QCheckBox, QRadioButton { spacing: 8px; }
QCheckBox::indicator, QRadioButton::indicator { width: 16px; height: 16px; border: 1px solid rgba(255,255,255,0.22); background: #070708; }
QCheckBox::indicator { border-radius: 5px; }
QRadioButton::indicator { border-radius: 8px; }
QCheckBox::indicator:hover, QRadioButton::indicator:hover { border-color: rgba(255,255,255,0.45); }
QCheckBox::indicator:checked { image: url(:/documents/checkmark.svg); background: #FFFFFF; border-color: #FFFFFF; }
QCheckBox::indicator:checked:disabled { image: url(:/documents/checkmark-disabled.svg); background: #2C2C2F; border-color: rgba(255,255,255,0.08); }
QRadioButton::indicator:checked { background: qradialgradient(cx:0.5, cy:0.5, radius:0.5, fx:0.5, fy:0.5, stop:0 #0B0B0C, stop:0.38 #0B0B0C, stop:0.45 #FFFFFF, stop:1 #FFFFFF); border-color: #FFFFFF; }
QCheckBox::indicator:disabled, QRadioButton::indicator:disabled { border-color: rgba(255,255,255,0.08); }

QSlider::groove:horizontal { height: 4px; background: #242427; border-radius: 2px; }
QSlider::sub-page:horizontal { background: #F5F5F7; border-radius: 2px; }
QSlider::handle:horizontal { width: 18px; margin: -7px 0; border-radius: 9px; border: 1px solid rgba(0,0,0,0.4);
  background: qlineargradient(x1:0, y1:0, x2:0, y2:1, stop:0 #FFFFFF, stop:1 #CFD0D5); }

QScrollBar:vertical { background: transparent; width: 10px; margin: 2px; }
QScrollBar:horizontal { background: transparent; height: 10px; margin: 2px; }
QScrollBar::handle { background: rgba(255,255,255,0.14); border-radius: 3px; min-height: 28px; min-width: 28px; }
QScrollBar::handle:hover { background: rgba(255,255,255,0.26); }
QScrollBar::add-line, QScrollBar::sub-line, QScrollBar::add-page, QScrollBar::sub-page { background: none; border: none; width: 0; height: 0; }

QMenu { background: #17171A; border: 1px solid rgba(255,255,255,0.10); border-radius: 10px; padding: 5px; }
QMenu::item { padding: 6px 24px 6px 12px; border-radius: 6px; }
QMenu::item:selected { background: rgba(255,255,255,0.09); }
QMenu::item:disabled { color: #5A5A5F; }
QMenu::separator { height: 1px; background: rgba(255,255,255,0.07); margin: 5px 8px; }
QMenu::indicator { width: 14px; height: 14px; margin-left: 4px; }
QMenuBar { background: #0B0B0D; }
QMenuBar::item:selected { background: rgba(255,255,255,0.08); border-radius: 6px; }

QStatusBar { background: #0B0B0D; color: #8E8E93; border-top: 1px solid rgba(255,255,255,0.06); }
QStatusBar::item { border: none; }
QSplitter::handle { background: rgba(255,255,255,0.06); }
QAbstractScrollArea { background: transparent; }
QTextEdit#instanceRenameEditor { background: #1C1C1F; color: #F5F5F7; border: 1px solid rgba(255,255,255,0.55); border-radius: 6px; padding: 1px 2px; selection-background-color: rgba(255,255,255,0.85); selection-color: #0B0B0C; }

/* First-run setup: roomy, one idea per page. */
QWizard QLabel#qt_wizard_title { font-size: 20px; font-weight: 600; color: #FFFFFF; }
QWizard QLabel#qt_wizard_subtitle { font-size: 13px; color: #A1A1A6; }
QWizard QFrame#qt_wizard_header { background: #0E0E10; border: none; }
)");
}

QString ChromeTheme::tooltip()
{
    return QObject::tr("Black, white and brushed silver");
}
