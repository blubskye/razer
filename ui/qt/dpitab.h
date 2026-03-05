#pragma once
#include <QWidget>
#include <QComboBox>
#include <QLabel>
#include <string>
extern "C" {
#include "librazerd.h"
}

class DpiTab : public QWidget {
    Q_OBJECT
public:
    explicit DpiTab(razerd_t *r, const std::string &idstr, QWidget *parent = nullptr);

private slots:
    void onApply();

private:
    razerd_t   *m_r;
    std::string m_idstr;
    QComboBox  *m_combo  = nullptr;
    QLabel     *m_status = nullptr;
};
