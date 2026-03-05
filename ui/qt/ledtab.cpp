#include "ledtab.h"

#include <QCheckBox>
#include <QColorDialog>
#include <QComboBox>
#include <QFormLayout>
#include <QFutureWatcher>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QScrollArea>
#include <QToolButton>
#include <QVBoxLayout>
#include <QtConcurrent/QtConcurrent>

static QString colorStyle(uint8_t r, uint8_t g, uint8_t b)
{
    return QString("background-color: rgb(%1,%2,%3); border: 1px solid #888;")
               .arg(r).arg(g).arg(b);
}

LedTab::LedTab(razerd_t *r, const std::string &idstr, QWidget *parent)
    : QWidget(parent), m_r(r), m_idstr(idstr)
{
    auto *outerVbox = new QVBoxLayout(this);
    outerVbox->setContentsMargins(12, 12, 12, 12);
    outerVbox->setSpacing(8);

    /* Scrollable area for LED list */
    auto *scroll = new QScrollArea(this);
    scroll->setWidgetResizable(true);
    auto *inner  = new QWidget;
    auto *grid   = new QGridLayout(inner);
    grid->setSpacing(6);
    grid->setContentsMargins(4, 4, 4, 4);

    /* Header row */
    grid->addWidget(new QLabel("<b>LED</b>",   inner), 0, 0);
    grid->addWidget(new QLabel("<b>On</b>",    inner), 0, 1);
    grid->addWidget(new QLabel("<b>Color</b>", inner), 0, 2);
    grid->addWidget(new QLabel("<b>Mode</b>",  inner), 0, 3);

    static const char *mode_names[] = {
        "Static", "Spectrum", "Breathing", "Wave", "Reaction"
    };

    razerd_led_t *leds = nullptr;
    size_t lc = 0;
    if (razerd_get_leds(r, idstr.c_str(), RAZERD_PROFILE_INVALID, &leds, &lc) == 0) {
        for (size_t i = 0; i < lc; i++) {
            LedRow row;
            row.led = leds[i];
            int gi  = static_cast<int>(i) + 1;

            grid->addWidget(new QLabel(leds[i].name, inner), gi, 0);

            row.stateBox = new QCheckBox(inner);
            row.stateBox->setChecked(leds[i].state != 0);
            grid->addWidget(row.stateBox, gi, 1);

            if (leds[i].can_change_color) {
                row.colorBtn = new QToolButton(inner);
                row.colorBtn->setFixedSize(28, 20);
                row.colorBtn->setStyleSheet(colorStyle(leds[i].r, leds[i].g, leds[i].b));
                /* Use index capture for the slot */
                int idx = static_cast<int>(i);
                connect(row.colorBtn, &QToolButton::clicked, this,
                        [this, idx]() { onColorPick(idx); });
                grid->addWidget(row.colorBtn, gi, 2);
            } else {
                grid->addWidget(new QLabel("—", inner), gi, 2);
            }

            row.modeCombo = new QComboBox(inner);
            for (int m = 0; m < 5; m++) {
                if (leds[i].supported_modes & (1u << static_cast<unsigned>(m)))
                    row.modeCombo->addItem(mode_names[m], m);
            }
            /* Select current mode */
            for (int j = 0; j < row.modeCombo->count(); j++) {
                if (static_cast<uint32_t>(row.modeCombo->itemData(j).toInt()) == leds[i].mode) {
                    row.modeCombo->setCurrentIndex(j);
                    break;
                }
            }
            grid->addWidget(row.modeCombo, gi, 3);

            m_rows.push_back(std::move(row));
        }
        razerd_free_leds(leds);
    }

    grid->setColumnStretch(0, 2);
    grid->setColumnStretch(3, 2);
    scroll->setWidget(inner);
    outerVbox->addWidget(scroll);

    auto *btnRow = new QHBoxLayout;
    m_applyBtn = new QPushButton("Apply", this);
    m_applyBtn->setFixedWidth(80);
    btnRow->addWidget(m_applyBtn);
    btnRow->addStretch();
    outerVbox->addLayout(btnRow);

    m_status = new QLabel(this);
    outerVbox->addWidget(m_status);

    connect(m_applyBtn, &QPushButton::clicked, this, &LedTab::onApply);
}

void LedTab::onColorPick(int row)
{
    LedRow &lr = m_rows[static_cast<size_t>(row)];
    QColor initial(lr.led.r, lr.led.g, lr.led.b);
    QColor c = QColorDialog::getColor(initial, this, "Pick LED colour");
    if (!c.isValid()) return;
    lr.led.r = static_cast<uint8_t>(c.red());
    lr.led.g = static_cast<uint8_t>(c.green());
    lr.led.b = static_cast<uint8_t>(c.blue());
    lr.colorBtn->setStyleSheet(colorStyle(lr.led.r, lr.led.g, lr.led.b));
}

void LedTab::onApply()
{
    /* Snapshot UI state on the main thread before going async */
    std::vector<razerd_led_t> leds;
    for (LedRow &lr : m_rows) {
        razerd_led_t led = lr.led;
        led.state = lr.stateBox->isChecked() ? 1u : 0u;
        if (lr.modeCombo->currentIndex() >= 0)
            led.mode = static_cast<uint32_t>(lr.modeCombo->currentData().toInt());
        leds.push_back(led);
    }

    m_applyBtn->setEnabled(false);
    m_status->setText("Applying…");

    razerd_t   *r     = m_r;
    std::string idstr = m_idstr;

    auto *watcher = new QFutureWatcher<QString>(this);
    connect(watcher, &QFutureWatcher<QString>::finished, this, [this, watcher]() {
        m_status->setText(watcher->result());
        m_applyBtn->setEnabled(true);
        watcher->deleteLater();
    });
    watcher->setFuture(QtConcurrent::run([=]() -> QString {
        int errors = 0;
        for (const razerd_led_t &led : leds) {
            if (razerd_set_led(r, idstr.c_str(), RAZERD_PROFILE_INVALID, &led))
                errors++;
        }
        return errors ? QString("%1 LED(s) failed to apply.").arg(errors)
                      : "LEDs applied.";
    }));
}
