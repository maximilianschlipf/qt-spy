#include "qt_spy/probe.h"

#include <QApplication>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSpinBox>
#include <QTextEdit>
#include <QVBoxLayout>

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("sample_mmi"));

    qt_spy::Probe probe;

    QWidget window;
    window.setObjectName(QStringLiteral("mmiMainWindow"));
    window.setWindowTitle(QStringLiteral("Sample MMI"));

    auto *layout = new QVBoxLayout(&window);

    auto *statusLabel = new QLabel(QStringLiteral("Enter operator details:"));
    statusLabel->setObjectName(QStringLiteral("statusLabel"));
    layout->addWidget(statusLabel);

    auto *formGroup = new QGroupBox(QStringLiteral("Operator"));
    formGroup->setObjectName(QStringLiteral("operatorGroup"));
    auto *formLayout = new QFormLayout(formGroup);

    auto *nameEdit = new QLineEdit();
    nameEdit->setObjectName(QStringLiteral("nameEdit"));
    formLayout->addRow(QStringLiteral("Name:"), nameEdit);

    auto *ageSpin = new QSpinBox();
    ageSpin->setObjectName(QStringLiteral("ageSpin"));
    ageSpin->setRange(18, 99);
    formLayout->addRow(QStringLiteral("Age:"), ageSpin);

    auto *notesEdit = new QTextEdit();
    notesEdit->setObjectName(QStringLiteral("notesEdit"));
    formLayout->addRow(QStringLiteral("Notes:"), notesEdit);

    layout->addWidget(formGroup);

    auto *buttonRow = new QWidget();
    buttonRow->setObjectName(QStringLiteral("buttonRow"));
    auto *buttonLayout = new QHBoxLayout(buttonRow);
    buttonLayout->setContentsMargins(0, 0, 0, 0);

    auto *submitButton = new QPushButton(QStringLiteral("Submit"));
    submitButton->setObjectName(QStringLiteral("submitButton"));
    buttonLayout->addWidget(submitButton);

    auto *resetButton = new QPushButton(QStringLiteral("Reset"));
    resetButton->setObjectName(QStringLiteral("resetButton"));
    buttonLayout->addWidget(resetButton);

    layout->addWidget(buttonRow);
    layout->addStretch();

    window.resize(420, 360);
    window.show();

    return app.exec();
}
