#include "dpitab.h"
#include <QLabel>
#include <QVBoxLayout>

DpiTab::DpiTab(razerd_t *r, const std::string &idstr, QWidget *parent)
    : QWidget(parent)
{
    (void)r; (void)idstr;
    auto *layout = new QVBoxLayout(this);
    layout->addWidget(new QLabel("(not yet implemented)", this));
}
