#pragma once
#include <QLabel>
#include <QPushButton>
#include <QScrollArea>
#include <QString>
#include <QWidget>
#include <string>
#include <vector>
extern "C" {
#include "librazerd.h"
}

struct LedRow {
    razerd_led_t led;         /* copy of LED state */
    class QCheckBox   *stateBox  = nullptr;
    class QToolButton *colorBtn  = nullptr; /* nullptr if no color support */
    class QComboBox   *modeCombo = nullptr;
};

class LedTab : public QWidget {
    Q_OBJECT
public:
    explicit LedTab(razerd_t *r, const std::string &idstr, QWidget *parent = nullptr);

private slots:
    void onColorPick(int row);
    void onApply();

private:
    razerd_t          *m_r;
    std::string        m_idstr;
    std::vector<LedRow> m_rows;
    QPushButton       *m_applyBtn = nullptr;
    QLabel            *m_status   = nullptr;
};
