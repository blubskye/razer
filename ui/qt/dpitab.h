#pragma once
#include <QWidget>
#include <string>
extern "C" {
#include "librazerd.h"
}

class DpiTab : public QWidget {
    Q_OBJECT
public:
    explicit DpiTab(razerd_t *r, const std::string &idstr, QWidget *parent = nullptr);
};
