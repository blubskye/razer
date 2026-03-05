#include "buttonstab.h"

#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QScrollArea>
#include <QVBoxLayout>

ButtonsTab::ButtonsTab(razerd_t *r, const std::string &idstr, QWidget *parent)
    : QWidget(parent), m_r(r), m_idstr(idstr)
{
    auto *outerVbox = new QVBoxLayout(this);
    outerVbox->setContentsMargins(12, 12, 12, 12);
    outerVbox->setSpacing(8);

    /* Scrollable grid of button → function assignments */
    auto *scroll = new QScrollArea(this);
    scroll->setWidgetResizable(true);
    auto *inner = new QWidget;
    auto *grid  = new QGridLayout(inner);
    grid->setSpacing(6);
    grid->setContentsMargins(4, 4, 4, 4);

    grid->addWidget(new QLabel("<b>Button</b>",   inner), 0, 0);
    grid->addWidget(new QLabel("<b>Function</b>", inner), 0, 1);

    /* Load all available functions once */
    razerd_button_func_t *funcs = nullptr;
    size_t fc = 0;
    if (razerd_get_button_functions(r, idstr.c_str(), &funcs, &fc) == 0) {
        for (size_t i = 0; i < fc; i++)
            m_funcs.push_back(funcs[i]);
        razerd_free_button_functions(funcs);
    }

    /* Load buttons and current assignments */
    razerd_button_t *btns = nullptr;
    size_t bc = 0;
    if (razerd_get_buttons(r, idstr.c_str(), &btns, &bc) == 0) {
        for (size_t i = 0; i < bc; i++) {
            ButtonRow row;
            row.button_id = btns[i].id;
            int gi = static_cast<int>(i) + 1;

            grid->addWidget(new QLabel(btns[i].name, inner), gi, 0);

            row.funcCombo = new QComboBox(inner);
            for (const auto &fn : m_funcs)
                row.funcCombo->addItem(fn.name, static_cast<uint>(fn.id));

            /* Pre-select current function */
            razerd_button_func_t cur{};
            if (razerd_get_button_function(r, idstr.c_str(),
                                           RAZERD_PROFILE_INVALID,
                                           btns[i].id, &cur) == 0) {
                for (int j = 0; j < row.funcCombo->count(); j++) {
                    if (row.funcCombo->itemData(j).toUInt() == cur.id) {
                        row.funcCombo->setCurrentIndex(j);
                        break;
                    }
                }
            }

            grid->addWidget(row.funcCombo, gi, 1);
            m_rows.push_back(std::move(row));
        }
        razerd_free_buttons(btns);
    }

    grid->setColumnStretch(0, 1);
    grid->setColumnStretch(1, 2);
    scroll->setWidget(inner);
    outerVbox->addWidget(scroll);

    auto *btnRow   = new QHBoxLayout;
    auto *applyBtn = new QPushButton("Apply", this);
    applyBtn->setFixedWidth(80);
    btnRow->addWidget(applyBtn);
    btnRow->addStretch();
    outerVbox->addLayout(btnRow);

    m_status = new QLabel(this);
    outerVbox->addWidget(m_status);

    connect(applyBtn, &QPushButton::clicked, this, &ButtonsTab::onApply);
}

void ButtonsTab::onApply()
{
    int errors = 0;
    for (ButtonRow &br : m_rows) {
        uint32_t func_id = br.funcCombo->currentData().toUInt();
        int err = razerd_set_button_function(m_r, m_idstr.c_str(),
                                              RAZERD_PROFILE_INVALID,
                                              br.button_id, func_id);
        if (err) errors++;
    }
    m_status->setText(errors
        ? QString("%1 button(s) failed to apply.").arg(errors)
        : "Buttons applied.");
}
