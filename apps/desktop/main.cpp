#include "legion/backends/lenovo/lenovo_rgb.hpp"
#include "legion/backends/lenovo/lenovo_telemetry.hpp"
#include "legion/backends/linux/linux_telemetry.hpp"
#include "legion/backends/nvidia/nvidia_telemetry.hpp"
#include "legion/core/interfaces.hpp"
#include "legion/core/lighting.hpp"
#include "legion/core/types.hpp"

#include <QApplication>
#include <QCheckBox>
#include <QColor>
#include <QColorDialog>
#include <QComboBox>
#include <QCoreApplication>
#include <QDir>
#include <QDoubleSpinBox>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QFontMetrics>
#include <QFormLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QMainWindow>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPen>
#include <QProcess>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QStringList>
#include <QTabWidget>
#include <QTableWidget>
#include <QTimer>
#include <QVBoxLayout>
#include <QVector>
#include <QWidget>

#include <algorithm>
#include <array>
#include <cerrno>
#include <cmath>
#include <cstring>
#include <functional>
#include <iostream>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/un.h>
#include <unistd.h>
#include <vector>

namespace {

QColor temp_color(double value, double warn, double crit) {
    if (value >= crit) return QColor("#ff4d4d");
    if (value >= warn) return QColor("#ffb300");
    return QColor("#00e676");
}

QString format_value(double value, int decimals) {
    return QString::number(value, 'f', decimals);
}

QColor to_qcolor(const legion::core::Rgb& rgb) {
    return QColor(rgb.r, rgb.g, rgb.b);
}

legion::core::Rgb from_qcolor(const QColor& color) {
    return {static_cast<std::uint8_t>(color.red()),
            static_cast<std::uint8_t>(color.green()),
            static_cast<std::uint8_t>(color.blue())};
}

std::string effect_label(legion::core::LightingEffect effect) {
    switch (effect) {
        case legion::core::LightingEffect::Off: return "Off";
        case legion::core::LightingEffect::Static: return "Static";
        case legion::core::LightingEffect::Breathing: return "Breathing";
        case legion::core::LightingEffect::Wave: return "Wave";
        case legion::core::LightingEffect::Smooth: return "Smooth (hue cycle)";
        case legion::core::LightingEffect::Heartbeat: return "Heartbeat (software)";
        case legion::core::LightingEffect::Rainbow: return "Rainbow (software)";
        case legion::core::LightingEffect::Dance: return "Dance / disco (software)";
        case legion::core::LightingEffect::Random: return "Random (software)";
        case legion::core::LightingEffect::Reactive: return "Reactive (software)";
    }
    return "Static";
}

bool notify_lightd() {
    const std::string path = legion::core::default_lightd_socket_path();
    const int fd = ::socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
    if (fd < 0) return false;
    struct sockaddr_un address {};
    address.sun_family = AF_UNIX;
    if (path.size() >= sizeof(address.sun_path)) {
        ::close(fd);
        return false;
    }
    std::strncpy(address.sun_path, path.c_str(), sizeof(address.sun_path) - 1);
    struct timeval timeout {};
    timeout.tv_sec = 0;
    timeout.tv_usec = 300000;
    ::setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
    ::setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));
    if (::connect(fd, reinterpret_cast<struct sockaddr*>(&address), sizeof(address)) != 0) {
        ::close(fd);
        return false;
    }
    const char message[] = "reload\n";
    if (::write(fd, message, sizeof(message) - 1) < 0) {
        ::close(fd);
        return false;
    }
    char reply[32] = {0};
    const ssize_t count = ::read(fd, reply, sizeof(reply) - 1);
    ::close(fd);
    return count > 0;
}

struct GaugeArc {
    QString label;
    double value = 0.0;
    double min = 0.0;
    double max = 100.0;
    QColor color;
    bool valid = false;
    QString unit;
    int decimals = 0;
};

class MultiArcGauge final : public QWidget {
public:
    explicit MultiArcGauge(const QString& title, QWidget* parent = nullptr)
        : QWidget(parent), title_(title) {
        setMinimumSize(300, 300);
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    }

    void set_arcs(const QVector<GaugeArc>& arcs) {
        arcs_ = arcs;
        update();
    }

    void set_center(const QString& primary, const QString& secondary) {
        primary_ = primary;
        secondary_ = secondary;
        update();
    }

protected:
    void paintEvent(QPaintEvent*) override {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);

        const int legend_height = arcs_.isEmpty() ? 0 : 32;
        const int title_height = 22;
        const int padding = 12;
        const int available = height() - legend_height - title_height - padding * 2;
        const int side = std::max(80, std::min(width() - padding * 2, available));
        const QRectF outer((width() - side) / 2.0,
                           padding + (available - side) / 2.0,
                           side, side);
        const double center_y = outer.center().y();

        for (int i = 0; i < arcs_.size(); ++i) {
            const auto& arc = arcs_[i];
            const double inset = 7.0 + i * 15.0;
            const QRectF ring = outer.adjusted(inset, inset, -inset, -inset);
            if (ring.width() < 12.0) break;
            QPen track(QColor("#242a36"), 10, Qt::SolidLine, Qt::RoundCap);
            painter.setPen(track);
            painter.drawArc(ring, 225 * 16, -270 * 16);
            if (arc.valid && arc.max > arc.min) {
                const double ratio = std::clamp((arc.value - arc.min) / (arc.max - arc.min), 0.0, 1.0);
                QPen pen(arc.color, 10, Qt::SolidLine, Qt::RoundCap);
                painter.setPen(pen);
                painter.drawArc(ring, 225 * 16, static_cast<int>(-270 * 16 * ratio));
            }
        }

        QFont font = painter.font();
        font.setPointSize(21);
        font.setBold(true);
        painter.setFont(font);
        painter.setPen(QColor("#f0f4f8"));
        painter.drawText(QRectF(0, center_y - 34, width(), 34), Qt::AlignCenter, primary_);

        font.setPointSize(9);
        font.setBold(false);
        painter.setFont(font);
        painter.setPen(QColor("#9aa5b1"));
        painter.drawText(QRectF(0, center_y + 2, width(), 22), Qt::AlignCenter, secondary_);

        if (!arcs_.isEmpty()) {
            const int legend_y = height() - title_height - legend_height;
            const int cell_width = std::max(1, width() / static_cast<int>(arcs_.size()));
            font.setPointSize(8);
            font.setBold(false);
            painter.setFont(font);
            for (int i = 0; i < arcs_.size(); ++i) {
                const auto& arc = arcs_[i];
                const int x = i * cell_width + 8;
                painter.setPen(Qt::NoPen);
                painter.setBrush(arc.color);
                painter.drawEllipse(QPointF(x + 4, legend_y + 9), 4, 4);
                const QString value_text = arc.valid
                    ? format_value(arc.value, arc.decimals) + " " + arc.unit
                    : QString("--");
                QString text = arc.label + " " + value_text;
                const int max_width = cell_width - 24;
                if (max_width > 10) {
                    text = QFontMetrics(font).elidedText(text, Qt::ElideRight, max_width);
                }
                painter.setPen(QColor("#cbd2d9"));
                painter.drawText(QRect(x + 13, legend_y, cell_width - 16, 18),
                                 Qt::AlignVCenter | Qt::AlignLeft, text);
            }
        }

