// SPDX-License-Identifier: MIT
// The settings window: the layer switch, the last game's status, per-game profiles and every setting of
// settings.conf, saved as you change them (a running game picks them up within half a second).
#pragma once

#include <QMainWindow>
#include <QTimer>
#include <functional>
#include <vector>

#include "layerinstall.h"
#include "settings.h"

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QFormLayout;
class QLabel;
class QPushButton;
class QSlider;
class QSpinBox;

namespace qvgui {

class Preview;

class MainWindow : public QMainWindow {
    Q_OBJECT
  public:
    MainWindow();

  private:
    // A slider with a spin box for one float setting.
    QWidget* number(const QString& label, const QString& tip, double lo, double hi, double step, int decimals,
                    std::function<float&(qv::Settings&)> field, QFormLayout* form);
    void build();
    void loadProfile();
    void showSettings();
    void changed();
    void save();
    void refreshStatus();
    void refreshProfiles();
    void updateBudget();
    QString fileText() const;
    bool writeFileText(const QString& text);
    QString currentSection() const; // empty: all games

    qv::Settings s_;
    bool loading_ = false;
    QTimer saveTimer_, statusTimer_;
    Install install_;

    QCheckBox* enabled_ = nullptr;
    QLabel* installText_ = nullptr;
    QLabel* statusText_ = nullptr;
    QComboBox* profile_ = nullptr;
    QPushButton* removeProfile_ = nullptr;
    QComboBox* tracking_ = nullptr;
    QComboBox* filter_ = nullptr;
    QCheckBox* gazeFilter_ = nullptr;
    QCheckBox* debug_ = nullptr;
    QSpinBox* hold_ = nullptr;
    QSpinBox* refW_ = nullptr;
    QSpinBox* refH_ = nullptr;
    QLabel* budget_ = nullptr;
    QLabel* saved_ = nullptr;
    Preview* preview_ = nullptr;
    std::vector<std::function<void()>> refreshers_; // put s_ into the widgets
};

} // namespace qvgui
