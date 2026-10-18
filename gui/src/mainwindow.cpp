// SPDX-License-Identifier: MIT
#include "mainwindow.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDesktopServices>
#include <QDir>
#include <QDoubleSpinBox>
#include <QFile>
#include <QFileInfo>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QSaveFile>
#include <QScrollArea>
#include <QSlider>
#include <QSpinBox>
#include <QStatusBar>
#include <QUrl>
#include <QVBoxLayout>
#include <cmath>

#include "fovmath.h"
#include "preview.h"

namespace qvgui {

namespace {

QString settingsFile() {
    return QString::fromStdString(qv::settingsPath().string());
}

QString mp(uint64_t px) {
    return QString::number(px / 1e6, 'f', 1) + " MP";
}

} // namespace

MainWindow::MainWindow() {
    setWindowTitle(tr("Quad Views for Vulkan"));
    saveTimer_.setSingleShot(true);
    saveTimer_.setInterval(300);
    connect(&saveTimer_, &QTimer::timeout, this, &MainWindow::save);
    statusTimer_.setInterval(1000);
    connect(&statusTimer_, &QTimer::timeout, this, &MainWindow::refreshStatus);
    build();
    refreshProfiles();
    loadProfile();
    refreshStatus();
    statusTimer_.start();
}

QWidget* MainWindow::number(const QString& label, const QString& tip, double lo, double hi, double step,
                            int decimals, std::function<float&(qv::Settings&)> field, QFormLayout* form) {
    auto* w = new QWidget;
    auto* h = new QHBoxLayout(w);
    h->setContentsMargins(0, 0, 0, 0);
    auto* slider = new QSlider(Qt::Horizontal);
    const int steps = int(std::lround((hi - lo) / step));
    slider->setRange(0, steps);
    auto* spin = new QDoubleSpinBox;
    spin->setRange(lo, hi);
    spin->setSingleStep(step);
    spin->setDecimals(decimals);
    spin->setMinimumWidth(80);
    h->addWidget(slider, 1);
    h->addWidget(spin);
    w->setToolTip(tip);
    connect(slider, &QSlider::valueChanged, this, [=, this](int v) {
        if (!loading_) spin->setValue(lo + v * step);
    });
    connect(spin, &QDoubleSpinBox::valueChanged, this, [=, this](double v) {
        const bool was = loading_;
        loading_ = true;
        slider->setValue(int(std::lround((v - lo) / step)));
        loading_ = was;
        if (!loading_) {
            field(s_) = float(v);
            changed();
        }
    });
    refreshers_.push_back([=, this] {
        spin->setValue(field(s_));
        slider->setValue(int(std::lround((field(s_) - lo) / step)));
    });
    auto* l = new QLabel(label);
    l->setToolTip(tip);
    form->addRow(l, w);
    return w;
}

void MainWindow::build() {
    auto* central = new QWidget;
    auto* top = new QVBoxLayout(central);

    // The switch and where the layer is.
    auto* head = new QHBoxLayout;
    enabled_ = new QCheckBox(tr("Quad views layer on"));
    enabled_->setToolTip(tr("Games that ask for quad views get them from this layer. Off: the OpenXR loader does not "
                            "load the layer at all. Takes effect when a game starts."));
    connect(enabled_, &QCheckBox::toggled, this, [this](bool on) {
        if (loading_) return;
        QString err;
        if (!qvgui::setEnabled(install_, on, err)) {
            QMessageBox::warning(this, windowTitle(), err);
        }
        refreshStatus();
    });
    installText_ = new QLabel;
    installText_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    head->addWidget(enabled_);
    head->addWidget(installText_, 1);
    top->addLayout(head);

    // Profiles.
    auto* prof = new QHBoxLayout;
    prof->addWidget(new QLabel(tr("Settings for:")));
    profile_ = new QComboBox;
    profile_->setToolTip(tr("\"All games\" is the base; a game profile overrides it for the games whose OpenXR name "
                            "(app:) or process name (exe:) contains the text."));
    connect(profile_, &QComboBox::currentIndexChanged, this, [this] {
        if (!loading_) loadProfile();
    });
    auto* add = new QPushButton(tr("Add game profile..."));
    connect(add, &QPushButton::clicked, this, [this] {
        const Status st = readStatus();
        const QString guess = st.values.value("app").isEmpty() ? "app:" : "app:" + st.values.value("app");
        bool ok = false;
        const QString name = QInputDialog::getText(
            this, tr("Game profile"),
            tr("app:TEXT matches the game's OpenXR application name, exe:TEXT its process name.\n"
               "The last game was \"%1\" (process %2).")
                .arg(st.values.value("app"), st.values.value("exe")),
            QLineEdit::Normal, guess, &ok);
        if (!ok || !(name.startsWith("app:") || name.startsWith("exe:")) || name.size() < 5) return;
        writeFileText(QString::fromStdString(
            qv::replaceSection(fileText().toStdString(), name.toStdString(), s_)));
        refreshProfiles();
        profile_->setCurrentIndex(profile_->findData(name));
    });
    removeProfile_ = new QPushButton(tr("Remove"));
    connect(removeProfile_, &QPushButton::clicked, this, [this] {
        const QString sec = currentSection();
        if (sec.isEmpty()) return;
        writeFileText(QString::fromStdString(qv::removeSection(fileText().toStdString(), sec.toStdString())));
        refreshProfiles();
        profile_->setCurrentIndex(0);
        loadProfile();
    });
    prof->addWidget(profile_, 1);
    prof->addWidget(add);
    prof->addWidget(removeProfile_);
    top->addLayout(prof);

    auto* body = new QHBoxLayout;
    top->addLayout(body, 1);

    // Left: the settings.
    auto* scroll = new QScrollArea;
    scroll->setWidgetResizable(true);
    auto* left = new QWidget;
    auto* lv = new QVBoxLayout(left);
    scroll->setWidget(left);
    body->addWidget(scroll, 3);

    auto* res = new QGroupBox(tr("Resolution (applies when the game starts)"));
    auto* rf = new QFormLayout(res);
    number(tr("Peripheral density"),
           tr("Pixels of the wide views per pixel the runtime recommends. 0.4 = 40 % in each direction."), 0.05, 1.5,
           0.01, 2, [](qv::Settings& s) -> float& { return s.peripheralDensity; }, rf);
    number(tr("Focus density"),
           tr("Pixels of the focus views (and of the composed image) per recommended pixel. Above 1 is "
              "supersampling where you look."),
           0.5, 2.0, 0.01, 2, [](qv::Settings& s) -> float& { return s.focusDensity; }, rf);
    lv->addWidget(res);

    auto* area = new QGroupBox(tr("Focus area"));
    auto* af = new QFormLayout(area);
    tracking_ = new QComboBox;
    tracking_->addItem(tr("Follow the eyes (when the headset tracks them)"), int(qv::Tracking::Eye));
    tracking_->addItem(tr("Fixed in the middle"), int(qv::Tracking::Fixed));
    connect(tracking_, &QComboBox::currentIndexChanged, this, [this] {
        if (loading_) return;
        s_.tracking = qv::Tracking(tracking_->currentData().toInt());
        changed();
    });
    refreshers_.push_back([this] { tracking_->setCurrentIndex(tracking_->findData(int(s_.tracking))); });
    af->addRow(tr("Placement"), tracking_);
    number(tr("Gaze area width"), tr("Share of the eye's field (in tangent units) when the focus follows the gaze."),
           0.1, 0.9, 0.01, 2, [](qv::Settings& s) -> float& { return s.gazeSize[0]; }, af);
    number(tr("Gaze area height"), tr("Share of the eye's field when the focus follows the gaze."), 0.1, 0.9, 0.01, 2,
           [](qv::Settings& s) -> float& { return s.gazeSize[1]; }, af);
    number(tr("Fixed area width"), tr("Share of the eye's field without eye tracking (larger: the eyes move)."), 0.1,
           0.9, 0.01, 2, [](qv::Settings& s) -> float& { return s.fixedSize[0]; }, af);
    number(tr("Fixed area height"), tr("Share of the eye's field without eye tracking."), 0.1, 0.9, 0.01, 2,
           [](qv::Settings& s) -> float& { return s.fixedSize[1]; }, af);
    number(tr("Move right / left"), tr("Degrees, both eyes; + is right."), -30, 30, 0.5, 1,
           [](qv::Settings& s) -> float& { return s.offsetDeg[0]; }, af);
    number(tr("Move up / down"), tr("Degrees, both eyes; + is up (cockpits often want the focus a little lower)."),
           -30, 30, 0.5, 1, [](qv::Settings& s) -> float& { return s.offsetDeg[1]; }, af);
    lv->addWidget(area);

    auto* look = new QGroupBox(tr("Image"));
    auto* lf = new QFormLayout(look);
    number(tr("Blend band"), tr("Width of the soft edge of the focus, share of its size on each side. 0 = hard edge."),
           0.0, 0.5, 0.01, 2, [](qv::Settings& s) -> float& { return s.transition; }, lf);
    number(tr("Edge opacity"),
           tr("How much of the focus is already there at the outer edge of the blend band. 0 = fades in from the "
              "periphery, 0.5 = starts half way (as Pimax), 1 = hard edge."),
           0.0, 1.0, 0.05, 2, [](qv::Settings& s) -> float& { return s.edgeOpacity; }, lf);
    number(tr("Sharpen focus"), tr("Contrast-adaptive sharpening of the focus view (0 = off)."), 0.0, 1.0, 0.05, 2,
           [](qv::Settings& s) -> float& { return s.sharpen; }, lf);
    filter_ = new QComboBox;
    filter_->addItem(tr("Bicubic (sharper)"), int(qv::Filter::Bicubic));
    filter_->addItem(tr("Bilinear (cheaper)"), int(qv::Filter::Bilinear));
    connect(filter_, &QComboBox::currentIndexChanged, this, [this] {
        if (loading_) return;
        s_.peripheralFilter = qv::Filter(filter_->currentData().toInt());
        changed();
    });
    refreshers_.push_back([this] { filter_->setCurrentIndex(filter_->findData(int(s_.peripheralFilter))); });
    lf->addRow(tr("Upscale wide views"), filter_);
    debug_ = new QCheckBox(tr("Show the focus area in the headset"));
    debug_->setToolTip(tr("Tints the focus green, the blend band orange and marks the gaze: for tuning."));
    connect(debug_, &QCheckBox::toggled, this, [this](bool v) {
        if (loading_) return;
        s_.debugOverlay = v;
        changed();
    });
    refreshers_.push_back([this] { debug_->setChecked(s_.debugOverlay); });
    lf->addRow(QString(), debug_);
    lv->addWidget(look);

    auto* eye = new QGroupBox(tr("Eye tracking"));
    auto* ef = new QFormLayout(eye);
    gazeFilter_ = new QCheckBox(tr("Smooth the gaze (jumps at once on saccades)"));
    connect(gazeFilter_, &QCheckBox::toggled, this, [this](bool v) {
        if (loading_) return;
        s_.gazeFilter = v;
        changed();
    });
    refreshers_.push_back([this] { gazeFilter_->setChecked(s_.gazeFilter); });
    ef->addRow(QString(), gazeFilter_);
    hold_ = new QSpinBox;
    hold_->setRange(0, 5000);
    hold_->setSingleStep(50);
    hold_->setSuffix(tr(" ms"));
    hold_->setToolTip(tr("Keep the last gaze this long when the tracker loses the eyes (blinks)."));
    connect(hold_, &QSpinBox::valueChanged, this, [this](int v) {
        if (loading_) return;
        s_.gazeHoldMs = uint32_t(v);
        changed();
    });
    refreshers_.push_back([this] { hold_->setValue(int(s_.gazeHoldMs)); });
    ef->addRow(tr("Hold through blinks"), hold_);
    lv->addWidget(eye);

    auto* buttons = new QHBoxLayout;
    auto* reset = new QPushButton(tr("Defaults"));
    connect(reset, &QPushButton::clicked, this, [this] {
        s_ = qv::Settings{};
        showSettings();
        changed();
    });
    auto* openLog = new QPushButton(tr("Open the log"));
    connect(openLog, &QPushButton::clicked, this,
            [] { QDesktopServices::openUrl(QUrl::fromLocalFile(logPath())); });
    auto* openCfg = new QPushButton(tr("Open settings.conf"));
    connect(openCfg, &QPushButton::clicked, this,
            [] { QDesktopServices::openUrl(QUrl::fromLocalFile(settingsFile())); });
    buttons->addWidget(reset);
    buttons->addStretch(1);
    buttons->addWidget(openLog);
    buttons->addWidget(openCfg);
    lv->addLayout(buttons);
    lv->addStretch(1);

    // Right: preview, budget, status.
    auto* right = new QVBoxLayout;
    body->addLayout(right, 2);
    preview_ = new Preview;
    right->addWidget(preview_, 1);
    auto* bud = new QGroupBox(tr("Pixels per frame"));
    auto* bv = new QVBoxLayout(bud);
    auto* refRow = new QHBoxLayout;
    refRow->addWidget(new QLabel(tr("Recommended per eye:")));
    refW_ = new QSpinBox;
    refH_ = new QSpinBox;
    for (QSpinBox* b : {refW_, refH_}) {
        b->setRange(256, 16384);
        b->setSingleStep(16);
        connect(b, &QSpinBox::valueChanged, this, &MainWindow::updateBudget);
    }
    refRow->addWidget(refW_);
    refRow->addWidget(new QLabel("x"));
    refRow->addWidget(refH_);
    refRow->addStretch(1);
    bv->addLayout(refRow);
    budget_ = new QLabel;
    budget_->setWordWrap(true);
    bv->addWidget(budget_);
    refW_->setValue(3552); // until a game reports the headset's size
    refH_->setValue(3840);
    right->addWidget(bud);
    auto* stat = new QGroupBox(tr("Last game"));
    auto* sv = new QVBoxLayout(stat);
    statusText_ = new QLabel;
    statusText_->setWordWrap(true);
    statusText_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    sv->addWidget(statusText_);
    right->addWidget(stat);

    saved_ = new QLabel;
    statusBar()->addPermanentWidget(saved_);
    setCentralWidget(central);
    resize(1100, 760);
}

QString MainWindow::fileText() const {
    QFile f(settingsFile());
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) return {};
    return QString::fromUtf8(f.readAll());
}