        font.setPointSize(11);
        font.setBold(true);
        painter.setFont(font);
        painter.setPen(QColor("#00d4ff"));
        painter.drawText(QRectF(0, height() - title_height, width(), title_height),
                         Qt::AlignCenter, title_);
    }

private:
    QString title_;
    QString primary_ = "--";
    QString secondary_;
    QVector<GaugeArc> arcs_;
};

class KeyboardWidget final : public QWidget {
public:
    explicit KeyboardWidget(QWidget* parent = nullptr) : QWidget(parent) {
        setMinimumSize(560, 230);
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
        setCursor(Qt::PointingHandCursor);
    }

    void set_zone_colors(const std::array<QColor, 4>& colors) {
        base_colors_ = colors;
        if (!has_preview_) {
            display_colors_ = colors;
        }
        update();
    }

    void set_preview_colors(const std::array<QColor, 4>& colors) {
        display_colors_ = colors;
        has_preview_ = true;
        update();
    }

    void set_highlight_zone(int zone) {
        highlight_zone_ = zone;
        update();
    }

    void set_zone_click_handler(std::function<void(int)> handler) {
        click_handler_ = std::move(handler);
    }

protected:
    void paintEvent(QPaintEvent*) override {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);

        const QRectF body = QRectF(rect()).adjusted(4, 4, -4, -22);
        QPainterPath body_path;
        body_path.addRoundedRect(body, 14, 14);
        painter.fillPath(body_path, QColor("#0f1218"));
        painter.setPen(QPen(QColor("#2b3140"), 1.5));
        painter.drawPath(body_path);

        const int rows = 5;
        const int keys_per_row = 15;
        const double pad = 12.0;
        const double gap = 4.0;
        const double key_height = (body.height() - pad * 2 - gap * (rows - 1)) / rows;
        const double key_width = (body.width() - pad * 2 - gap * (keys_per_row - 1)) / keys_per_row;
        const auto& colors = has_preview_ ? display_colors_ : base_colors_;

        for (int row = 0; row < rows; ++row) {
            const double y = body.top() + pad + row * (key_height + gap);
            for (int key = 0; key < keys_per_row; ++key) {
                const double x = body.left() + pad + key * (key_width + gap);
                const int zone = std::clamp(
                    static_cast<int>(((x + key_width / 2.0) / body.width()) * 4.0), 0, 3);
                QColor color = colors[static_cast<std::size_t>(zone)];
                color = color.darker(210);
                if (highlight_zone_ == zone) {
                    color = colors[static_cast<std::size_t>(zone)].lighter(150);
                }
                const QRectF cap(x, y, key_width, key_height);
                QPainterPath cap_path;
                cap_path.addRoundedRect(cap, 3, 3);
                painter.fillPath(cap_path, color);
                painter.setPen(QPen(QColor("#0b0d12").lighter(125), 1));
                painter.drawPath(cap_path);
                painter.setPen(QPen(QColor(255, 255, 255, 22), 1));
                painter.drawLine(QPointF(cap.left() + 2, cap.top() + 1),
                                 QPointF(cap.right() - 2, cap.top() + 1));
            }
        }

        const QFont font = painter.font();
        QFont small = font;
        small.setPointSize(8);
        painter.setFont(small);
        painter.setPen(QColor("#9aa5b1"));
        const int zone_width = width() / 4;
        for (int zone = 0; zone < 4; ++zone) {
            painter.drawText(QRect(zone * zone_width, height() - 18, zone_width, 16),
                             Qt::AlignCenter, QString("Zone %1").arg(zone + 1));
        }
    }

    void mousePressEvent(QMouseEvent* event) override {
        const int zone = std::clamp(
            static_cast<int>((event->position().x() / std::max(1, width())) * 4.0), 0, 3);
        if (click_handler_) click_handler_(zone);
    }

private:
    std::array<QColor, 4> base_colors_ = {QColor("#00d4ff"), QColor("#00d4ff"),
                                          QColor("#00d4ff"), QColor("#00d4ff")};
    std::array<QColor, 4> display_colors_ = base_colors_;
    bool has_preview_ = false;
    int highlight_zone_ = -1;
    std::function<void(int)> click_handler_;
};

}  // namespace

