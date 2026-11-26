#include "legion/backends/lenovo/lenovo_rgb.hpp"
#include "legion/backends/lenovo/lenovo_keymap.hpp"
#include "legion/backends/lenovo/lenovo_telemetry.hpp"
#include "legion/backends/linux/keyboard_input.hpp"
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
#include <QIcon>
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
#include <QStackedWidget>
#include <QStringList>
#include <QTabBar>
#include <QTableWidget>
#include <QTimer>
#include <QVBoxLayout>

#include <vector>
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

double celsius_to_fahrenheit(double celsius) {
    return celsius * 9.0 / 5.0 + 32.0;
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

bool send_lightd_command(const std::string& command) {
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
    const std::string message = command + "\n";
    if (::write(fd, message.data(), message.size()) < 0) {
        ::close(fd);
        return false;
    }
    char reply[32] = {0};
    const ssize_t count = ::read(fd, reply, sizeof(reply) - 1);
    ::close(fd);
    return count > 0 && std::string(reply, static_cast<std::size_t>(count)).rfind("ok", 0) == 0;
}

bool notify_lightd() {
    return send_lightd_command("reload");
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

        const int legend_height = arcs_.isEmpty() ? 0 : 30;
        const int title_height = 28;
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
            font.setPointSize(9);
            font.setBold(false);
            painter.setFont(font);
            const QFontMetrics metrics(font);
            const int dot = 8;
            const int item_gap = 16;
            QVector<int> widths;
            int total = 0;
            for (const auto& arc : arcs_) {
                const QString value_text = arc.valid
                    ? format_value(arc.value, arc.decimals) + " " + arc.unit
                    : QString("--");
                widths.append(dot + 6 + metrics.horizontalAdvance(arc.label + " " + value_text));
                total += widths.last();
            }
            total += item_gap * (arcs_.size() - 1);
            int x = std::max(6, (width() - total) / 2);
            for (int i = 0; i < arcs_.size(); ++i) {
                const auto& arc = arcs_[i];
                painter.setPen(Qt::NoPen);
                painter.setBrush(arc.color);
                painter.drawEllipse(QPointF(x + dot / 2.0, legend_y + 9), 4, 4);
                const QString value_text = arc.valid
                    ? format_value(arc.value, arc.decimals) + " " + arc.unit
                    : QString("--");
                painter.setPen(QColor("#cbd2d9"));
                painter.drawText(QRect(x + dot + 6, legend_y, widths[i], 18),
                                 Qt::AlignVCenter | Qt::AlignLeft, arc.label + " " + value_text);
                x += widths[i] + item_gap;
            }
        }

        font.setPointSize(16);
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

        static const std::array<std::vector<double>, 5> key_rows = {{
            {1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 2},
            {1.5, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 2.5},
            {1.8, 1, 1, 1, 1, 1, 1, 1, 1, 1, 3.2},
            {2.3, 1, 1, 1, 1, 1, 1, 1, 1, 3.7},
            {1.2, 1, 1, 1, 5.8, 1, 1, 1, 1, 1}
        }};
        const double pad = 12.0;
        const double gap = 4.0;
        const double key_height =
            (body.height() - pad * 2 - gap * (static_cast<double>(key_rows.size()) - 1)) /
            static_cast<double>(key_rows.size());
        const double unit_width = (body.width() - pad * 2) / 15.0;
        const auto& colors = has_preview_ ? display_colors_ : base_colors_;

        for (std::size_t row = 0; row < key_rows.size(); ++row) {
            const double y = body.top() + pad + static_cast<double>(row) * (key_height + gap);
            double x = body.left() + pad;
            for (double unit : key_rows[row]) {
                const double key_width = unit * unit_width;
                const QRectF cap(x + gap / 2.0, y, std::max(1.0, key_width - gap), key_height);
                const int zone = std::clamp(
                    static_cast<int>(((cap.center().x() - body.left()) / body.width()) * 4.0), 0, 3);
                QColor color = colors[static_cast<std::size_t>(zone)].darker(210);
                if (highlight_zone_ == zone) {
                    color = colors[static_cast<std::size_t>(zone)].lighter(150);
                }
                QPainterPath cap_path;
                cap_path.addRoundedRect(cap, 3, 3);
                painter.fillPath(cap_path, color);
                painter.setPen(QPen(QColor("#0b0d12").lighter(125), 1));
                painter.drawPath(cap_path);
                painter.setPen(QPen(QColor(255, 255, 255, 22), 1));
                painter.drawLine(QPointF(cap.left() + 2, cap.top() + 1),
                                 QPointF(cap.right() - 2, cap.top() + 1));
                x += key_width;
            }
        }

        const QFont font = painter.font();
        QFont small = font;
        small.setPointSize(8);
        painter.setFont(small);
        painter.setPen(QColor("#9aa5b1"));
        const double zone_width = body.width() / 4.0;
        for (int zone = 0; zone < 4; ++zone) {
            painter.drawText(QRectF(body.left() + zone * zone_width, height() - 18, zone_width, 16),
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
        setWindowIcon(QIcon(":/logo.png"));
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
        interval_spin_->setToolTip("Telemetry refresh interval. 0.1 s is supported; nvidia-smi may make the effective rate slower.");
        temp_unit_combo_ = new QComboBox(this);
        temp_unit_combo_->addItems({"°C", "°F"});
        temp_unit_combo_->setToolTip("Temperature unit");
        status_label_ = new QLabel("Ready (read-only telemetry)", this);
        status_label_->setStyleSheet("color: #9aa5b1;");
        toolbar->addWidget(refresh_btn);
        toolbar->addWidget(auto_refresh_cb_);
        toolbar->addWidget(new QLabel("Interval:", this));
        toolbar->addWidget(interval_spin_);
        toolbar->addWidget(new QLabel("Temp:", this));
        toolbar->addWidget(temp_unit_combo_);
        toolbar->addStretch();
        toolbar->addWidget(status_label_);
        root_layout->addLayout(toolbar);

        auto* tabs_bar = new QTabBar(this);
        tabs_bar->setExpanding(true);
        auto* pages = new QStackedWidget(this);
        connect(tabs_bar, &QTabBar::currentChanged, pages, &QStackedWidget::setCurrentIndex);

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
        tabs_bar->addTab("Dashboard");
        pages->addWidget(dash_tab);

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
        tabs_bar->addTab("Controls");
        pages->addWidget(controls_tab);

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
        keyboard_->set_zone_click_handler([this](int zone) { select_zone(zone); });
        keyboard_column->addWidget(keyboard_);
        auto* zone_row = new QHBoxLayout();
        for (int zone = 0; zone < 4; ++zone) {
            zone_buttons_[static_cast<std::size_t>(zone)] = new QPushButton(QString("Zone %1").arg(zone + 1), this);
            zone_row->addWidget(zone_buttons_[static_cast<std::size_t>(zone)]);
            connect(zone_buttons_[static_cast<std::size_t>(zone)], &QPushButton::clicked, this,
                    [this, zone]() { select_zone(zone); });
        }
        keyboard_column->addLayout(zone_row);

        auto* palette_row = new QHBoxLayout();
        palette_row->addWidget(new QLabel("Presets:", this));
        const std::array<const char*, 12> preset_colors = {
            "#ff0000", "#ff7f00", "#ffd400", "#00e676", "#00d4ff", "#0066ff",
            "#8a2be2", "#ff2d95", "#ffffff", "#ffb3c1", "#00ffd5", "#7c4dff"};
        for (const char* hex : preset_colors) {
            auto* swatch = new QPushButton(this);
            swatch->setFixedSize(26, 26);
            swatch->setToolTip(hex);
            swatch->setStyleSheet(
                QString("background-color: %1; border: 1px solid #2b3140; border-radius: 5px;").arg(hex));
            const QColor color(hex);
            connect(swatch, &QPushButton::clicked, this, [this, color]() { apply_palette_color(color); });
            palette_row->addWidget(swatch);
        }
        auto* custom_btn = new QPushButton("Custom…", this);
        connect(custom_btn, &QPushButton::clicked, this, [this]() { open_custom_color(); });
        palette_row->addWidget(custom_btn);
        palette_row->addStretch();
        keyboard_column->addLayout(palette_row);

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

        reactive_color_combo_ = new QComboBox(this);
        reactive_color_combo_->addItems({"Preset colors", "Random colors"});
        rgb_form->addRow("Reactive color:", reactive_color_combo_);

        realtime_cb_ = new QCheckBox("Realtime color preview", this);
        realtime_cb_->setToolTip("Write static zone colors to the keyboard immediately while editing.");
        rgb_form->addRow("", realtime_cb_);

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
            "HID protocol; this app keeps four equivalent userspace profiles instead. Reactive needs read access "
            "to /dev/input (install the keyboard udev rule or join the input group) and maps each key to the zone "
            "under it. Always confirm the physical keyboard changed.",
            this);
        rgb_note->setWordWrap(true);
        rgb_note->setStyleSheet("color: #9aa5b1; padding: 4px;");
        rgb_root->addWidget(rgb_note);

        rgb_layout->addWidget(rgb_group);
        rgb_layout->addStretch();
        tabs_bar->addTab("4-Zone RGB");
        pages->addWidget(rgb_tab);

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
        tabs_bar->addTab("Sensors");
        pages->addWidget(raw_tab);

        root_layout->addWidget(tabs_bar);
        root_layout->addWidget(pages, 1);
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
        connect(temp_unit_combo_, &QComboBox::currentIndexChanged, this, [this](int index) {
            use_fahrenheit_ = index == 1;
            refresh_data();
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
        connect(reactive_color_combo_, &QComboBox::currentIndexChanged, this, [this](int) {
            store_controls_into_profile(current_profile_);
            reactive_state_ = legion::core::ReactiveFlashState{};
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
        animation_clock_.start();
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
            const QSignalBlocker block_reactive(reactive_color_combo_);
            const int mode_index = rgb_mode_combo_->findData(static_cast<int>(profile.effect));
            rgb_mode_combo_->setCurrentIndex(mode_index >= 0 ? mode_index : 1);
            rgb_speed_spin_->setValue(std::clamp(profile.speed, 1, 4));
            rgb_brightness_combo_->setCurrentIndex(profile.brightness == 1 ? 0 : 1);
            rgb_direction_combo_->setCurrentIndex(
                profile.direction == legion::core::WaveDirection::LeftToRight ? 0 : 1);
            reactive_color_combo_->setCurrentIndex(
                profile.reactive_mode == legion::core::ReactiveMode::Random ? 1 : 0);
        }
        for (std::size_t zone = 0; zone < profile.zones.size(); ++zone) {
            zone_colors_[zone] = to_qcolor(profile.zones[zone]);
            update_zone_button_style(static_cast<int>(zone));
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
        profile.reactive_mode = reactive_color_combo_->currentIndex() == 1
            ? legion::core::ReactiveMode::Random
            : legion::core::ReactiveMode::Preset;
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
        profile.reactive_mode = reactive_color_combo_->currentIndex() == 1
            ? legion::core::ReactiveMode::Random
            : legion::core::ReactiveMode::Preset;
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
        reactive_color_combo_->setEnabled(effect == legion::core::LightingEffect::Reactive);
        realtime_cb_->setEnabled(effect == legion::core::LightingEffect::Static);
        const bool uses_zones = legion::core::effect_uses_zones(effect);
        for (auto* button : zone_buttons_) {
            button->setEnabled(uses_zones);
        }
        if (legion::core::is_hardware_effect(effect)) {
            rgb_kind_label_->setText("Native firmware effect (written to the ITE 8295 controller).");
        } else if (effect == legion::core::LightingEffect::Reactive) {
            rgb_kind_label_->setText("Software effect: pressing a key flashes the zone that contains it. "
                                     "Preset colors use the zone colors; Random picks a new color per press.");
        } else {
            rgb_kind_label_->setText("Software effect: legion-lightd animates it at login; this app animates it "
                                     "directly when the daemon is not running.");
        }
    }

    void update_zone_button_style(int zone) {
        auto* button = zone_buttons_[static_cast<std::size_t>(zone)];
        const QColor color = zone_colors_[static_cast<std::size_t>(zone)];
        const bool selected = zone == selected_zone_;
        button->setStyleSheet(
            QString("background-color: %1; color: %2; font-weight: bold; padding: 8px; border: %3;")
                .arg(color.name(), color.lightness() > 128 ? "#111111" : "#ffffff",
                     selected ? "2px solid #00d4ff" : "1px solid #2b3140"));
    }

    void select_zone(int zone) {
        if (zone < 0 || zone > 3) return;
        selected_zone_ = zone;
        for (int i = 0; i < 4; ++i) update_zone_button_style(i);
        keyboard_->set_highlight_zone(zone);
        if (current_effect() == legion::core::LightingEffect::Reactive) {
            flash_zone(zone);
        }
    }

    void apply_palette_color(const QColor& color) {
        apply_zone_color(selected_zone_, color, true);
    }

    void open_custom_color() {
        const int zone = selected_zone_;
        QColorDialog dialog(zone_colors_[static_cast<std::size_t>(zone)], this);
        dialog.setWindowTitle(QString("Zone %1 Color").arg(zone + 1));
        dialog.setOption(QColorDialog::DontUseNativeDialog, true);
        connect(&dialog, &QColorDialog::currentColorChanged, this, [this, zone](const QColor& color) {
            if (color.isValid()) apply_zone_color(zone, color, true);
        });
        dialog.exec();
    }

    void apply_zone_color(int zone, const QColor& color, bool live) {
        if (zone < 0 || zone > 3 || !color.isValid()) return;
        zone_colors_[static_cast<std::size_t>(zone)] = color;
        update_zone_button_style(zone);
        keyboard_->set_zone_colors(zone_colors_);
        store_controls_into_profile(current_profile_);
        if (live && realtime_cb_ != nullptr && realtime_cb_->isChecked() &&
            current_effect() == legion::core::LightingEffect::Static) {
            apply_live_static();
        }
    }

    void apply_live_static() {
        store_controls_into_profile(current_profile_);
        auto profile = current_ui_profile();
        profile.effect = legion::core::LightingEffect::Static;
        if (send_lightd_preview(profile)) {
            rgb_status_label_->setText("Realtime color sent to legion-lightd.");
            return;
        }
        std::string error;
        if (!direct_session_.is_open() && !direct_session_.open(error)) {
            rgb_status_label_->setText(
                "Realtime preview needs /dev/hidraw access: install packaging/udev/60-legion-rgb.rules.");
            return;
        }
        if (!direct_session_.apply_zones(profile.zones, profile.brightness, error)) {
            direct_session_.reset();
            rgb_status_label_->setText(
                QString("Realtime write failed: %1").arg(QString::fromStdString(error)));
            return;
        }
        rgb_status_label_->setText("Realtime color written directly.");
    }

    void flash_zone(int zone) {
        if (zone < 0 || zone > 3) return;
        keyboard_->set_highlight_zone(zone);
        reactive_clear_timer_->start(260);
        const auto profile = current_ui_profile();
        const double now = static_cast<double>(animation_clock_.elapsed()) / 1000.0;
        const auto seed = static_cast<std::uint32_t>(now * 1000.0) +
                          static_cast<std::uint32_t>(zone + 1) * 40503U;
        legion::core::reactive_flash(reactive_state_, zone,
                                     legion::core::reactive_color_for(profile, zone, seed), now);
    }

    bool ensure_keyboard_input() {
        if (keyboard_input_.is_open()) return true;
        const qint64 now = animation_clock_.elapsed();
        if (now - last_keyboard_attempt_ms_ < 2000) return false;
        last_keyboard_attempt_ms_ = now;
        std::string error;
        return keyboard_input_.open(error);
    }

    void poll_reactive_input() {
        if (current_effect() != legion::core::LightingEffect::Reactive) return;
        if (!ensure_keyboard_input()) return;
        const auto profile = current_ui_profile();
        const double now = static_cast<double>(animation_clock_.elapsed()) / 1000.0;
        keyboard_input_.poll([&](int code, bool pressed) {
            if (!pressed) return;
            const int zone = legion::backends::lenovo::legion_key_to_zone(code);
            if (zone < 0) return;
            const auto seed = static_cast<std::uint32_t>(now * 1000.0) +
                              static_cast<std::uint32_t>(code) * 2654435761U;
            legion::core::reactive_flash(reactive_state_, zone,
                                         legion::core::reactive_color_for(profile, zone, seed), now);
            keyboard_->set_highlight_zone(zone);
            reactive_clear_timer_->start(200);
        });
    }

    std::array<legion::core::Rgb, 4> sample_current(double seconds) const {
        const auto profile = current_ui_profile();
        if (profile.effect == legion::core::LightingEffect::Reactive) {
            return legion::core::reactive_render(reactive_state_, profile);
        }
        return legion::core::sample_lighting_frame(profile.effect, profile, seconds);
    }

    void update_preview() {
        const double seconds = static_cast<double>(animation_clock_.elapsed()) / 1000.0;
        if (current_effect() == legion::core::LightingEffect::Reactive) {
            poll_reactive_input();
            legion::core::reactive_decay(reactive_state_, seconds);
        }
        const auto frame = sample_current(seconds);
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
        reactive_state_ = legion::core::ReactiveFlashState{};
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
        const double seconds = static_cast<double>(animation_clock_.elapsed()) / 1000.0;
        std::array<legion::core::Rgb, 4> frame{};
        if (live_profile_.effect == legion::core::LightingEffect::Reactive) {
            frame = legion::core::reactive_render(reactive_state_, live_profile_);
        } else {
            frame = legion::core::sample_lighting_frame(live_profile_.effect, live_profile_, seconds);
        }
        std::string error;
        if (!direct_session_.apply_zones(frame, live_profile_.brightness, error)) {
            stop_live_animation();
            direct_session_.reset();
            rgb_status_label_->setText(QString("Live animation stopped: %1")
                                           .arg(QString::fromStdString(error)));
        }
    }

    bool send_lightd_preview(const legion::core::LightingProfile& profile) {
        std::string command = "preview";
        for (const auto& zone : profile.zones) {
            command += " " + std::to_string(zone.r) + " " + std::to_string(zone.g) + " " +
                       std::to_string(zone.b);
        }
        command += " " + std::to_string(profile.brightness);
        return send_lightd_command(command);
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

    QString temp_unit() const { return use_fahrenheit_ ? "°F" : "°C"; }

    double display_temp(double celsius) const {
        return use_fahrenheit_ ? celsius_to_fahrenheit(celsius) : celsius;
    }

    GaugeArc make_temp_arc(const QString& label, double celsius, double max_celsius,
                           double warn, double crit, bool valid) const {
        const double lo = display_temp(0.0);
        const double hi = display_temp(max_celsius);
        return {label, display_temp(celsius), lo, hi, temp_color(celsius, warn, crit), valid, temp_unit(), 0};
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
            const bool is_celsius = reading.is_numeric &&
                                    reading.unit == legion::core::SensorUnit::Celsius;
            const double shown_value = (is_celsius && use_fahrenheit_)
                ? celsius_to_fahrenheit(reading.numeric_value)
                : reading.numeric_value;
            QString unit_text = QString::fromStdString(legion::core::to_string(reading.unit));
            if (is_celsius && use_fahrenheit_) unit_text = "F";
            const QString value = reading.is_numeric
                ? QString::number(shown_value, 'f', 2)
                : QString::fromStdString(reading.text_value);
            sensors_table_->setItem(i, 0, new QTableWidgetItem(QString::fromStdString(reading.label)));
            sensors_table_->setItem(i, 1, new QTableWidgetItem(value));
            sensors_table_->setItem(i, 2, new QTableWidgetItem(unit_text));
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
        cpu_arcs.append(make_temp_arc("Temp", max_cpu_temp, 105.0, 80.0, 92.0, has_cpu_temp));
        cpu_gauge_->set_arcs(cpu_arcs);
        QStringList cpu_parts;
        if (has_freq) cpu_parts << QString("%1 GHz").arg(freq_ghz, 0, 'f', 2);
        if (has_power) cpu_parts << QString("%1 W").arg(power, 0, 'f', 1);
        if (has_usage) cpu_parts << QString("%1%").arg(usage, 0, 'f', 0);
        cpu_gauge_->set_center(
            has_cpu_temp ? QString("%1 %2").arg(display_temp(max_cpu_temp), 0, 'f', 0).arg(temp_unit())
                         : "--",
            cpu_parts.join(" • "));

        const bool has_gpu = numbers.count("nvidia.gpu.temp") > 0;
        const double gpu_temp = has_gpu ? numbers["nvidia.gpu.temp"] : 0.0;
        const double gpu_util = numbers.count("nvidia.gpu.util") ? numbers["nvidia.gpu.util"] : 0.0;
        const double gpu_power = numbers.count("nvidia.gpu.power") ? numbers["nvidia.gpu.power"] : 0.0;
        const double gpu_clock = numbers.count("nvidia.gpu.clock_mhz") ? numbers["nvidia.gpu.clock_mhz"] : 0.0;
        const double gpu_fan = numbers.count("nvidia.gpu.fan_percent") ? numbers["nvidia.gpu.fan_percent"] : 0.0;
        const double gpu_power_limit = numbers.count("nvidia.gpu.power_limit")
            ? std::max(1.0, numbers["nvidia.gpu.power_limit"]) : 140.0;
        QVector<GaugeArc> gpu_arcs;
        gpu_arcs.append({"Usage", gpu_util, 0.0, 100.0, QColor("#00d4ff"),
                         numbers.count("nvidia.gpu.util") > 0, "%", 0});
        gpu_arcs.append({"Power", gpu_power, 0.0, gpu_power_limit, QColor("#ffb300"),
                         numbers.count("nvidia.gpu.power") > 0, "W", 0});
        gpu_arcs.append(make_temp_arc("Temp", gpu_temp, 95.0, 75.0, 86.0, has_gpu));
        gpu_gauge_->set_arcs(gpu_arcs);
        QStringList gpu_parts;
        if (numbers.count("nvidia.gpu.clock_mhz")) gpu_parts << QString("%1 MHz").arg(gpu_clock, 0, 'f', 0);
        if (numbers.count("nvidia.gpu.power")) {
            gpu_parts << QString("%1 / %2 W").arg(gpu_power, 0, 'f', 1).arg(gpu_power_limit, 0, 'f', 0);
        }
        if (numbers.count("nvidia.gpu.fan_percent")) gpu_parts << QString("Fan %1%").arg(gpu_fan, 0, 'f', 0);
        gpu_gauge_->set_center(
            has_gpu ? QString("%1 %2").arg(display_temp(gpu_temp), 0, 'f', 0).arg(temp_unit()) : "--",
            gpu_parts.join(" • "));

        const bool has_storage = numbers.count("storage.root_used_percent") > 0;
        const double storage_used = has_storage ? numbers["storage.root_used_percent"] : 0.0;
        const bool has_nvme = max_nvme_temp >= 0.0;
        QVector<GaugeArc> nvme_arcs;
        nvme_arcs.append({"Used", storage_used, 0.0, 100.0, QColor("#7c4dff"), has_storage, "%", 0});
        nvme_arcs.append(make_temp_arc("Temp", max_nvme_temp, 85.0, 65.0, 75.0, has_nvme));
        nvme_gauge_->set_arcs(nvme_arcs);
        QString nvme_secondary;
        if (has_storage) {
            nvme_secondary = QString("used %1% of %2 GB")
                                 .arg(storage_used, 0, 'f', 0)
                                 .arg(numbers.count("storage.root_total_gb")
                                          ? numbers["storage.root_total_gb"] : 0.0, 0, 'f', 0);
        }
        nvme_gauge_->set_center(
            has_nvme ? QString("%1 %2").arg(display_temp(max_nvme_temp), 0, 'f', 0).arg(temp_unit()) : "--",
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
    QComboBox* temp_unit_combo_ = nullptr;
    bool use_fahrenheit_ = false;
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
    int selected_zone_ = 0;
    QComboBox* rgb_profile_combo_ = nullptr;
    QCheckBox* apply_on_login_cb_ = nullptr;
    QComboBox* rgb_mode_combo_ = nullptr;
    QSpinBox* rgb_speed_spin_ = nullptr;
    QComboBox* rgb_brightness_combo_ = nullptr;
    QComboBox* rgb_direction_combo_ = nullptr;
    QComboBox* reactive_color_combo_ = nullptr;
    QCheckBox* realtime_cb_ = nullptr;
    QLabel* rgb_kind_label_ = nullptr;
    QLabel* rgb_status_label_ = nullptr;

    QTimer* timer_ = nullptr;
    QTimer* preview_timer_ = nullptr;
    QTimer* live_timer_ = nullptr;
    QTimer* reactive_clear_timer_ = nullptr;
    QElapsedTimer animation_clock_;
    qint64 last_keyboard_attempt_ms_ = 0;
    legion::core::ReactiveFlashState reactive_state_;
    legion::backends::linux_input::KeyboardInput keyboard_input_;
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
    if (qEnvironmentVariableIsSet("LEGION_SCREENSHOT_PATH")) {
        QTimer::singleShot(1500, &window, [&window]() {
            if (auto* tabs = window.findChild<QTabBar*>()) {
                if (qEnvironmentVariableIsSet("LEGION_SCREENSHOT_TAB")) {
                    tabs->setCurrentIndex(qEnvironmentVariableIntValue("LEGION_SCREENSHOT_TAB"));
                }
            }
            const QString path = qEnvironmentVariable("LEGION_SCREENSHOT_PATH");
            if (!path.isEmpty()) {
                window.grab().save(path);
            }
            QCoreApplication::quit();
        });
    }
    return app.exec();
}
