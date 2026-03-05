#include "dpitab.h"

#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QVBoxLayout>
#include <QtConcurrent/QtConcurrent>
#include <QFutureWatcher>

DpiTab::DpiTab(razerd_t *r, const std::string &idstr, QWidget *parent)
    : QWidget(parent), m_r(r), m_idstr(idstr)
{
    auto *vbox = new QVBoxLayout(this);
    vbox->setContentsMargins(12, 12, 12, 12);
    vbox->setSpacing(8);

    /* ----- Load supported resolutions -------------------------------- */
    uint32_t *sres = nullptr;
    size_t    src  = 0;
    if (razerd_get_supported_res(r, idstr.c_str(), &sres, &src) == 0) {
        for (size_t i = 0; i < src; i++)
            m_supportedRes.push_back(sres[i]);
        razerd_free_supported_res(sres);
    }

    /* ----- Load DPI mappings ----------------------------------------- */
    razerd_dpi_mapping_t *maps = nullptr;
    size_t mc = 0;
    uint32_t active = 0;
    (void)razerd_get_dpi_mapping(r, idstr.c_str(), 0, 0xFFFFFFFFu, &active);

    auto *form = new QFormLayout;

    /* Slot selector */
    m_slotCombo = new QComboBox(this);
    form->addRow("DPI slot:", m_slotCombo);

    /* X DPI */
    m_xCombo = new QComboBox(this);
    form->addRow("X DPI:", m_xCombo);

    /* Y DPI row (hidden for single-axis devices) */
    m_yRow = new QWidget(this);
    auto *yLayout = new QHBoxLayout(m_yRow);
    yLayout->setContentsMargins(0, 0, 0, 0);
    m_yCombo = new QComboBox(m_yRow);
    m_lockXY = new QCheckBox("Lock X = Y", m_yRow);
    m_lockXY->setChecked(true);
    yLayout->addWidget(m_yCombo);
    yLayout->addWidget(m_lockXY);
    form->addRow("Y DPI:", m_yRow);

    vbox->addLayout(form);

    /* ----- Populate slot combo --------------------------------------- */
    if (razerd_get_dpi_mappings(r, idstr.c_str(), &maps, &mc) == 0) {
        for (size_t i = 0; i < mc; i++) {
            m_mappings.push_back(maps[i]);
            QString label = QString("%1 DPI").arg(maps[i].res[0]);
            if (maps[i].dim_mask & 2u)
                label += QString(" x %1 DPI").arg(maps[i].res[1]);
            m_slotCombo->addItem(label, static_cast<uint>(i));
            if (maps[i].id == active)
                m_slotCombo->setCurrentIndex(static_cast<int>(i));
        }
        razerd_free_dpi_mappings(maps);
    }

    /* Apply + status */
    auto *btnRow = new QHBoxLayout;
    m_applyBtn = new QPushButton("Apply", this);
    m_applyBtn->setFixedWidth(80);
    btnRow->addWidget(m_applyBtn);
    btnRow->addStretch();
    vbox->addLayout(btnRow);

    m_status = new QLabel(this);
    vbox->addWidget(m_status);
    vbox->addStretch();

    /* ----- Populate X/Y combos with supported resolutions ------------ */
    for (uint32_t v : m_supportedRes) {
        m_xCombo->addItem(QString("%1 DPI").arg(v), v);
        m_yCombo->addItem(QString("%1 DPI").arg(v), v);
    }

    /* Initial axis combo values */
    updateAxisCombos(m_slotCombo->currentIndex());

    /* ----- Signals --------------------------------------------------- */
    connect(m_slotCombo, &QComboBox::currentIndexChanged,
            this, &DpiTab::onSlotChanged);
    connect(m_xCombo, &QComboBox::currentIndexChanged,
            this, &DpiTab::onXChanged);
    connect(m_yCombo, &QComboBox::currentIndexChanged,
            this, &DpiTab::onYChanged);
    connect(m_applyBtn, &QPushButton::clicked, this, &DpiTab::onApply);
}