class LegionMainWindow final : public QMainWindow {
public:
    LegionMainWindow()
        : sysfs_(legion::core::create_posix_sysfs_reader()),
          runner_(legion::core::create_posix_process_runner()),
          linux_provider_(*sysfs_),
          lenovo_provider_(*sysfs_),
          nvidia_provider_(*runner_) {
        setWindowTitle("Legion Linux Hardware Platform");
        resize(1360, 900);
        setStyleSheet(
            "QMainWindow, QWidget { background-color: #12141a; color: #e4e7eb; }"
            "QGroupBox { border: 1px solid #2b3140; border-radius: 8px; margin-top: 12px; padding-top: 12px; font-weight: bold; color: #00d4ff; }"
            "QGroupBox::title { subcontrol-origin: margin; left: 12px; padding: 0 6px; }"
            "QPushButton { background-color: #1f2937; border: 1px solid #374151; border-radius: 6px; padding: 6px 14px; color: #f9fafb; }"
            "QPushButton:hover { background-color: #374151; border-color: #00d4ff; }"
            "QPushButton:disabled { color: #6b7280; border-color: #2b3140; }"
            "QComboBox, QSpinBox, QDoubleSpinBox { background-color: #1f2937; border: 1px solid #374151; border-radius: 6px; padding: 4px 10px; }"
            "QTableWidget { background-color: #181b23; gridline-color: #2b3140; border: 1px solid #2b3140; }"
            "QHeaderView::section { background-color: #1f2430; color: #9aa5b1; padding: 6px; border: none; }"
            "QTabWidget::pane { border: 1px solid #2b3140; border-radius: 6px; }"
            "QTabBar::tab { background: #181b23; padding: 8px 16px; margin-right: 2px; border-top-left-radius: 6px; border-top-right-radius: 6px; }"
            "QTabBar::tab:selected { background: #232834; color: #00d4ff; font-weight: bold; }"
            "QCheckBox { spacing: 6px; }");

        load_lighting_config();

        auto* central = new QWidget(this);
        auto* root_layout = new QVBoxLayout(central);

        header_label_ = new QLabel("Loading hardware info...", this);
        header_label_->setStyleSheet("font-size: 15px; font-weight: bold; color: #00d4ff; padding: 4px;");
        root_layout->addWidget(header_label_);

        auto* toolbar = new QHBoxLayout();
        auto* refresh_btn = new QPushButton("Refresh Now", this);
        auto_refresh_cb_ = new QCheckBox("Auto-refresh", this);
        auto_refresh_cb_->setChecked(true);
        interval_spin_ = new QDoubleSpinBox(this);
        interval_spin_->setRange(0.1, 60.0);
        interval_spin_->setSingleStep(0.1);
        interval_spin_->setDecimals(1);
        interval_spin_->setValue(1.0);
        interval_spin_->setSuffix(" s");
        interval_spin_->setToolTip("Telemetry refresh interval (0.1 s minimum)");
        status_label_ = new QLabel("Ready (read-only telemetry)", this);
        status_label_->setStyleSheet("color: #9aa5b1;");
        toolbar->addWidget(refresh_btn);
        toolbar->addWidget(auto_refresh_cb_);
        toolbar->addWidget(new QLabel("Interval:", this));
        toolbar->addWidget(interval_spin_);
        toolbar->addStretch();
        toolbar->addWidget(status_label_);
        root_layout->addLayout(toolbar);

        auto* tabs = new QTabWidget(this);

        auto* dash_tab = new QWidget(this);
        auto* dash_grid = new QGridLayout(dash_tab);
        dash_grid->setSpacing(14);
        cpu_gauge_ = new MultiArcGauge("CPU", this);
        gpu_gauge_ = new MultiArcGauge("GPU", this);
        nvme_gauge_ = new MultiArcGauge("NVMe / Storage", this);
        battery_gauge_ = new MultiArcGauge("Battery", this);
        memory_gauge_ = new MultiArcGauge("Memory", this);
        fan_gauge_ = new MultiArcGauge("Fans", this);
        dash_grid->addWidget(cpu_gauge_, 0, 0);
        dash_grid->addWidget(gpu_gauge_, 0, 1);
        dash_grid->addWidget(nvme_gauge_, 1, 0);
        dash_grid->addWidget(battery_gauge_, 1, 1);
        dash_grid->addWidget(memory_gauge_, 2, 0);
        dash_grid->addWidget(fan_gauge_, 2, 1);
        tabs->addTab(dash_tab, "Dashboard");

        auto* controls_tab = new QWidget(this);
        auto* controls_layout = new QVBoxLayout(controls_tab);
        auto* note = new QLabel(
            "Hardware controls run through the allowlisted legion-helper. With the installed Polkit rule "
            "you are not asked for a password for these narrow actions.",
            this);
        note->setWordWrap(true);
        note->setStyleSheet("color: #9aa5b1; padding: 4px;");
        controls_layout->addWidget(note);

        auto* form_group = new QGroupBox("Lenovo & Power Controls", this);
        auto* form = new QFormLayout(form_group);

        profile_combo_ = new QComboBox(this);
        profile_combo_->addItems({"low-power", "balanced", "performance", "custom"});
        auto* apply_profile_btn = new QPushButton("Apply Profile", this);
        auto* profile_row = new QHBoxLayout();
        profile_row->addWidget(profile_combo_);
        profile_row->addWidget(apply_profile_btn);
        form->addRow("Platform Profile:", profile_row);

        auto add_toggle_row = [&](const QString& label, const QString& key, QLabel*& state_lbl) {
            state_lbl = new QLabel("?", this);
            auto* on_btn = new QPushButton("Enable (1)", this);
            auto* off_btn = new QPushButton("Disable (0)", this);
            auto* row = new QHBoxLayout();
            row->addWidget(state_lbl);
            row->addStretch();
            row->addWidget(on_btn);
            row->addWidget(off_btn);
            form->addRow(label, row);
            connect(on_btn, &QPushButton::clicked, this, [this, key]() { apply_control(key, "1"); });
            connect(off_btn, &QPushButton::clicked, this, [this, key]() { apply_control(key, "0"); });
        };

        add_toggle_row("Conservation Mode:", "conservation_mode", conservation_lbl_);
        add_toggle_row("Fn Lock:", "fn_lock", fn_lock_lbl_);
        add_toggle_row("Always-On USB Charging:", "usb_charging", usb_charging_lbl_);
        add_toggle_row("Camera Power:", "camera_power", camera_lbl_);

        controls_layout->addWidget(form_group);
        controls_layout->addStretch();
        tabs->addTab(controls_tab, "Controls");

        auto* rgb_tab = new QWidget(this);
        auto* rgb_layout = new QVBoxLayout(rgb_tab);
        auto* rgb_group = new QGroupBox("4-Zone RGB Keyboard (ITE 8295 / 048D:C965)", this);
        auto* rgb_root = new QVBoxLayout(rgb_group);

        auto* profile_row2 = new QHBoxLayout();
        profile_row2->addWidget(new QLabel("Profile:", this));
        rgb_profile_combo_ = new QComboBox(this);
        rgb_profile_combo_->addItems({"Profile 1", "Profile 2", "Profile 3", "Profile 4"});
        rgb_profile_combo_->setCurrentIndex(current_profile_);
        profile_row2->addWidget(rgb_profile_combo_);
        profile_row2->addStretch();
        auto* apply_rgb_btn = new QPushButton("Apply & Save", this);
        auto* off_rgb_btn = new QPushButton("Turn Off", this);
        profile_row2->addWidget(apply_rgb_btn);
        profile_row2->addWidget(off_rgb_btn);
        rgb_root->addLayout(profile_row2);

        apply_on_login_cb_ = new QCheckBox("Apply this profile at login (legion-lightd)", this);
        apply_on_login_cb_->setChecked(lighting_config_.apply_on_login);
        rgb_root->addWidget(apply_on_login_cb_);

        auto* rgb_body = new QHBoxLayout();
        auto* keyboard_column = new QVBoxLayout();
        keyboard_ = new KeyboardWidget(this);
        keyboard_->set_zone_click_handler([this](int zone) { choose_zone_color(zone); });
        keyboard_column->addWidget(keyboard_);
        auto* zone_row = new QHBoxLayout();
        for (int zone = 0; zone < 4; ++zone) {
            zone_buttons_[static_cast<std::size_t>(zone)] = new QPushButton(QString("Zone %1").arg(zone + 1), this);
            zone_row->addWidget(zone_buttons_[static_cast<std::size_t>(zone)]);
            connect(zone_buttons_[static_cast<std::size_t>(zone)], &QPushButton::clicked, this,
                    [this, zone]() { choose_zone_color(zone); });
        }
        keyboard_column->addLayout(zone_row);
        rgb_body->addLayout(keyboard_column, 3);

        auto* rgb_form = new QFormLayout();
        rgb_mode_combo_ = new QComboBox(this);
        const std::array<legion::core::LightingEffect, 10> effects = {
            legion::core::LightingEffect::Off,
            legion::core::LightingEffect::Static,
            legion::core::LightingEffect::Breathing,
            legion::core::LightingEffect::Wave,
            legion::core::LightingEffect::Smooth,
            legion::core::LightingEffect::Heartbeat,
            legion::core::LightingEffect::Rainbow,
            legion::core::LightingEffect::Dance,
            legion::core::LightingEffect::Random,
            legion::core::LightingEffect::Reactive
        };
        for (const auto effect : effects) {
            rgb_mode_combo_->addItem(QString::fromStdString(effect_label(effect)),
                                     static_cast<int>(effect));
        }
        rgb_form->addRow("Effect:", rgb_mode_combo_);

        rgb_speed_spin_ = new QSpinBox(this);
        rgb_speed_spin_->setRange(1, 4);
        rgb_speed_spin_->setValue(2);
        rgb_form->addRow("Speed:", rgb_speed_spin_);

        rgb_brightness_combo_ = new QComboBox(this);
        rgb_brightness_combo_->addItems({"Low (1)", "High (2)"});
        rgb_brightness_combo_->setCurrentIndex(1);
        rgb_form->addRow("Brightness:", rgb_brightness_combo_);

        rgb_direction_combo_ = new QComboBox(this);
        rgb_direction_combo_->addItems({"Left to right", "Right to left"});
        rgb_form->addRow("Wave direction:", rgb_direction_combo_);

        rgb_kind_label_ = new QLabel(this);
        rgb_kind_label_->setWordWrap(true);
        rgb_kind_label_->setStyleSheet("color: #9aa5b1;");
        rgb_form->addRow("", rgb_kind_label_);

        rgb_status_label_ = new QLabel("Ready.", this);
        rgb_status_label_->setWordWrap(true);
        rgb_status_label_->setStyleSheet("color: #9aa5b1;");
        rgb_form->addRow("Status:", rgb_status_label_);

        rgb_body->addLayout(rgb_form, 2);
        rgb_root->addLayout(rgb_body);

        auto* rgb_note = new QLabel(
            "Native firmware effects: Off, Static, Breathing, Wave, Smooth. Heartbeat, Rainbow, Dance, "
            "Random and Reactive are synthesized in software by legion-lightd (or by this app when the daemon "
            "is not running). Lenovo's own Vantage profile slots are not programmable through the documented "
            "HID protocol; this app keeps four equivalent userspace profiles instead. Always confirm the physical "
            "keyboard changed. Reactive follows clicks on this preview; physical typing reactivity needs "
            "input-device access and is intentionally not enabled.",
            this);
        rgb_note->setWordWrap(true);
        rgb_note->setStyleSheet("color: #9aa5b1; padding: 4px;");
        rgb_root->addWidget(rgb_note);

        rgb_layout->addWidget(rgb_group);
        rgb_layout->addStretch();
        tabs->addTab(rgb_tab, "4-Zone RGB");

        auto* raw_tab = new QWidget(this);
        auto* raw_layout = new QVBoxLayout(raw_tab);
        sensors_table_ = new QTableWidget(0, 4, this);
        sensors_table_->setHorizontalHeaderLabels({"Sensor", "Value", "Unit", "State"});
        sensors_table_->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
        sensors_table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
        caps_table_ = new QTableWidget(0, 4, this);
        caps_table_->setHorizontalHeaderLabels({"Capability", "Backend", "State", "Notes"});
        caps_table_->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
        caps_table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
        raw_layout->addWidget(sensors_table_);
        raw_layout->addWidget(caps_table_);
        tabs->addTab(raw_tab, "Sensors & Capabilities");

        root_layout->addWidget(tabs);
        setCentralWidget(central);

        timer_ = new QTimer(this);
        timer_->setSingleShot(true);
        connect(timer_, &QTimer::timeout, this, &LegionMainWindow::refresh_data);
        connect(refresh_btn, &QPushButton::clicked, this, &LegionMainWindow::refresh_data);
        connect(auto_refresh_cb_, &QCheckBox::toggled, this, [this](bool on) {
            if (on) {
                timer_->start(refresh_interval_ms_);
            } else {
                timer_->stop();
            }
        });
        connect(interval_spin_, &QDoubleSpinBox::valueChanged, this, [this](double value) {
            refresh_interval_ms_ = std::max(100, static_cast<int>(std::lround(value * 1000.0)));
            if (auto_refresh_cb_->isChecked()) {
                timer_->start(refresh_interval_ms_);
            }
        });
        connect(apply_profile_btn, &QPushButton::clicked, this, [this]() {
            apply_control("platform_profile", profile_combo_->currentText());
        });

        connect(rgb_profile_combo_, &QComboBox::currentIndexChanged, this,
                &LegionMainWindow::on_profile_changed);
        connect(rgb_mode_combo_, &QComboBox::currentIndexChanged, this, [this](int) {
            update_effect_controls();
            store_controls_into_profile(current_profile_);
        });
        connect(rgb_speed_spin_, &QSpinBox::valueChanged, this, [this](int) {
            store_controls_into_profile(current_profile_);
        });
        connect(rgb_brightness_combo_, &QComboBox::currentIndexChanged, this, [this](int) {
            store_controls_into_profile(current_profile_);
        });
        connect(rgb_direction_combo_, &QComboBox::currentIndexChanged, this, [this](int) {
            store_controls_into_profile(current_profile_);
        });
        connect(apply_on_login_cb_, &QCheckBox::toggled, this, [this](bool enabled) {
            lighting_config_.apply_on_login = enabled;
        });
        connect(apply_rgb_btn, &QPushButton::clicked, this, &LegionMainWindow::apply_lighting);
        connect(off_rgb_btn, &QPushButton::clicked, this, [this]() {
            rgb_mode_combo_->setCurrentIndex(0);
            apply_lighting();
        });

        preview_timer_ = new QTimer(this);
        connect(preview_timer_, &QTimer::timeout, this, &LegionMainWindow::update_preview);
        preview_elapsed_.start();
        preview_timer_->start(40);

        live_timer_ = new QTimer(this);
        connect(live_timer_, &QTimer::timeout, this, &LegionMainWindow::send_live_frame);

        reactive_clear_timer_ = new QTimer(this);
        reactive_clear_timer_->setSingleShot(true);
        connect(reactive_clear_timer_, &QTimer::timeout, this, [this]() {
            keyboard_->set_highlight_zone(-1);
        });

        load_profile_into_controls(current_profile_);
        update_effect_controls();
        refresh_data();
    }

private:
    void load_lighting_config() {
        const auto text = legion::core::read_text_file(legion::core::default_lighting_config_path());
        if (text.has_value()) {
            const auto parsed = legion::core::parse_lighting_config(*text);
            if (parsed.has_value()) {
                lighting_config_ = *parsed;
                current_profile_ = std::clamp(lighting_config_.active_profile, 0, 3);
                return;
            }
        }
        lighting_config_ = legion::core::LightingConfig();
        current_profile_ = 0;
    }

