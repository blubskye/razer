#pragma once
#include <QComboBox>
#include <QLabel>
#include <QPushButton>
#include <QWidget>
#include <string>
extern "C" {
#include "librazerd.h"
}

class FreqTab : public QWidget {
    Q_OBJECT
public:
    explicit FreqTab(razerd_t *r, const std::string &idstr, QWidget *parent = nullptr);

private slots:
    void onApply();

private:
    razerd_t   *m_r;
    std::string m_idstr;
    QComboBox   *m_combo     = nullptr;
    QPushButton *m_applyBtn  = nullptr;
    QLabel      *m_status    = nullptr;
};
