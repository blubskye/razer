#include "dpitab.h"

#include <QFormLayout>
#include <QHBoxLayout>
#include <QPushButton>
#include <QVBoxLayout>

DpiTab::DpiTab(razerd_t *r, const std::string &idstr, QWidget *parent)
    : QWidget(parent), m_r(r), m_idstr(idstr)
{
    auto *vbox = new QVBoxLayout(this);
    vbox->setContentsMargins(12, 12, 12, 12);
    vbox->setSpacing(8);

    auto *form = new QFormLayout;
    m_combo = new QComboBox(this);
    form->addRow("DPI mapping:", m_combo);
    vbox->addLayout(form);

    /* Populate combo with available DPI mappings */
    razerd_dpi_mapping_t *maps = nullptr;
    size_t mc = 0;
    uint32_t active = 0;
    (void)razerd_get_dpi_mapping(r, idstr.c_str(), 0, 0xFFFFFFFFu, &active);

    if (razerd_get_dpi_mappings(r, idstr.c_str(), &maps, &mc) == 0) {
        for (size_t i = 0; i < mc; i++) {
            QString label = QString("%1 DPI").arg(maps[i].res[0]);
            if (maps[i].dim_mask & 2u)
                label += QString(" x %1 DPI").arg(maps[i].res[1]);
            m_combo->addItem(label, maps[i].id);
            if (maps[i].id == active)
                m_combo->setCurrentIndex(static_cast<int>(i));
        }
        razerd_free_dpi_mappings(maps);
    }

    auto *row = new QHBoxLayout;
    auto *applyBtn = new QPushButton("Apply", this);
    applyBtn->setFixedWidth(80);
    row->addWidget(applyBtn);
    row->addStretch();
    vbox->addLayout(row);

    m_status = new QLabel(this);
    vbox->addWidget(m_status);
    vbox->addStretch();

    connect(applyBtn, &QPushButton::clicked, this, &DpiTab::onApply);
}

void DpiTab::onApply()
{
    uint32_t mapping_id = m_combo->currentData().toUInt();
    int err = razerd_set_dpi_mapping(m_r, m_idstr.c_str(), 0, mapping_id, 0xFFFFFFFFu);
    if (err)
        m_status->setText(QString("Error setting DPI mapping: %1").arg(err));
    else
        m_status->setText("DPI mapping applied.");
}