    void on_profile_changed(int index) {
        if (index < 0 || index > 3) return;
        if (index == current_profile_) return;
        store_controls_into_profile(current_profile_);
        current_profile_ = index;
        load_profile_into_controls(index);
        update_effect_controls();
        rgb_status_label_->setText(QString("Editing Profile %1. Press Apply & Save to persist it.")
                                       .arg(index + 1));
    }

    void load_profile_into_controls(int index) {
        const auto& profile = lighting_config_.profiles[static_cast<std::size_t>(index)];
        {
            const QSignalBlocker block_mode(rgb_mode_combo_);
            const QSignalBlocker block_speed(rgb_speed_spin_);
            const QSignalBlocker block_brightness(rgb_brightness_combo_);
            const QSignalBlocker block_direction(rgb_direction_combo_);
            const int mode_index = rgb_mode_combo_->findData(static_cast<int>(profile.effect));
            rgb_mode_combo_->setCurrentIndex(mode_index >= 0 ? mode_index : 1);
            rgb_speed_spin_->setValue(std::clamp(profile.speed, 1, 4));
            rgb_brightness_combo_->setCurrentIndex(profile.brightness == 1 ? 0 : 1);
            rgb_direction_combo_->setCurrentIndex(
                profile.direction == legion::core::WaveDirection::LeftToRight ? 0 : 1);
        }
        for (std::size_t zone = 0; zone < profile.zones.size(); ++zone) {
            zone_colors_[zone] = to_qcolor(profile.zones[zone]);
            update_zone_button_style(zone_buttons_[zone], zone_colors_[zone]);
        }
        keyboard_->set_zone_colors(zone_colors_);
    }

