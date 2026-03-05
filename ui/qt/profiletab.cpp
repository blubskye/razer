#include "profiletab.h"

#include <QFutureWatcher>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QVBoxLayout>
#include <QtConcurrent/QtConcurrent>
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
    m_renameBtn = new QPushButton("Rename", grp);
    m_renameBtn->setFixedWidth(80);
    gbox->addWidget(m_nameEdit);
    gbox->addWidget(m_renameBtn);
    vbox->addWidget(grp);

    /* Set active button */
    auto *row = new QHBoxLayout;
    m_setActiveBtn = new QPushButton("Set Active", this);
    m_setActiveBtn->setFixedWidth(100);
    row->addWidget(m_setActiveBtn);
    row->addStretch();
    vbox->addLayout(row);

    m_status = new QLabel(this);
    vbox->addWidget(m_status);

    connect(m_setActiveBtn, &QPushButton::clicked, this, &ProfileTab::onSetActive);
    connect(m_renameBtn,    &QPushButton::clicked, this, &ProfileTab::onRename);
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
    uint32_t    pid   = m_ids[static_cast<size_t>(row)];
    razerd_t   *r     = m_r;
    std::string idstr = m_idstr;

    m_setActiveBtn->setEnabled(false);
    m_status->setText("Applying…");

    auto *watcher = new QFutureWatcher<QString>(this);
    connect(watcher, &QFutureWatcher<QString>::finished, this, [this, watcher]() {
        m_status->setText(watcher->result());
        m_setActiveBtn->setEnabled(true);
        if (watcher->result() == "Active profile changed.")
            reload();
        watcher->deleteLater();
    });
    watcher->setFuture(QtConcurrent::run([=]() -> QString {
        int err = razerd_set_active_profile(r, idstr.c_str(), pid);
        return err ? QString("Error setting active profile: %1").arg(err)
                   : "Active profile changed.";
    }));
}

void ProfileTab::onRename()
{
    int row = m_list->currentRow();
    if (row < 0 || static_cast<size_t>(row) >= m_ids.size()) {
        m_status->setText("No profile selected.");
        return;
    }
    uint32_t    pid   = m_ids[static_cast<size_t>(row)];
    std::string name  = m_nameEdit->text().toUtf8().toStdString();
    razerd_t   *r     = m_r;
    std::string idstr = m_idstr;

    m_renameBtn->setEnabled(false);
    m_status->setText("Applying…");

    auto *watcher = new QFutureWatcher<QString>(this);
    connect(watcher, &QFutureWatcher<QString>::finished, this, [this, watcher]() {
        m_status->setText(watcher->result());
        m_renameBtn->setEnabled(true);
        if (watcher->result() == "Profile renamed.")
            reload();
        watcher->deleteLater();
    });
    watcher->setFuture(QtConcurrent::run([=]() -> QString {
        int err = razerd_set_profile_name(r, idstr.c_str(), pid, name.c_str());
        return err ? QString("Error renaming profile: %1").arg(err) : "Profile renamed.";
    }));
}