bool MainWindow::writeFileText(const QString& text) {
    const QString path = settingsFile();
    QDir().mkpath(QFileInfo(path).absolutePath());
    QSaveFile f(path); // temp file + rename: the layer never reads half a file
    if (!f.open(QIODevice::WriteOnly | QIODevice::Text)) return false;
    f.write(text.toUtf8());
    return f.commit();
}

QString MainWindow::currentSection() const {
    return profile_->currentData().toString();
}

void MainWindow::refreshProfiles() {
    loading_ = true;
    const QString keep = currentSection();
    profile_->clear();
    profile_->addItem(tr("All games"), QString());
    for (const auto& n : qv::sectionNames(fileText().toStdString())) {
        const QString q = QString::fromStdString(n);
        profile_->addItem(q.startsWith("app:") ? tr("Game \"%1\"").arg(q.mid(4)) : tr("Process \"%1\"").arg(q.mid(4)),
                          q);
    }
    const int i = profile_->findData(keep);
    profile_->setCurrentIndex(i < 0 ? 0 : i);
    loading_ = false;
}

void MainWindow::loadProfile() {
    const std::string text = fileText().toStdString();
    const QString sec = currentSection();
    s_ = sec.isEmpty() ? qv::parseSettings(text, qv::AppId{"\x01", "\x01"}) // no section matches
                       : qv::parseSection(text, sec.toStdString());
    removeProfile_->setEnabled(!sec.isEmpty());
    showSettings();
}

