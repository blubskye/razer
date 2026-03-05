#include "profiletab.h"

#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QVBoxLayout>
#include <cstdlib>

ProfileTab::ProfileTab(razerd_t *r, const std::string &idstr, QWidget *parent)
    : QWidget(parent), m_r(r), m_idstr(idstr)
{
    auto *vbox = new QVBoxLayout(this);
    vbox->setContentsMargins(12, 12, 12, 12);
    vbox->setSpacing(8);

    m_list = new QListWidget(this);
    vbox->addWidget(m_list);

    /* Rename group */
    auto *grp   = new QGroupBox("Rename selected profile", this);
    auto *gbox  = new QHBoxLayout(grp);
    m_nameEdit  = new QLineEdit(grp);
    auto *renBtn = new QPushButton("Rename", grp);
    renBtn->setFixedWidth(80);
    gbox->addWidget(m_nameEdit);
    gbox->addWidget(renBtn);
    vbox->addWidget(grp);

    /* Set active button */
    auto *row     = new QHBoxLayout;
    auto *actBtn  = new QPushButton("Set Active", this);
    actBtn->setFixedWidth(100);
    row->addWidget(actBtn);
    row->addStretch();
    vbox->addLayout(row);

    m_status = new QLabel(this);
    vbox->addWidget(m_status);

    connect(actBtn,  &QPushButton::clicked, this, &ProfileTab::onSetActive);
    connect(renBtn,  &QPushButton::clicked, this, &ProfileTab::onRename);
    connect(m_list,  &QListWidget::currentRowChanged, this, &ProfileTab::onSelectionChanged);

    reload();
}

void ProfileTab::reload()
{
    m_list->clear();
    m_ids.clear();

    (void)razerd_get_active_profile(m_r, m_idstr.c_str(), &m_active);

    uint32_t *ids = nullptr;
    size_t    pc  = 0;
    if (razerd_get_profiles(m_r, m_idstr.c_str(), &ids, &pc) != 0)
        return;

    for (size_t i = 0; i < pc; i++) {
        m_ids.push_back(ids[i]);

        char *name = nullptr;
        QString label;
        if (razerd_get_profile_name(m_r, m_idstr.c_str(), ids[i], &name) == 0 && name) {
            label = QString::fromUtf8(name);
            free(name);
        } else {
            label = QString("Profile %1").arg(ids[i]);
        }

        if (ids[i] == m_active)
            label += "  [active]";

        auto *item = new QListWidgetItem(label, m_list);
        if (ids[i] == m_active) {
            QFont f = item->font();
            f.setBold(true);
            item->setFont(f);
        }
    }
    razerd_free_profiles(ids);
}

void ProfileTab::onSelectionChanged()
{
    int row = m_list->currentRow();
    if (row < 0 || static_cast<size_t>(row) >= m_ids.size()) {
        m_nameEdit->clear();
        return;
    }
    uint32_t pid = m_ids[static_cast<size_t>(row)];
    char *name = nullptr;
    if (razerd_get_profile_name(m_r, m_idstr.c_str(), pid, &name) == 0 && name) {
        m_nameEdit->setText(QString::fromUtf8(name));
        free(name);
    } else {
        m_nameEdit->clear();
    }
}

void ProfileTab::onSetActive()
{
    int row = m_list->currentRow();
    if (row < 0 || static_cast<size_t>(row) >= m_ids.size()) {
        m_status->setText("No profile selected.");
        return;
    }
    uint32_t pid = m_ids[static_cast<size_t>(row)];
    int err = razerd_set_active_profile(m_r, m_idstr.c_str(), pid);
    if (err)
        m_status->setText(QString("Error setting active profile: %1").arg(err));
    else {
        m_status->setText("Active profile changed.");
        reload();
    }
}

void ProfileTab::onRename()
{
    int row = m_list->currentRow();
    if (row < 0 || static_cast<size_t>(row) >= m_ids.size()) {
        m_status->setText("No profile selected.");
        return;
    }
    uint32_t pid = m_ids[static_cast<size_t>(row)];
    QByteArray utf8 = m_nameEdit->text().toUtf8();
    int err = razerd_set_profile_name(m_r, m_idstr.c_str(), pid, utf8.constData());
    if (err)
        m_status->setText(QString("Error renaming profile: %1").arg(err));
    else {
        m_status->setText("Profile renamed.");
        reload();
    }
}
