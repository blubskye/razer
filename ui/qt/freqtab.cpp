#include "freqtab.h"

#include <QFormLayout>
#include <QHBoxLayout>
#include <QPushButton>
#include <QVBoxLayout>

FreqTab::FreqTab(razerd_t *r, const std::string &idstr, QWidget *parent)
    : QWidget(parent), m_r(r), m_idstr(idstr)
{
    auto *vbox = new QVBoxLayout(this);
    vbox->setContentsMargins(12, 12, 12, 12);
    vbox->setSpacing(8);

    auto *form = new QFormLayout;
    m_combo = new QComboBox(this);
    form->addRow("Polling rate:", m_combo);
    vbox->addLayout(form);

    /* Get current frequency */
    uint32_t cur = 0;
    (void)razerd_get_freq(r, idstr.c_str(), RAZERD_PROFILE_INVALID, &cur);

    /* Populate combo with supported frequencies */
    uint32_t *freqs = nullptr;
    size_t fc = 0;
    if (razerd_get_supported_freqs(r, idstr.c_str(), &freqs, &fc) == 0) {
        for (size_t i = 0; i < fc; i++) {
            m_combo->addItem(QString("%1 Hz").arg(freqs[i]),
                             static_cast<uint>(freqs[i]));
            if (freqs[i] == cur)
                m_combo->setCurrentIndex(static_cast<int>(i));
        }
        razerd_free_freqs(freqs);
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

    connect(applyBtn, &QPushButton::clicked, this, &FreqTab::onApply);
}

void FreqTab::onApply()
{
    uint32_t freq = m_combo->currentData().toUInt();
    int err = razerd_set_freq(m_r, m_idstr.c_str(), RAZERD_PROFILE_INVALID, freq);
    if (err)
        m_status->setText(QString("Error setting frequency: %1").arg(err));
    else
        m_status->setText("Frequency applied.");
}