void MainWindow::showSettings() {
    loading_ = true;
    for (auto& r : refreshers_) r();
    loading_ = false;
    preview_->setSettings(s_);
    updateBudget();
}

void MainWindow::changed() {
    preview_->setSettings(s_);
    updateBudget();
    saved_->setText(tr("Saving..."));
    saveTimer_.start();
}

void MainWindow::save() {
    const std::string text = fileText().toStdString();
    const QString sec = currentSection();
    const std::string out = sec.isEmpty() ? qv::replaceGlobal(text, s_) : qv::replaceSection(text, sec.toStdString(), s_);
    saved_->setText(writeFileText(QString::fromStdString(out)) ? tr("Saved to %1").arg(settingsFile())
                                                                : tr("Could not write %1").arg(settingsFile()));
}

void MainWindow::updateBudget() {
    const qv::Size rec{uint32_t(refW_->value()), uint32_t(refH_->value())};
    const qv::Budget g = qv::budget(rec, s_, true), f = qv::budget(rec, s_, false);
    const qv::Size p = qv::scaledSize(rec, {}, s_.peripheralDensity);
    const qv::Size fg = qv::scaledSize(rec, {}, s_.focusDensity, s_.gazeSize[0], s_.gazeSize[1]);
    const qv::Size ff = qv::scaledSize(rec, {}, s_.focusDensity, s_.fixedSize[0], s_.fixedSize[1]);
    budget_->setText(tr("Plain stereo: %1.<br>With eye tracking: wide %2x%3 + focus %4x%5 per eye = <b>%6 "
                        "(%7 % fewer)</b>.<br>Fixed: focus %8x%9 = <b>%10 (%11 % fewer)</b>.")
                         .arg(mp(g.stereoPixels))
                         .arg(p.w)
                         .arg(p.h)
                         .arg(fg.w)
                         .arg(fg.h)
                         .arg(mp(g.quadPixels))
                         .arg(int(std::lround(g.saving * 100)))
                         .arg(ff.w)
                         .arg(ff.h)
                         .arg(mp(f.quadPixels))
                         .arg(int(std::lround(f.saving * 100))));
}