    void store_controls_into_profile(int index) {
        if (index < 0 || index > 3) return;
        auto& profile = lighting_config_.profiles[static_cast<std::size_t>(index)];
        profile.effect = current_effect();
        profile.speed = rgb_speed_spin_->value();
        profile.brightness = rgb_brightness_combo_->currentIndex() == 0 ? 1 : 2;
        profile.direction = rgb_direction_combo_->currentIndex() == 0
            ? legion::core::WaveDirection::LeftToRight
            : legion::core::WaveDirection::RightToLeft;
        for (std::size_t zone = 0; zone < zone_colors_.size(); ++zone) {
            profile.zones[zone] = from_qcolor(zone_colors_[zone]);
        }
    }

    legion::core::LightingEffect current_effect() const {
        return static_cast<legion::core::LightingEffect>(rgb_mode_combo_->currentData().toInt());
    }

    legion::core::LightingProfile current_ui_profile() const {
        legion::core::LightingProfile profile;
        profile.effect = current_effect();
        profile.speed = rgb_speed_spin_->value();
        profile.brightness = rgb_brightness_combo_->currentIndex() == 0 ? 1 : 2;
        profile.direction = rgb_direction_combo_->currentIndex() == 0
            ? legion::core::WaveDirection::LeftToRight
            : legion::core::WaveDirection::RightToLeft;
        for (std::size_t zone = 0; zone < zone_colors_.size(); ++zone) {
            profile.zones[zone] = from_qcolor(zone_colors_[zone]);
        }
        return profile;
    }

    void update_effect_controls() {
        const auto effect = current_effect();
        const bool animated = effect != legion::core::LightingEffect::Off &&
                              effect != legion::core::LightingEffect::Static;
        rgb_speed_spin_->setEnabled(animated);
        rgb_brightness_combo_->setEnabled(effect != legion::core::LightingEffect::Off);
        rgb_direction_combo_->setEnabled(legion::core::effect_uses_direction(effect));
        const bool uses_zones = legion::core::effect_uses_zones(effect);
        for (auto* button : zone_buttons_) {
            button->setEnabled(uses_zones);
        }
        if (legion::core::is_hardware_effect(effect)) {
            rgb_kind_label_->setText("Native firmware effect (written to the ITE 8295 controller).");
        } else if (effect == legion::core::LightingEffect::Reactive) {
            rgb_kind_label_->setText("Software effect: click a zone on the preview to flash it. "
                                     "Physical typing reactivity is not enabled.");
        } else {
            rgb_kind_label_->setText("Software effect: legion-lightd animates it at login; this app animates it "
                                     "directly when the daemon is not running.");
        }
    }

    static void update_zone_button_style(QPushButton* button, const QColor& color) {
        button->setStyleSheet(
            QString("background-color: %1; color: %2; font-weight: bold; padding: 8px;")
                .arg(color.name(), color.lightness() > 128 ? "#111111" : "#ffffff"));
    }

    void choose_zone_color(int zone) {
        if (zone < 0 || zone > 3) return;
        const QColor chosen = QColorDialog::getColor(
            zone_colors_[static_cast<std::size_t>(zone)], this,
            QString("Select Zone %1 Color").arg(zone + 1));
        if (!chosen.isValid()) return;
        zone_colors_[static_cast<std::size_t>(zone)] = chosen;
        update_zone_button_style(zone_buttons_[static_cast<std::size_t>(zone)], chosen);
        keyboard_->set_zone_colors(zone_colors_);
        store_controls_into_profile(current_profile_);
        if (current_effect() == legion::core::LightingEffect::Reactive) {
            flash_zone(zone);
        }
    }

    void flash_zone(int zone) {
        if (zone < 0 || zone > 3) return;
        keyboard_->set_highlight_zone(zone);
        reactive_clear_timer_->start(220);
        if (direct_session_.is_open() && current_effect() == legion::core::LightingEffect::Reactive) {
            auto profile = current_ui_profile();
            auto frame = legion::core::sample_lighting_frame(
                legion::core::LightingEffect::Reactive, profile, 0.0);
            frame[static_cast<std::size_t>(zone)] = from_qcolor(zone_colors_[static_cast<std::size_t>(zone)]);
            std::string error;
            direct_session_.apply_zones(frame, profile.brightness, error);
        }
    }

    void update_preview() {
        const auto profile = current_ui_profile();
        const double seconds = static_cast<double>(preview_elapsed_.elapsed()) / 1000.0;
        const auto frame = legion::core::sample_lighting_frame(profile.effect, profile, seconds);
        std::array<QColor, 4> colors{};
        for (std::size_t i = 0; i < frame.size(); ++i) {
            colors[i] = to_qcolor(frame[i]);
        }
        keyboard_->set_preview_colors(colors);
    }

