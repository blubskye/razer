#pragma once
#include <QComboBox>
#include <QLabel>
#include <QPushButton>
#include <QWidget>
#include <string>
#include <vector>
extern "C" {
#include "librazerd.h"
}

struct ButtonRow {
    uint32_t  button_id;
    QComboBox *funcCombo = nullptr;
};

class ButtonsTab : public QWidget {
    Q_OBJECT
public:
    explicit ButtonsTab(razerd_t *r, const std::string &idstr, QWidget *parent = nullptr);

private slots:
    void onApply();

private:
    razerd_t    *m_r;
    std::string  m_idstr;
    std::vector<ButtonRow>         m_rows;
    std::vector<razerd_button_func_t> m_funcs;
    QPushButton *m_applyBtn = nullptr;
    QLabel      *m_status   = nullptr;
};
