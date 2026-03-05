#pragma once
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QWidget>
#include <string>
#include <vector>
extern "C" {
#include "librazerd.h"
}

class ProfileTab : public QWidget {
    Q_OBJECT
public:
    explicit ProfileTab(razerd_t *r, const std::string &idstr, QWidget *parent = nullptr);

private slots:
    void onSetActive();
    void onRename();
    void onSelectionChanged();

private:
    void reload();

    razerd_t          *m_r;
    std::string        m_idstr;
    std::vector<uint32_t> m_ids;
    uint32_t           m_active = RAZERD_PROFILE_INVALID;

    QListWidget *m_list      = nullptr;
    QLineEdit   *m_nameEdit  = nullptr;
    QLabel      *m_status    = nullptr;
};