    void apply_lighting() {
        store_controls_into_profile(current_profile_);
        lighting_config_.active_profile = current_profile_;
        lighting_config_.apply_on_login = apply_on_login_cb_->isChecked();
        const auto& profile = lighting_config_.profiles[static_cast<std::size_t>(current_profile_)];

        std::string error;
        if (!legion::core::write_text_file_atomic(
                legion::core::default_lighting_config_path(),
                legion::core::serialize_lighting_config(lighting_config_), error)) {
            QMessageBox::warning(this, "Save Failed", QString::fromStdString(error));
            return;
        }

        if (notify_lightd()) {
            stop_live_animation();
            rgb_status_label_->setText(
                "Saved and sent to legion-lightd. The daemon applies the profile and keeps it after login.");
            return;
        }

        if (legion::core::is_software_effect(profile.effect)) {
            start_live_animation(profile);
            rgb_status_label_->setText(
                "Saved. legion-lightd is not running, so this app is animating the profile directly. "
                "Confirm the physical keyboard.");
            return;
        }

        stop_live_animation();
        if (!direct_session_.is_open() && !direct_session_.open(error)) {
            if (legion::core::is_hardware_effect(profile.effect)) {
                if (run_helper(rgb_helper_args(profile))) {
                    rgb_status_label_->setText(
                        "Saved and applied through legion-helper. Write was reported; confirm the physical keyboard.");
                } else {
                    rgb_status_label_->setText("Saved, but the keyboard could not be written.");
                }
                return;
            }
            QMessageBox::warning(
                this, "RGB Access Failed",
                QString::fromStdString(error) +
                    "\n\nInstall packaging/udev/60-legion-rgb.rules so your user can access /dev/hidraw.");
            rgb_status_label_->setText("Saved, but the keyboard could not be written.");
            return;
        }
        if (!direct_session_.apply(profile, error)) {
            direct_session_.reset();
            QMessageBox::warning(this, "RGB Write Failed", QString::fromStdString(error));
            rgb_status_label_->setText("Saved, but the write failed.");
            return;
        }
        rgb_status_label_->setText(
            "Saved and applied directly. Write was reported; confirm the physical keyboard.");
    }

    void start_live_animation(const legion::core::LightingProfile& profile) {
        stop_live_animation();
        std::string error;
        if (!direct_session_.is_open() && !direct_session_.open(error)) {
            rgb_status_label_->setText(
                QString("Saved, but RGB access failed: %1. Install packaging/udev/60-legion-rgb.rules "
                        "so this app can animate the effect.")
                    .arg(QString::fromStdString(error)));
            return;
        }
        live_profile_ = profile;
        live_elapsed_.restart();
        live_timer_->start(40);
    }

    void stop_live_animation() {
        if (live_timer_ != nullptr) {
            live_timer_->stop();
        }
    }

    void send_live_frame() {
        if (!direct_session_.is_open()) {
            stop_live_animation();
            return;
        }
        const double seconds = static_cast<double>(live_elapsed_.elapsed()) / 1000.0;
        const auto frame = legion::core::sample_lighting_frame(live_profile_.effect, live_profile_, seconds);
        std::string error;
        if (!direct_session_.apply_zones(frame, live_profile_.brightness, error)) {
            stop_live_animation();
            direct_session_.reset();
            rgb_status_label_->setText(QString("Live animation stopped: %1")
                                           .arg(QString::fromStdString(error)));
        }
    }

    QString find_helper_path() const {
        const QString installed = "/usr/local/bin/legion-helper";
        if (QFileInfo::exists(installed)) return installed;
        const QString app_dir = QCoreApplication::applicationDirPath();
        return QDir(app_dir).filePath("legion-helper");
    }

    QStringList rgb_helper_args(const legion::core::LightingProfile& profile) const {
        if (profile.effect == legion::core::LightingEffect::Off) {
            return {"rgb", "off"};
        }
        QStringList args{"rgb"};
        args << QString::fromStdString(legion::core::to_string(profile.effect))
             << QString::number(profile.speed)
             << QString::number(profile.brightness)
             << QString::fromStdString(legion::core::to_string(profile.direction));
        for (const auto& zone : profile.zones) {
            args << QString::number(zone.r) << QString::number(zone.g) << QString::number(zone.b);
        }
        return args;
    }

    bool run_helper(const QStringList& args) {
        QStringList full_args;
        full_args << find_helper_path() << args;
        QProcess process;
        process.start("pkexec", full_args);
        if (!process.waitForFinished(20000)) {
            QMessageBox::warning(this, "Timeout", "Polkit helper timed out.");
            return false;
        }
        if (process.exitCode() != 0) {
            const QString error = QString::fromUtf8(process.readAllStandardError()).trimmed();
            QMessageBox::warning(this, "Write Failed",
                                 error.isEmpty() ? "Authorization cancelled or helper failed." : error);
            return false;
        }
        status_label_->setText(QString::fromUtf8(process.readAllStandardOutput()).trimmed());
        refresh_data();
        return true;
    }

    void apply_control(const QString& key, const QString& value) {
        run_helper({"set", key, value});
    }

