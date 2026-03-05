#pragma once
#include <QCheckBox>
#include <QComboBox>
#include <QLabel>
#include <QPushButton>
#include <QWidget>
#include <string>
#include <vector>
extern "C" {
#include "librazerd.h"
}

class DpiTab : public QWidget {
    Q_OBJECT
public:
    explicit DpiTab(razerd_t *r, const std::string &idstr, QWidget *parent = nullptr);

private slots:
    void onSlotChanged(int index);
    void onXChanged(int index);
    void onYChanged(int index);
    void onApply();

private:
    void updateAxisCombos(int slotIndex);

    razerd_t   *m_r;
    std::string m_idstr;

    /* mapping slot selector */
    QComboBox  *m_slotCombo  = nullptr;

    /* per-axis DPI selectors (Y may be hidden for single-axis devices) */
    QComboBox  *m_xCombo     = nullptr;
    QComboBox  *m_yCombo     = nullptr;
    QCheckBox  *m_lockXY     = nullptr;
    QWidget    *m_yRow       = nullptr; /* container — hidden when no Y axis */

    QPushButton *m_applyBtn  = nullptr;
    QLabel      *m_status    = nullptr;

    /* data */
    std::vector<razerd_dpi_mapping_t> m_mappings;
    std::vector<uint32_t>             m_supportedRes;

    bool m_updating = false; /* guard against signal loops */
};
