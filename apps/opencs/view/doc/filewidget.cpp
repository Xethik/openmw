
#include "filewidget.hpp"

#include <QHBoxLayout>
#include <QLineEdit>
#include <QLabel>
#include <QRegExpValidator>
#include <QRegExp>

QString CSVDoc::FileWidget::getExtension() const
{
    return mAddon ? ".omwaddon" : ".omwgame";
}

CSVDoc::FileWidget::FileWidget (QWidget *parent) : QWidget (parent), mAddon (false)
{
    QHBoxLayout *layout = new QHBoxLayout (this);

    mInput = new QLineEdit (this);
    mInput->setValidator (new QRegExpValidator(QRegExp("^[a-zA-Z0-9\\s]*$")));

    layout->addWidget (mInput, 1);

    mType = new QLabel (this);

    layout ->addWidget (mType);

    connect (mInput, SIGNAL (textChanged (const QString&)), this, SLOT (textChanged (const QString&)));

    setLayout (layout);
}

void CSVDoc::FileWidget::setType (bool addon)
{
    mAddon = addon;

    mType->setText (getExtension());
}

QString CSVDoc::FileWidget::getName() const
{
    QString text = mInput->text();

    if (text.isEmpty())
        return "";

    return text + getExtension();
}

void CSVDoc::FileWidget::textChanged (const QString& text)
{
    emit nameChanged (getName(), mAddon);
}