    void refresh_data() {
        auto snapshot = linux_provider_.collect_system_metadata();
        linux_provider_.collect(snapshot.capabilities, snapshot.readings);
        lenovo_provider_.collect(snapshot.capabilities, snapshot.readings);
        nvidia_provider_.collect(snapshot.capabilities, snapshot.readings);

        header_label_->setText(
            QString("%1 (%2)  |  BIOS: %3  |  Kernel: %4")
                .arg(QString::fromStdString(snapshot.product_version),
                     QString::fromStdString(snapshot.product_name),
                     QString::fromStdString(snapshot.bios_version),
                     QString::fromStdString(snapshot.kernel_version)));

        std::map<std::string, double> numbers;
        std::map<std::string, std::string> texts;
        double max_cpu_temp = -1.0;
        double max_nvme_temp = -1.0;
        double max_fan_rpm = -1.0;
        int fan_count = 0;

        sensors_table_->setRowCount(static_cast<int>(snapshot.readings.size()));
        for (int i = 0; i < static_cast<int>(snapshot.readings.size()); ++i) {
            const auto& reading = snapshot.readings[static_cast<std::size_t>(i)];
            if (reading.is_numeric) {
                numbers[reading.id] = reading.numeric_value;
                if (reading.unit == legion::core::SensorUnit::Celsius) {
                    if (reading.label.find("coretemp") != std::string::npos ||
                        reading.label.find("Package") != std::string::npos ||
                        reading.label.find("CPU") != std::string::npos) {
                        max_cpu_temp = std::max(max_cpu_temp, reading.numeric_value);
                    }
                    if (reading.label.find("nvme") != std::string::npos ||
                        reading.label.find("NVMe") != std::string::npos) {
                        max_nvme_temp = std::max(max_nvme_temp, reading.numeric_value);
                    }
                }
                if (reading.unit == legion::core::SensorUnit::Rpm) {
                    max_fan_rpm = std::max(max_fan_rpm, reading.numeric_value);
                    ++fan_count;
                }
            } else {
                texts[reading.id] = reading.text_value;
            }
            const QString value = reading.is_numeric
                ? QString::number(reading.numeric_value, 'f', 2)
                : QString::fromStdString(reading.text_value);
            sensors_table_->setItem(i, 0, new QTableWidgetItem(QString::fromStdString(reading.label)));
            sensors_table_->setItem(i, 1, new QTableWidgetItem(value));
            sensors_table_->setItem(i, 2, new QTableWidgetItem(
                QString::fromStdString(legion::core::to_string(reading.unit))));
            sensors_table_->setItem(i, 3, new QTableWidgetItem(
                QString::fromStdString(legion::core::to_string(reading.state))));
        }

        const bool has_usage = numbers.count("cpu.usage_percent") > 0;
        const bool has_freq = numbers.count("cpu.freq_mhz") > 0;
        const bool has_power = numbers.count("cpu.power_watts") > 0;
        const bool has_cpu_temp = max_cpu_temp >= 0.0;
        const double usage = has_usage ? numbers["cpu.usage_percent"] : 0.0;
        const double freq_ghz = has_freq ? numbers["cpu.freq_mhz"] / 1000.0 : 0.0;
        const double power = has_power ? numbers["cpu.power_watts"] : 0.0;
        const double max_freq_ghz = numbers.count("cpu.freq_max_mhz")
            ? std::max(1.0, numbers["cpu.freq_max_mhz"] / 1000.0)
            : 5.0;

        QVector<GaugeArc> cpu_arcs;
        cpu_arcs.append({"Usage", usage, 0.0, 100.0, QColor("#00d4ff"), has_usage, "%", 0});
        if (has_power) {
            cpu_arcs.append({"Power", power, 0.0, std::max(60.0, power * 1.3),
                             QColor("#ffb300"), true, "W", 0});
        } else if (has_freq) {
            cpu_arcs.append({"Freq", freq_ghz, 0.0, max_freq_ghz, QColor("#ffb300"), true, "GHz", 2});
        }
        cpu_arcs.append({"Temp", max_cpu_temp, 0.0, 105.0,
                         temp_color(max_cpu_temp, 80.0, 92.0), has_cpu_temp, "°C", 0});
        cpu_gauge_->set_arcs(cpu_arcs);
        QStringList cpu_parts;
        if (has_freq) cpu_parts << QString("%1 GHz").arg(freq_ghz, 0, 'f', 2);
        if (has_power) cpu_parts << QString("%1 W").arg(power, 0, 'f', 1);
        if (has_usage) cpu_parts << QString("%1%").arg(usage, 0, 'f', 0);
        cpu_gauge_->set_center(has_cpu_temp ? QString("%1 °C").arg(max_cpu_temp, 0, 'f', 0) : "--",
                               cpu_parts.join(" • "));

        const bool has_gpu = numbers.count("nvidia.gpu.temp") > 0;
        const double gpu_temp = has_gpu ? numbers["nvidia.gpu.temp"] : 0.0;
        const double gpu_util = numbers.count("nvidia.gpu.util") ? numbers["nvidia.gpu.util"] : 0.0;
        const double gpu_power = numbers.count("nvidia.gpu.power") ? numbers["nvidia.gpu.power"] : 0.0;
        const double gpu_clock = numbers.count("nvidia.gpu.clock_mhz") ? numbers["nvidia.gpu.clock_mhz"] : 0.0;
        const double gpu_fan = numbers.count("nvidia.gpu.fan_percent") ? numbers["nvidia.gpu.fan_percent"] : 0.0;
        QVector<GaugeArc> gpu_arcs;
        gpu_arcs.append({"Usage", gpu_util, 0.0, 100.0, QColor("#00d4ff"),
                         numbers.count("nvidia.gpu.util") > 0, "%", 0});
        gpu_arcs.append({"Power", gpu_power, 0.0, 140.0, QColor("#ffb300"),
                         numbers.count("nvidia.gpu.power") > 0, "W", 0});
        gpu_arcs.append({"Temp", gpu_temp, 0.0, 95.0, temp_color(gpu_temp, 75.0, 86.0),
                         has_gpu, "°C", 0});
        gpu_gauge_->set_arcs(gpu_arcs);
        QStringList gpu_parts;
        if (numbers.count("nvidia.gpu.clock_mhz")) gpu_parts << QString("%1 MHz").arg(gpu_clock, 0, 'f', 0);
        if (numbers.count("nvidia.gpu.power")) gpu_parts << QString("%1 W").arg(gpu_power, 0, 'f', 1);
        if (numbers.count("nvidia.gpu.fan_percent")) gpu_parts << QString("Fan %1%").arg(gpu_fan, 0, 'f', 0);
        gpu_gauge_->set_center(has_gpu ? QString("%1 °C").arg(gpu_temp, 0, 'f', 0) : "--",
                               gpu_parts.join(" • "));

        const bool has_storage = numbers.count("storage.root_used_percent") > 0;
        const double storage_used = has_storage ? numbers["storage.root_used_percent"] : 0.0;
        const bool has_nvme = max_nvme_temp >= 0.0;
        QVector<GaugeArc> nvme_arcs;
        nvme_arcs.append({"Used", storage_used, 0.0, 100.0, QColor("#7c4dff"), has_storage, "%", 0});
        nvme_arcs.append({"Temp", max_nvme_temp, 0.0, 85.0,
                          temp_color(max_nvme_temp, 65.0, 75.0), has_nvme, "°C", 0});
        nvme_gauge_->set_arcs(nvme_arcs);
        QString nvme_secondary;
        if (has_storage) {
            nvme_secondary = QString("used %1% of %2 GB")
                                 .arg(storage_used, 0, 'f', 0)
                                 .arg(numbers.count("storage.root_total_gb")
                                          ? numbers["storage.root_total_gb"] : 0.0, 0, 'f', 0);
        }
        nvme_gauge_->set_center(has_nvme ? QString("%1 °C").arg(max_nvme_temp, 0, 'f', 0) : "--",
                                nvme_secondary);

        const bool has_charge = numbers.count("battery.capacity") > 0;
        const bool has_health = numbers.count("battery.health_percent") > 0;
        const bool has_battery_power = numbers.count("battery.power_watts") > 0;
        const double charge = has_charge ? numbers["battery.capacity"] : 0.0;
        const double health = has_health ? numbers["battery.health_percent"] : 0.0;
        const double battery_power = has_battery_power ? numbers["battery.power_watts"] : 0.0;
        QVector<GaugeArc> battery_arcs;
        battery_arcs.append({"Charge", charge, 0.0, 100.0, QColor("#00e676"), has_charge, "%", 0});
        battery_arcs.append({"Health", health, 0.0, 100.0, QColor("#00d4ff"), has_health, "%", 0});
        battery_arcs.append({"Power", battery_power, 0.0, 100.0, QColor("#ffb300"),
                             has_battery_power, "W", 1});
        battery_gauge_->set_arcs(battery_arcs);
        QStringList battery_parts;
        if (has_health) battery_parts << QString("Health %1%").arg(health, 0, 'f', 0);
        if (has_battery_power) battery_parts << QString("%1 W").arg(battery_power, 0, 'f', 1);
        battery_gauge_->set_center(has_charge ? QString("%1%").arg(charge, 0, 'f', 0) : "--",
                                   battery_parts.join(" • "));

        const bool has_memory = numbers.count("memory.total") > 0 && numbers["memory.total"] > 0.0;
        double memory_usage = 0.0;
        double memory_used_gb = 0.0;
        double memory_total_gb = 0.0;
        if (has_memory) {
            const double total_mb = numbers["memory.total"];
            const double available_mb = numbers.count("memory.available") ? numbers["memory.available"] : 0.0;
            memory_used_gb = (total_mb - available_mb) / 1024.0;
            memory_total_gb = total_mb / 1024.0;
            memory_usage = std::clamp(100.0 * (1.0 - available_mb / total_mb), 0.0, 100.0);
        }
        QVector<GaugeArc> memory_arcs;
        memory_arcs.append({"Usage", memory_usage, 0.0, 100.0, QColor("#00d4ff"), has_memory, "%", 0});
        memory_arcs.append({"Used", memory_used_gb, 0.0, std::max(1.0, memory_total_gb),
                            QColor("#7c4dff"), has_memory, "GB", 1});
        memory_gauge_->set_arcs(memory_arcs);
        memory_gauge_->set_center(
            has_memory ? QString("%1%").arg(memory_usage, 0, 'f', 0) : "--",
            has_memory ? QString("%1 / %2 GB").arg(memory_used_gb, 0, 'f', 1)
                             .arg(memory_total_gb, 0, 'f', 1)
                       : QString());

        const bool has_fans = max_fan_rpm >= 0.0;
        QVector<GaugeArc> fan_arcs;
        fan_arcs.append({"Max RPM", max_fan_rpm, 0.0, std::max(6000.0, max_fan_rpm * 1.2),
                         QColor("#00d4ff"), has_fans, "RPM", 0});
        fan_arcs.append({"Sensors", static_cast<double>(fan_count), 0.0, 10.0,
                         QColor("#ffb300"), fan_count > 0, "", 0});
        fan_gauge_->set_arcs(fan_arcs);
        fan_gauge_->set_center(has_fans ? QString("%1").arg(max_fan_rpm, 0, 'f', 0) : "--",
                               has_fans ? QString("%1 fan sensors").arg(fan_count) : QString());

        if (texts.count("platform_profile")) {
            profile_combo_->setCurrentText(QString::fromStdString(texts["platform_profile"]));
        }
        auto set_flag_label = [&](const std::string& id, QLabel* label) {
            if (texts.count(id)) {
                label->setText(texts[id] == "1" ? "Enabled (1)" : "Disabled (0)");
            } else {
                label->setText("Unavailable");
            }
        };
        set_flag_label("lenovo.conservation_mode", conservation_lbl_);
        set_flag_label("lenovo.fn_lock", fn_lock_lbl_);
        set_flag_label("lenovo.usb_charging", usb_charging_lbl_);
        set_flag_label("lenovo.camera_power", camera_lbl_);

        caps_table_->setRowCount(static_cast<int>(snapshot.capabilities.size()));
        for (int i = 0; i < static_cast<int>(snapshot.capabilities.size()); ++i) {
            const auto& capability = snapshot.capabilities[static_cast<std::size_t>(i)];
            caps_table_->setItem(i, 0, new QTableWidgetItem(QString::fromStdString(capability.name)));
            caps_table_->setItem(i, 1, new QTableWidgetItem(QString::fromStdString(capability.backend)));
            caps_table_->setItem(i, 2, new QTableWidgetItem(
                QString::fromStdString(legion::core::to_string(capability.state))));
            caps_table_->setItem(i, 3, new QTableWidgetItem(QString::fromStdString(capability.notes)));
        }

        status_label_->setText(QString("Updated %1 • read-only telemetry")
                                   .arg(QString::fromStdString(legion::core::current_timestamp_iso8601())));

        if (auto_refresh_cb_->isChecked()) {
            timer_->start(refresh_interval_ms_);
        }
    }