void MainWindow::refreshStatus() {
    install_ = findInstall();
    loading_ = true;
    enabled_->setEnabled(install_.userInstalled);
    enabled_->setChecked(install_.userInstalled ? install_.userEnabled : !install_.systemManifest.isEmpty());
    loading_ = false;
    if (install_.userInstalled) {
        installText_->setText(tr("<small>%1</small>").arg(install_.userManifest));
    } else if (!install_.systemManifest.isEmpty()) {
        installText_->setText(tr("<small>Installed system-wide (%1); this switch only works for a user install. "
                                 "Disable it for one game with DISABLE_XR_APILAYER_ARTSCOUT_QUAD_VIEWS_VULKAN=1.</small>")
                                  .arg(install_.systemManifest));
    } else {
        installText_->setText(tr("<small>Not installed: cmake --install build (see README).</small>"));
    }

    const Status st = readStatus();
    if (!st.valid()) {
        statusText_->setText(tr("No game has used the layer yet."));
        return;
    }
    const auto v = [&](const char* k) { return st.values.value(k).toHtmlEscaped(); };
    const qint64 age = st.written.secsTo(QDateTime::currentDateTime());
    const bool live = age < 5 && (v("session") == "quad" || v("session") == "stereo");
    const QString session = v("session") == "quad"     ? tr("quad views")
                            : v("session") == "stereo" ? tr("stereo (the game did not ask for quad views)")
                                                       : tr("ended");
    const QString eye = v("eye_tracking") == "off" ? tr("off (fixed area)")
                        : !live                       ? tr("available")
                        : v("eye_tracking") == "live" ? tr("following the gaze")
                                                      : tr("no gaze right now (fixed area)");
    statusText_->setText(
        tr("<b>%1</b> %2<br>Runtime: %3, %4<br>Session: %5<br>Eye tracking: %6<br>Wide %7, focus %8, composed "
           "%9 per eye<br>Composition: %10 ms GPU")
            .arg(v("app"), live ? tr("(running)") : tr("(%1 ago)").arg(age < 120 ? tr("%1 s").arg(age)
                                                                                  : tr("%1 min").arg(age / 60)),
                 v("runtime"), v("system"), session, eye, v("peripheral"), v("focus"), v("canvas"))
            .arg(QString::number(st.values.value("compose_gpu_ms").toDouble(), 'f', 2)));
    // The recommended size of the headset, for the pixel budget.
    const QStringList rec = st.values.value("recommended").split('x');
    if (rec.size() == 2 && rec[0].toInt() > 0 && refW_->property("fromStatus").toString() != st.values["recommended"]) {
        refW_->setValue(rec[0].toInt());
        refH_->setValue(rec[1].toInt());
        refW_->setProperty("fromStatus", st.values["recommended"]);
    }
}

} // namespace qvgui