void DpiTab::updateAxisCombos(int slotIndex)
{
    if (slotIndex < 0 || static_cast<size_t>(slotIndex) >= m_mappings.size())
        return;

    const razerd_dpi_mapping_t &m = m_mappings[static_cast<size_t>(slotIndex)];
    bool hasY    = (m.dim_mask & 2u) != 0;
    bool mutable_ = m.is_mutable;

    m_yRow->setVisible(hasY);
    m_xCombo->setEnabled(mutable_);
    m_yCombo->setEnabled(mutable_ && hasY);
    m_lockXY->setEnabled(mutable_ && hasY);

    m_updating = true;
    /* Select current X */
    for (int i = 0; i < m_xCombo->count(); i++) {
        if (m_xCombo->itemData(i).toUInt() == m.res[0]) {
            m_xCombo->setCurrentIndex(i);
            break;
        }
    }
    /* Select current Y */
    if (hasY) {
        for (int i = 0; i < m_yCombo->count(); i++) {
            if (m_yCombo->itemData(i).toUInt() == m.res[1]) {
                m_yCombo->setCurrentIndex(i);
                break;
            }
        }
    }
    m_updating = false;
}

void DpiTab::onSlotChanged(int index)
{
    updateAxisCombos(index);
}

void DpiTab::onXChanged(int index)
{
    if (m_updating) return;
    if (m_lockXY && m_lockXY->isChecked() && m_yRow->isVisible()) {
        m_updating = true;
        m_yCombo->setCurrentIndex(index);
        m_updating = false;
    }
}

void DpiTab::onYChanged(int index)
{
    if (m_updating) return;
    if (m_lockXY && m_lockXY->isChecked() && m_yRow->isVisible()) {
        m_updating = true;
        m_xCombo->setCurrentIndex(index);
        m_updating = false;
    }
}

void DpiTab::onApply()
{
    int slotIdx = m_slotCombo->currentIndex();
    if (slotIdx < 0 || static_cast<size_t>(slotIdx) >= m_mappings.size()) {
        m_status->setText("No slot selected.");
        return;
    }

    m_applyBtn->setEnabled(false);
    m_status->setText("Applying…");

    const razerd_dpi_mapping_t &m = m_mappings[static_cast<size_t>(slotIdx)];
    uint32_t mapping_id = m.id;
    uint32_t x_dpi = m_xCombo->currentData().toUInt();
    uint32_t y_dpi = (m_lockXY && m_lockXY->isChecked())
                         ? x_dpi
                         : m_yCombo->currentData().toUInt();
    bool hasY    = (m.dim_mask & 2u) != 0;
    bool mutable_ = m.is_mutable;

    razerd_t   *r       = m_r;
    std::string idstr   = m_idstr;

    /* Run blocking USB calls off the UI thread */
    auto *watcher = new QFutureWatcher<QString>(this);
    connect(watcher, &QFutureWatcher<QString>::finished, this, [this, watcher]() {
        m_status->setText(watcher->result());
        m_applyBtn->setEnabled(true);
        watcher->deleteLater();
    });

    watcher->setFuture(QtConcurrent::run([=]() -> QString {
        if (mutable_) {
            int err = razerd_change_dpi_mapping(r, idstr.c_str(), mapping_id, 0, x_dpi);
            if (err) return QString("Error setting X DPI: %1").arg(err);
            if (hasY) {
                err = razerd_change_dpi_mapping(r, idstr.c_str(), mapping_id, 1, y_dpi);
                if (err) return QString("Error setting Y DPI: %1").arg(err);
            }
        }
        int err = razerd_set_dpi_mapping(r, idstr.c_str(), 0, mapping_id, 0xFFFFFFFFu);
        if (err) return QString("Error activating slot: %1").arg(err);
        return QString("DPI applied.");
    }));
}