    std::unique_ptr<legion::core::ISysfsReader> sysfs_;
    std::unique_ptr<legion::core::IProcessRunner> runner_;
    legion::backends::linux_sysfs::LinuxTelemetryProvider linux_provider_;
    legion::backends::lenovo::LenovoTelemetryProvider lenovo_provider_;
    legion::backends::nvidia::NvidiaTelemetryProvider nvidia_provider_;
    legion::backends::lenovo::RgbSession direct_session_;

    QLabel* header_label_ = nullptr;
    QLabel* status_label_ = nullptr;
    QCheckBox* auto_refresh_cb_ = nullptr;
    QDoubleSpinBox* interval_spin_ = nullptr;
    int refresh_interval_ms_ = 1000;

    MultiArcGauge* cpu_gauge_ = nullptr;
    MultiArcGauge* gpu_gauge_ = nullptr;
    MultiArcGauge* nvme_gauge_ = nullptr;
    MultiArcGauge* battery_gauge_ = nullptr;
    MultiArcGauge* memory_gauge_ = nullptr;
    MultiArcGauge* fan_gauge_ = nullptr;

    QTableWidget* sensors_table_ = nullptr;
    QTableWidget* caps_table_ = nullptr;
    QComboBox* profile_combo_ = nullptr;
    QLabel* conservation_lbl_ = nullptr;
    QLabel* fn_lock_lbl_ = nullptr;
    QLabel* usb_charging_lbl_ = nullptr;
    QLabel* camera_lbl_ = nullptr;

    legion::core::LightingConfig lighting_config_;
    int current_profile_ = 0;
    std::array<QColor, 4> zone_colors_ = {QColor("#00d4ff"), QColor("#00d4ff"),
                                          QColor("#00d4ff"), QColor("#00d4ff")};
    std::array<QPushButton*, 4> zone_buttons_ = {nullptr, nullptr, nullptr, nullptr};
    KeyboardWidget* keyboard_ = nullptr;
    QComboBox* rgb_profile_combo_ = nullptr;
    QCheckBox* apply_on_login_cb_ = nullptr;
    QComboBox* rgb_mode_combo_ = nullptr;
    QSpinBox* rgb_speed_spin_ = nullptr;
    QComboBox* rgb_brightness_combo_ = nullptr;
    QComboBox* rgb_direction_combo_ = nullptr;
    QLabel* rgb_kind_label_ = nullptr;
    QLabel* rgb_status_label_ = nullptr;

    QTimer* timer_ = nullptr;
    QTimer* preview_timer_ = nullptr;
    QTimer* live_timer_ = nullptr;
    QTimer* reactive_clear_timer_ = nullptr;
    QElapsedTimer preview_elapsed_;
    QElapsedTimer live_elapsed_;
    legion::core::LightingProfile live_profile_;
};

int main(int argc, char* argv[]) {
    if (geteuid() == 0) {
        std::cerr << "Refusing to run GUI as root. Run as a normal user; writes use udev/Polkit.\n";
        return 1;
    }
    QApplication app(argc, argv);
    LegionMainWindow window;
    window.show();
    return app.exec();
}
