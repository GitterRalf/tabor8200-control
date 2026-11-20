#include "mainwindow.h"

#include <QActionGroup>
#include <QApplication>
#include <QCheckBox>
#include <QCloseEvent>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QDoubleValidator>
#include <QFile>
#include <cmath>
#include <stdexcept>
#include <QFormLayout>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QMovie>
#include <QPalette>
#include <QPainter>
#include <QPushButton>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QStyle>
#include <QTabWidget>
#include <QVBoxLayout>
#include <algorithm>

class ScientificSpinBox final : public QDoubleSpinBox
{
public:
    using QDoubleSpinBox::QDoubleSpinBox;

    void setDisplayUnit(const QString &symbol, double scale)
    {
        if (lineEdit()->isModified()) {
            interpretText();
        }
        unitSymbol = symbol;
        displayScale = scale;
        const QSignalBlocker blockEditor(lineEdit());
        lineEdit()->setText(textFromValue(value()));
    }

    QString displayUnit() const { return unitSymbol; }

    QString displayValue(double baseValue, int precision = 10) const
    {
        return locale().toString(baseValue * displayScale, 'g', precision);
    }

protected:
    QString textFromValue(double value) const override
    {
        return displayValue(value);
    }
    double valueFromText(const QString &text) const override
    {
        QString number = text;
        if (!suffix().isEmpty() && number.endsWith(suffix())) {
            number.chop(suffix().size());
        }
        return locale().toDouble(number.trimmed()) / displayScale;
    }
    QValidator::State validate(QString &text, int &position) const override
    {
        QString number = text;
        if (!suffix().isEmpty() && number.endsWith(suffix())) {
            number.chop(suffix().size());
        }
        QDoubleValidator validator(minimum() * displayScale, maximum() * displayScale, 12);
        validator.setLocale(locale());
        validator.setNotation(QDoubleValidator::ScientificNotation);
        int cursor = std::min(position, int(number.size()));
        return validator.validate(number, cursor);
    }

private:
    // Spin-box values stay in Hz or V for validation, GPIB and saved settings.
    QString unitSymbol;
    double displayScale = 1.0;
};

namespace {

QIcon globeIcon(bool dark)
{
    QPixmap icon(24, 24);
    icon.fill(Qt::transparent);
    QPainter painter(&icon);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setPen(QPen(QColor(dark ? "#d4e3f5" : "#27425a"), 1.35));
    painter.drawEllipse(QRectF(3, 3, 18, 18));
    painter.drawEllipse(QRectF(8, 3, 8, 18));
    painter.drawLine(QPointF(3, 12), QPointF(21, 12));
    painter.drawLine(QPointF(5, 7.5), QPointF(19, 7.5));
    painter.drawLine(QPointF(5, 16.5), QPointF(19, 16.5));
    return QIcon(icon);
}


void refreshStyle(QWidget *widget)
{
    widget->style()->unpolish(widget);
    widget->style()->polish(widget);
    widget->update();
}

} // namespace


MainWindow::MainWindow(QWidget *parent) : QMainWindow(parent),instrument(new Tabor8200)
{
    setWindowTitle(QStringLiteral("Tabor 8200 Control"));
    resize(1100,790);
    target.board=std::clamp(preferences.value("gpib/board",0).toInt(),0,99);
    target.address=std::clamp(preferences.value("gpib/address",16).toInt(),0,30);
    installLanguage(preferences.value("language","de").toString());
    setDarkTheme(preferences.value("appearance/dark",true).toBool());
    createMenus();
    auto *body=new QWidget;
    setCentralWidget(body);
    auto *outer=new QVBoxLayout(body);
    outer->setContentsMargins(24,20,24,16);
    outer->setSpacing(14);
    auto *header=new QHBoxLayout;
    auto *titles=new QVBoxLayout;
    auto *title=new QLabel(QStringLiteral("Tabor 8200"));
    title->setObjectName("title");
    titles->addWidget(title);
    auto *subtitle=caption(QT_TR_NOOP("20-MHz-Funktions-/Pulsgenerator"));
    subtitle->setObjectName("muted");
    titles->addWidget(subtitle);
    header->addLayout(titles);
    header->addStretch();
    connectionLabel=new QLabel;
    connectionLabel->setObjectName("connectionState");
    header->addWidget(connectionLabel,0,Qt::AlignVCenter);
    connectButton=button(QT_TR_NOOP("Verbinden"),"connectDevice");
    connectButton->setProperty("primary",true);
    connect(connectButton,&QPushButton::clicked,this,[this] { start(connected ? Action::Disconnect : Action::Connect); });
    header->addWidget(connectButton,0,Qt::AlignVCenter);
    outer->addLayout(header);
    tabs=new QTabWidget;
    tabs->setObjectName("deviceTabs");
    createSignalPage();
    createTriggerSweepPage();
    createOptionsPage();
    createDevicePage();
    outer->addWidget(tabs,1);

    auto *actions=new QHBoxLayout;
    outputLabel=new QLabel;
    outputLabel->setObjectName("outputState");
    actions->addWidget(outputLabel,1);
    auto *read=actionButton(QT_TR_NOOP("Gerätewerte übernehmen"),"readSettings",Action::ReadSettings);
    actions->addWidget(read);
    auto *off=actionButton(QT_TR_NOOP("Ausgang aus"),"outputOff",Action::OutputOff);
    off->setProperty("stopping",true);
    actions->addWidget(off);
    auto *apply=actionButton(QT_TR_NOOP("Übertragen und Ausgang ein"),"applySettings",Action::Apply);
    apply->setProperty("primary",true);
    localized(apply,QT_TR_NOOP("Überträgt Signal, Trigger und den gewählten Sweep. Der Ausgang wird eingeschaltet."),&QPushButton::setToolTip);
    actions->addWidget(apply);
    outer->addLayout(actions);
    statusLabel=new QLabel;
    statusLabel->setObjectName("statusMessage");
    statusLabel->setTextFormat(Qt::PlainText);
    statusLabel->setWordWrap(true);
    statusLabel->setMinimumHeight(34);
    outer->addWidget(statusLabel);
    parameterInputs={{0,frequency},{1,amplitude},{2,offset},{3,symmetry},{4,sweepStop},
                     {5,sweepTime},{6,sweepMarker},{7,phase},{8,burstCount},{9,triggerPhase},{10,dcLevel}};
    loadSettings();
    for (auto *input : tabs->findChildren<QComboBox *>()) {
        connect(input,&QComboBox::currentIndexChanged,this,&MainWindow::updateControls);
    }
    connect(amplitude,&QDoubleSpinBox::valueChanged,this,&MainWindow::updateControls);
    instrument->moveToThread(&workerThread);
    connect(&workerThread,&QThread::finished,instrument,&QObject::deleteLater);
    connect(instrument,&Tabor8200::finished,this,&MainWindow::finish);
    workerThread.start();
    updateControls();
}

MainWindow::~MainWindow()
{
    if (workerThread.isRunning()) {
        QMetaObject::invokeMethod(instrument,&Tabor8200::disconnectDevice,Qt::BlockingQueuedConnection);
        workerThread.quit();
        workerThread.wait();
    }
    qApp->removeTranslator(&appTranslator);
    qApp->removeTranslator(&qtTranslator);
}

QPushButton *MainWindow::actionButton(const char *source,const char *name,Action action)
{
    auto *widget=button(source,name);
    deviceButtons.append(widget);
    connect(widget,&QPushButton::clicked,this,[this,action] { start(action); });
    return widget;
}

void MainWindow::createSignalPage()
{
    auto *layout=new QHBoxLayout(page(QT_TR_NOOP("Signal")));
    auto *form=panel(layout,QT_TR_NOOP("Signalparameter"));
    waveform=comboBox("waveform",{QT_TR_NOOP("Sinus"),QT_TR_NOOP("Dreieck"),QT_TR_NOOP("Rechteck"),
        QT_TR_NOOP("Positiver Puls"),QT_TR_NOOP("Negativer Puls"),QT_TR_NOOP("Gleichspannung")});
    frequency=numberInput("frequency",0.002,20e6,10000,"");
    amplitude=numberInput("amplitude",0.001,15,1,"");
    offset=numberInput("offset",-6.7,6.7,0,"");
    symmetry=numberInput("symmetry",10,90,50," %",2);
    dcLevel=numberInput("dcLevel",-7.5,7.5,0,"");
    row(form,QT_TR_NOOP("Signalform"),waveform);
    unitRow(form,QT_TR_NOOP("Frequenz / Sweep-Start"),frequency,"Hz","MHz",1e-6);
    unitRow(form,QT_TR_NOOP("Amplitude, Spitze–Spitze an 50 Ω"),amplitude,"V","mV",1e3);
    unitRow(form,QT_TR_NOOP("DC-Offset an 50 Ω"),offset,"V","mV",1e3);
    row(form,QT_TR_NOOP("Symmetrie"),symmetry);
    unitRow(form,QT_TR_NOOP("DC-Pegel an 50 Ω"),dcLevel,"V","mV",1e3);

    form=panel(layout,QT_TR_NOOP("Vom Gerät gelesen"));
    frequencyReadout=new QLabel(QStringLiteral("—"));
    frequencyReadout->setObjectName("frequencyValue");
    form->addRow(frequencyReadout);
    readbackSummary=new QLabel;
    readbackSummary->setTextFormat(Qt::PlainText);
    readbackSummary->setWordWrap(true);
    form->addRow(readbackSummary);
    auto *note=caption(QT_TR_NOOP("Gerätewerte übernehmen liest die aktuellen Werte und trägt sie in die Eingabefelder ein."));
    note->setWordWrap(true);
    note->setObjectName("muted");
    form->addRow(note);
}

void MainWindow::createTriggerSweepPage()
{
    auto *layout=new QHBoxLayout(page(QT_TR_NOOP("Trigger und Sweep")));
    auto *form=panel(layout,QT_TR_NOOP("Betrieb und Trigger"));
    runMode=comboBox("runMode",{QT_TR_NOOP("Kontinuierlich"),QT_TR_NOOP("Einzeltrigger"),QT_TR_NOOP("Gate"),QT_TR_NOOP("Burst")});
    triggerSlope=comboBox("triggerSlope",{QT_TR_NOOP("Steigende Flanke"),QT_TR_NOOP("Fallende Flanke")});
    triggerThreshold=comboBox("triggerThreshold",{QT_TR_NOOP("0 V"),QT_TR_NOOP("TTL")});
    triggerPhase=numberInput("triggerPhase",-90,90,0," °",2);
    burstCount=numberInput("burstCount",2,500000,2,"",0);
    row(form,QT_TR_NOOP("Betriebsart"),runMode);
    row(form,QT_TR_NOOP("Triggerflanke"),triggerSlope);
    row(form,QT_TR_NOOP("Triggerschwelle"),triggerThreshold);
    row(form,QT_TR_NOOP("Startphase"),triggerPhase);
    row(form,QT_TR_NOOP("Perioden pro Burst"),burstCount);
    triggerButton=actionButton(QT_TR_NOOP("Bus-Trigger senden"),"manualTrigger",Action::Trigger);
    form->addRow(triggerButton);
    auto *hint=caption(QT_TR_NOOP("Der Bus-Trigger benötigt am Gerät Einzeltrigger, steigende Flanke und TTL."));
    hint->setWordWrap(true);
    hint->setObjectName("muted");
    form->addRow(hint);

    form=panel(layout,QT_TR_NOOP("Frequenz-Sweep"));
    sweepMode=comboBox("sweepMode",{QT_TR_NOOP("Aus"),
        QT_TR_NOOP("Linear · Start → Stopp"),QT_TR_NOOP("Linear · Stopp → Start"),
        QT_TR_NOOP("Linear · Start → Stopp → Start"),QT_TR_NOOP("Linear · Stopp → Start → Stopp"),
        QT_TR_NOOP("Log · Start → Stopp"),QT_TR_NOOP("Log · Stopp → Start"),
        QT_TR_NOOP("Log · Start → Stopp → Start"),QT_TR_NOOP("Log · Stopp → Start → Stopp")});
    sweepStop=numberInput("sweepStop",0.002,20e6,2000,"");
    sweepTime=numberInput("sweepTime",0.01,1000,1," s");
    sweepMarker=numberInput("sweepMarker",-20e6,20e6,5000,"");
    localized(sweepMarker,QT_TR_NOOP("Positiv markiert den Bereich bis Sweep-Ende, negativ eine einzelne Markerfrequenz."),&QDoubleSpinBox::setToolTip);
    row(form,QT_TR_NOOP("Verlauf und Richtung"),sweepMode);
    unitRow(form,QT_TR_NOOP("Stoppfrequenz"),sweepStop,"Hz","MHz",1e-6);
    sweepTimeLabel=new QLabel;
    sweepTimeLabel->setBuddy(sweepTime);
    form->addRow(sweepTimeLabel,sweepTime);
    unitRow(form,QT_TR_NOOP("Markerfrequenz"),sweepMarker,"Hz","MHz",1e-6);
    auto *start=actionButton(QT_TR_NOOP("Übertragen und Sweep starten"),"startSweep",Action::StartSweep);
    start->setProperty("primary",true);
    form->addRow(start);
    form->addRow(actionButton(QT_TR_NOOP("Sweep aus"),"stopSweep",Action::StopSweep));
    hint=caption(QT_TR_NOOP("Die Startfrequenz steht unter Signal. Die Betriebsart links bestimmt automatischen, getriggerten, Gate- oder Burst-Sweep."));
    hint->setWordWrap(true);
    hint->setObjectName("muted");
    form->addRow(hint);
}

void MainWindow::createOptionsPage()
{
    auto *layout=new QVBoxLayout(page(QT_TR_NOOP("Optionen")));
    optionStatus=new QLabel;
    optionStatus->setObjectName("optionStatus");
    layout->addWidget(optionStatus);
    auto *columns=new QHBoxLayout;
    layout->addLayout(columns);
    auto *form=panel(columns,QT_TR_NOOP("Phasenregelung"));
    phase=numberInput("phase",-90,90,0," °",2);
    row(form,QT_TR_NOOP("Phasenabweichung"),phase);
    auto *on=actionButton(QT_TR_NOOP("Phasenregelung ein"),"phaseOn",Action::PhaseOn);
    auto *off=actionButton(QT_TR_NOOP("Phasenregelung aus"),"phaseOff",Action::PhaseOff);
    optionButtons << on << off;
    form->addRow(on);
    form->addRow(off);
    auto *hint=caption(QT_TR_NOOP("Referenzsignal an TRIG IN / EXT FREQ anschließen. Triggerflanke und Schwelle werden aus Trigger und Sweep übernommen."));
    hint->setWordWrap(true);
    hint->setObjectName("muted");
    form->addRow(hint);
    form=panel(columns,QT_TR_NOOP("Externer Frequenzzähler"));
    externalReadout=new QLabel(QStringLiteral("— Hz"));
    externalReadout->setObjectName("frequencyValue");
    form->addRow(externalReadout);
    on=actionButton(QT_TR_NOOP("Zählermodus ein"),"counterOn",Action::CounterOn);
    off=actionButton(QT_TR_NOOP("Zählermodus aus"),"counterOff",Action::CounterOff);
    counterReadButton=actionButton(QT_TR_NOOP("Frequenz lesen"),"readCounter",Action::ReadCounter);
    optionButtons << on << off << counterReadButton;
    form->addRow(on);
    form->addRow(off);
    form->addRow(counterReadButton);
    hint=caption(QT_TR_NOOP("Messsignal an TRIG IN / EXT FREQ anschließen. Messbereich ab 10 Hz, sechsstellige Anzeige."));
    hint->setWordWrap(true);
    hint->setObjectName("muted");
    form->addRow(hint);
    form=panel(layout,QT_TR_NOOP("Externe Modulation"));
    hint=caption(QT_TR_NOOP("VCO/FM wird über den rückseitigen VCO-Eingang gesteuert. 0 bis −10 V verändern die Frequenz, Bandbreite DC bis 70 kHz. Die Grundfrequenz wird unter Signal eingestellt."));
    hint->setWordWrap(true);
    form->addRow(hint);
    layout->addStretch();
}

void MainWindow::createDevicePage()
{
    auto *layout=new QHBoxLayout(page(QT_TR_NOOP("Gerät")));
    auto *form=panel(layout,QT_TR_NOOP("Gerätespeicher"));
    memorySlot=numberInput("memorySlot",0,9,1,"",0);
    row(form,QT_TR_NOOP("Speicherplatz"),memorySlot);
    form->addRow(actionButton(QT_TR_NOOP("Im Gerät speichern"),"storeMemory",Action::Store));
    form->addRow(actionButton(QT_TR_NOOP("Aus Gerät aufrufen"),"recallMemory",Action::Recall));
    auto *hint=caption(QT_TR_NOOP("Gespeichert werden die aktuellen Geräteeinstellungen. Beim Aufrufen werden die Eingabefelder mit den Gerätewerten aktualisiert."));
    hint->setWordWrap(true);
    hint->setObjectName("muted");
    form->addRow(hint);
    form=panel(layout,QT_TR_NOOP("Anzeige und Gerätestatus"));
    displayMode=comboBox("displayMode",{QT_TR_NOOP("Frequenz"),QT_TR_NOOP("Amplitude"),QT_TR_NOOP("Offset"),QT_TR_NOOP("Symmetrie"),
        QT_TR_NOOP("Sweep-Stopp"),QT_TR_NOOP("Sweep-Zeit"),QT_TR_NOOP("Sweep-Marke"),QT_TR_NOOP("Phasenabweichung"),QT_TR_NOOP("Burstzahl"),QT_TR_NOOP("Triggerphase")});
    row(form,QT_TR_NOOP("Geräteanzeige"),displayMode);
    form->addRow(actionButton(QT_TR_NOOP("Anzeige umschalten"),"applyDisplay",Action::Display));
    form->addRow(actionButton(QT_TR_NOOP("Fehler lesen und löschen"),"readErrors",Action::ReadErrors));
    auto *reset=actionButton(QT_TR_NOOP("Werkseinstellungen laden"),"resetDevice",Action::Reset);
    localized(reset,QT_TR_NOOP("Setzt den Tabor zurück. Der Ausgang liefert anschließend 10 kHz Sinus mit 1 Vss."),&QPushButton::setToolTip);
    form->addRow(reset);
}

QLabel *MainWindow::caption(const char *source)
{
    return localized(new QLabel, source, &QLabel::setText);
}

QPushButton *MainWindow::button(const char *source, const char *name)
{
    auto *widget = localized(new QPushButton, source, &QPushButton::setText);
    widget->setObjectName(QString::fromLatin1(name));
    return widget;
}


QComboBox *MainWindow::comboBox(const char *name, std::initializer_list<const char *> sources)
{
    auto *widget = new QComboBox;
    widget->setObjectName(QString::fromLatin1(name));
    int index = 0;
    for (auto *source : sources) {
        widget->addItem(tr(source));
        const int item = index++;
        textUpdates.push_back([widget, item, source] { widget->setItemText(item, tr(source)); });
    }
    return widget;
}

ScientificSpinBox *MainWindow::numberInput(const char *name, double lower, double upper, double initial, const char *unit, int decimals)
{
    auto *input = new ScientificSpinBox;
    input->setObjectName(QString::fromLatin1(name));
    input->setDecimals(decimals);
    input->setRange(lower, upper);
    input->setSingleStep(decimals == 0 ? 1 : 0.001);
    input->setValue(initial);
    input->setSuffix(QString::fromUtf8(unit));
    input->setKeyboardTracking(false);
    input->setButtonSymbols(QAbstractSpinBox::NoButtons);
    input->setMinimumWidth(155);
    return input;
}

void MainWindow::row(QFormLayout *form, const char *source, QWidget *input)
{
    auto *label = caption(source);
    label->setBuddy(input);
    form->addRow(label, input);
}

void MainWindow::unitRow(QFormLayout *form, const char *source, ScientificSpinBox *input,
                         const char *baseUnit, const char *alternateUnit, double alternateScale)
{
    auto *unit = new QComboBox;
    unit->setObjectName(input->objectName() + QStringLiteral("Unit"));
    unit->addItem(QString::fromLatin1(baseUnit), 1.0);
    unit->addItem(QString::fromLatin1(alternateUnit), alternateScale);
    input->setDisplayUnit(unit->currentText(), unit->currentData().toDouble());
    connect(unit, &QComboBox::currentIndexChanged, input, [input, unit] {
        input->setDisplayUnit(unit->currentText(), unit->currentData().toDouble());
    });

    auto *label = caption(source);
    label->setBuddy(input);
    auto updateAccessibleName = [this, label, unit] {
        unit->setAccessibleName(tr("Einheit für %1").arg(label->text()));
    };
    textUpdates.push_back(updateAccessibleName);
    updateAccessibleName();
    auto *field = new QHBoxLayout;
    field->setSpacing(6);
    field->addWidget(input, 1);
    field->addWidget(unit);
    form->addRow(label, field);
}

QFormLayout *MainWindow::panel(QBoxLayout *pageLayout, const char *source)
{
    auto *frame = new QFrame;
    frame->setObjectName("panel");
    auto *layout = new QVBoxLayout(frame);
    layout->setContentsMargins(18, 14, 18, 16);
    auto *title = caption(source);
    title->setObjectName("sectionTitle");
    layout->addWidget(title);
    auto *form = new QFormLayout;
    form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    form->setHorizontalSpacing(24);
    form->setVerticalSpacing(9);
    layout->addLayout(form);
    pageLayout->addWidget(frame, 0, Qt::AlignTop);
    return form;
}

QWidget *MainWindow::page(const char *source)
{
    auto *content = new QWidget;
    auto *scroll = new QScrollArea;
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setWidget(content);
    const int index = tabs->addTab(scroll, tr(source));
    textUpdates.push_back([this, index, source] { tabs->setTabText(index, tr(source)); });
    return content;
}


void MainWindow::setDarkTheme(bool dark)
{
    darkTheme = dark;
    QPalette palette;
    const auto setColor = [&](QPalette::ColorRole role, const char *darkColor, const char *lightColor) {
        palette.setColor(role, QColor(dark ? darkColor : lightColor));
    };
    setColor(QPalette::Window, "#10191d", "#eef4f0");
    setColor(QPalette::WindowText, "#e9f1f1", "#172c28");
    setColor(QPalette::Base, "#111e23", "#ffffff");
    setColor(QPalette::AlternateBase, "#1b2b31", "#e7f0ea");
    setColor(QPalette::Text, "#e9f1f1", "#172c28");
    setColor(QPalette::Button, "#21343a", "#e1ece5");
    setColor(QPalette::ButtonText, "#e9f1f1", "#172c28");
    setColor(QPalette::Highlight, "#b8ea98", "#c0e7a8");
    setColor(QPalette::HighlightedText, "#142517", "#142517");
    setColor(QPalette::PlaceholderText, "#9cb0b4", "#4b625c");
    for (const auto role : {QPalette::Text, QPalette::ButtonText, QPalette::WindowText}) {
        palette.setColor(QPalette::Disabled, role, QColor(dark ? "#82969a" : "#586b65"));
    }
    qApp->setPalette(palette);

    QFile theme(QStringLiteral(":/theme.qss"));
    QString style;
    if (theme.open(QIODevice::ReadOnly)) {
        style = QString::fromUtf8(theme.readAll());
    }
    if (!dark) {
        QFile light(QStringLiteral(":/theme-light.qss"));
        if (light.open(QIODevice::ReadOnly)) {
            style += QString::fromUtf8(light.readAll());
        }
    }
    qApp->setStyleSheet(style);
    if (languageMenu) {
        languageMenu->setIcon(globeIcon(dark));
    }
}



void MainWindow::createMenus()
{
    auto *bar = menuBar();
    bar->setNativeMenuBar(false);
    auto *file = localized(bar->addMenu(QString()), QT_TR_NOOP("&Datei"), &QMenu::setTitle);
    localized(file->addAction(QString(), QKeySequence::Quit, this, &QWidget::close),
              QT_TR_NOOP("&Beenden"), &QAction::setText);
    auto *settings = localized(bar->addMenu(QString()), QT_TR_NOOP("&Einstellungen"), &QMenu::setTitle);
    settings->setObjectName("settingsMenu");
    connectionAction = localized(settings->addAction(QString(), this, &MainWindow::editConnection),
                                 QT_TR_NOOP("Verbindung …"), &QAction::setText);
    connectionAction->setObjectName("connectionSettings");
    auto *appearance = localized(settings->addMenu(QString()), QT_TR_NOOP("Darstellung"), &QMenu::setTitle);
    auto *themes = new QActionGroup(appearance);
    for (bool dark : {false, true}) {
        auto *action = localized(appearance->addAction(QString()), dark ? QT_TR_NOOP("Dunkel") : QT_TR_NOOP("Hell"), &QAction::setText);
        action->setObjectName(dark ? "darkTheme" : "lightTheme");
        action->setCheckable(true);
        action->setChecked(dark == darkTheme);
        themes->addAction(action);
        connect(action, &QAction::triggered, this, [this, dark] { setDarkTheme(dark); });
    }
    settings->addSeparator();
    localized(settings->addAction(QString(), QKeySequence::Save, this, &MainWindow::saveSettings),
              QT_TR_NOOP("Einstellungen speichern"), &QAction::setText)->setObjectName("saveSettings");

    auto *right = new QMenuBar(bar);
    right->setNativeMenuBar(false);
    languageMenu = right->addMenu(QString());
    languageMenu->setObjectName("languageMenu");
    languageMenu->setIcon(globeIcon(darkTheme));
    localized(languageMenu->menuAction(), QT_TR_NOOP("Sprache wählen"), &QAction::setToolTip);
    auto *languages = new QActionGroup(languageMenu);
    const std::array choices{
        std::pair{"de", "Deutsch"}, std::pair{"en", "English"}, std::pair{"fr", "Français"}
    };
    for (const auto &[code, name] : choices) {
        auto *action = languageMenu->addAction(QString::fromUtf8(name));
        action->setObjectName(QString("language-") + code);
        action->setData(QString::fromLatin1(code));
        action->setCheckable(true);
        action->setChecked(language == QLatin1String(code));
        languages->addAction(action);
        connect(action, &QAction::triggered, this, [this, code] { changeLanguage(QString::fromLatin1(code)); });
    }
    auto *about = localized(right->addMenu(QString()), QT_TR_NOOP("Über"), &QMenu::setTitle);
    about->setObjectName("aboutMenu");
    localized(about->addAction(QString(), this, &MainWindow::showAbout), QT_TR_NOOP("Über"), &QAction::setText)->setObjectName("aboutApplication");
    localized(about->addAction(QString(), this, [this] { QMessageBox::aboutQt(this, tr("Über Qt")); }),
              QT_TR_NOOP("Über Qt"), &QAction::setText)->setObjectName("aboutQt");
    about->addSeparator();
    updateAction = localized(about->addAction(QString(), this, &MainWindow::checkForUpdates),
                             QT_TR_NOOP("Nach Updates suchen"), &QAction::setText);
    updateAction->setObjectName("checkForUpdates");
    bar->setCornerWidget(right, Qt::TopRightCorner);
}

void MainWindow::installLanguage(const QString &code)
{
    qApp->removeTranslator(&appTranslator);
    qApp->removeTranslator(&qtTranslator);
    language = QStringLiteral("de");
    if ((code == "en" || code == "fr") && appTranslator.load(":/i18n/tabor8200_" + code)) {
        language = code;
    }
    QLocale numberFormat(language);
    numberFormat.setNumberOptions(QLocale::OmitGroupSeparator);
    QLocale::setDefault(numberFormat);
    setLocale(numberFormat);
    if (qtTranslator.load(":/qt-i18n/qtbase_" + language)) {
        qApp->installTranslator(&qtTranslator);
    }
    if (language != "de") {
        qApp->installTranslator(&appTranslator);
    }
}

void MainWindow::changeLanguage(const QString &code)
{
    if (code == language) return;
    interpretInputs();
    installLanguage(code);
    preferences.setValue("language", language);
    for (auto *action : languageMenu->actions()) {
        action->setChecked(action->data().toString() == language);
    }
    for (const auto &update : textUpdates) update();
    for (auto *input : tabs->findChildren<QDoubleSpinBox *>()) {
        input->setLocale(locale());
    }
    auto *bar = menuBar();
    auto *corner = bar->cornerWidget(Qt::TopRightCorner);
    bar->setCornerWidget(nullptr, Qt::TopRightCorner);
    bar->setCornerWidget(corner, Qt::TopRightCorner);
    statusLabel->setText(statusText ? statusText() : QString());
    updateControls();
}

void MainWindow::showAbout()
{
    QMessageBox dialog(this);
    dialog.setWindowTitle(tr("Über"));
    dialog.setTextFormat(Qt::RichText);
    dialog.setText(QStringLiteral("<b>Tabor 8200 Control</b><br>%1")
                       .arg(tr("Version %1").arg(QApplication::applicationVersion())));
    dialog.setStandardButtons(QMessageBox::Ok);
    QMovie logo(QStringLiteral(":/branding/logo_g.gif"));
    if (logo.isValid()) {
        connect(&logo, &QMovie::frameChanged, &dialog, [&] { dialog.setIconPixmap(logo.currentPixmap()); });
        logo.start();
    } else {
        dialog.setIconPixmap(windowIcon().pixmap(120));
    }
    dialog.exec();
}

void MainWindow::editConnection()
{
    QDialog dialog(this);
    dialog.setWindowTitle(tr("GPIB-Verbindung"));
    auto *form = new QFormLayout(&dialog);
    form->setContentsMargins(20, 18, 20, 18);
    auto *driver = new QLabel(QStringLiteral("NI-488.2"));
    form->addRow(tr("Adapter"), driver);
    auto *board = new QSpinBox;
    board->setRange(0, 99);
    board->setValue(target.board);
    form->addRow(tr("GPIB-Bus"), board);
    auto *address = new QSpinBox;
    address->setRange(0, 30);
    address->setValue(target.address);
    form->addRow(tr("Primäradresse"), address);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    form->addRow(buttons);
    if (dialog.exec() == QDialog::Accepted) {
        target = {board->value(), address->value()};
        updateControls();
    }
}


void MainWindow::loadSettings()
{
    for (auto *input : tabs->findChildren<QDoubleSpinBox *>()) {
        const auto key = "input/" + input->objectName();
        bool valid = false;
        const auto value = preferences.value(key, input->value()).toDouble(&valid);
        if (valid && std::isfinite(value)) input->setValue(value);
    }
    for (auto *input : tabs->findChildren<QComboBox *>()) {
        const auto key = "input/" + input->objectName();
        const int value = preferences.value(key, input->currentIndex()).toInt();
        if (value >= 0 && value < input->count()) input->setCurrentIndex(value);
    }
    for (auto *input : tabs->findChildren<QCheckBox *>()) {
        input->setChecked(preferences.value("input/" + input->objectName(), input->isChecked()).toBool());
    }
}

void MainWindow::saveSettings()
{
    interpretInputs();
    for (auto *input : tabs->findChildren<QDoubleSpinBox *>()) {
        preferences.setValue("input/" + input->objectName(), input->value());
    }
    for (auto *input : tabs->findChildren<QComboBox *>()) {
        preferences.setValue("input/" + input->objectName(), input->currentIndex());
    }
    for (auto *input : tabs->findChildren<QCheckBox *>()) {
        preferences.setValue("input/" + input->objectName(), input->isChecked());
    }
    preferences.setValue("gpib/board", target.board);
    preferences.setValue("gpib/address", target.address);
    preferences.setValue("appearance/dark", darkTheme);
    preferences.setValue("language", language);
    preferences.sync();
    const bool saved = preferences.status() == QSettings::NoError;
    setStatus([saved] { return saved ? tr("Einstellungen gespeichert.") : tr("Die Einstellungen konnten nicht gespeichert werden."); }, !saved);
}

void MainWindow::interpretInputs()
{
    for (auto *input : tabs->findChildren<QDoubleSpinBox *>()) input->interpretText();
}


void MainWindow::setStatus(std::function<QString()> text, bool error)
{
    statusText = std::move(text);
    statusLabel->setText(statusText ? statusText() : QString());
    statusLabel->setProperty("error", error);
    refreshStyle(statusLabel);
}


TaborSettings MainWindow::settings() const
{
    TaborSettings result;
    result.waveform=static_cast<Waveform>(waveform->currentIndex()+1);
    result.frequency=frequency->value();
    result.amplitude=amplitude->value();
    result.offset=offset->value();
    result.symmetry=symmetry->value();
    result.dcLevel=dcLevel->value();
    result.runMode=static_cast<RunMode>(runMode->currentIndex());
    result.negativeSlope=triggerSlope->currentIndex()==1;
    result.ttlLevel=triggerThreshold->currentIndex()==1;
    result.triggerPhase=triggerPhase->value();
    result.burstCount=int(burstCount->value());
    result.sweep=sweepMode->currentIndex();
    result.sweepStop=sweepStop->value();
    result.sweepTime=sweepTime->value();
    result.sweepMarker=sweepMarker->value();
    return result;
}

void MainWindow::adoptSettings(const InstrumentSnapshot &snapshot)
{
    for (auto it=snapshot.values.cbegin(); it!=snapshot.values.cend(); ++it) {
        if (auto *input=parameterInputs.value(it.key())) {
            input->setValue(it.value());
        }
    }
    const auto &machine=snapshot.machine;
    if (machine.waveform>=1 && machine.waveform<=6) {
        waveform->setCurrentIndex(machine.waveform-1);
    }
    const auto mode=machine.burst ? RunMode::Burst : machine.gated ? RunMode::Gated
                    : machine.triggered ? RunMode::Triggered : RunMode::Continuous;
    runMode->setCurrentIndex(int(mode));
    triggerSlope->setCurrentIndex(machine.negativeSlope ? 1 : 0);
    triggerThreshold->setCurrentIndex(machine.ttlLevel ? 1 : 0);
    sweepMode->setCurrentIndex(machine.sweep);
    displayMode->setCurrentIndex(machine.display);
}

void MainWindow::updateReadback()
{
    const auto frequencyUnit=frequency->displayUnit();
    frequencyReadout->setText(QStringLiteral("— %1").arg(frequencyUnit));
    externalReadout->setText(QStringLiteral("— %1").arg(frequencyUnit));
    if (!currentSnapshot) {
        readbackSummary->setText(tr("Gerätewerte wurden noch nicht gelesen."));
        optionStatus->setText(tr("Optionen werden beim Verbinden erkannt."));
        return;
    }
    const auto &snapshot=*currentSnapshot;
    const auto &machine=snapshot.machine;
    const auto format=[this](double value) { return locale().toString(value,'g',9); };
    if (snapshot.values.contains(0)) {
        frequencyReadout->setText(QStringLiteral("%1 %2").arg(frequency->displayValue(snapshot.values.value(0), 9), frequencyUnit));
    }
    if (snapshot.externalFrequency.has_value()) {
        externalReadout->setText(QStringLiteral("%1 %2").arg(frequency->displayValue(*snapshot.externalFrequency, 9), frequencyUnit));
    }
    QStringList lines{tr("Modell %1").arg(machine.model)};
    if (snapshot.values.contains(1)) {
        lines << tr("Amplitude %1 %2ss").arg(amplitude->displayValue(snapshot.values.value(1), 9), amplitude->displayUnit());
    }
    if (snapshot.values.contains(2)) {
        lines << tr("Offset %1 %2").arg(offset->displayValue(snapshot.values.value(2), 9), offset->displayUnit());
    }
    if (snapshot.values.contains(3)) {
        lines << tr("Symmetrie %1 %").arg(format(snapshot.values.value(3)));
    }
    QStringList modes;
    if (machine.externalCounter) modes << tr("Externer Frequenzzähler");
    if (machine.phaseLock) modes << tr("Phasenregelung");
    if (machine.sweep) modes << tr("Sweep");
    if (machine.gated) modes << tr("Gate");
    if (machine.triggered) modes << tr("Einzeltrigger");
    if (machine.burst) modes << tr("Burst");
    if (modes.isEmpty()) modes << tr("Kontinuierlich");
    lines << modes.join(QStringLiteral(" · "));
    readbackSummary->setText(lines.join(QLatin1Char('\n')));
    optionStatus->setText(machine.model==8220 ? tr("Modell 8220 · Option 20 erkannt")
                         : machine.model==8222 ? tr("Modell 8222 · Option 22 erkannt")
                         : tr("Modell 8200 · Grundgerät"));
}

void MainWindow::updateControls()
{
    const bool dc=waveform->currentIndex()==5;
    frequency->setEnabled(!dc);
    amplitude->setEnabled(!dc);
    offset->setEnabled(!dc);
    symmetry->setEnabled(!dc);
    dcLevel->setEnabled(dc);
    triggerPhase->setEnabled(runMode->currentIndex()!=0);
    burstCount->setEnabled(runMode->currentIndex()==3);
    const bool sweeping=sweepMode->currentIndex()!=0;
    sweepStop->setEnabled(sweeping);
    sweepTime->setEnabled(sweeping);
    sweepMarker->setEnabled(sweeping);
    sweepTimeLabel->setText(sweepMode->currentIndex()>=5 ? tr("Zeit pro Dekade") : tr("Sweep-Zeit"));
    offset->setToolTip(tr("Bei dieser Amplitude maximal ±%1 %2 gemäß Pegelfenster.")
                          .arg(offset->displayValue(maximumOffset(amplitude->value()), 6), offset->displayUnit()));

    tabs->setEnabled(!busy);
    connectButton->setEnabled(!busy);
    connectButton->setText(connected ? tr("Trennen") : tr("Verbinden"));
    connectionAction->setEnabled(!busy && !connected);
    for (auto *action : deviceButtons) {
        action->setEnabled(connected && !busy);
    }
    const bool hasOptions=currentSnapshot && currentSnapshot->machine.hasOptions();
    for (auto *action : optionButtons) {
        action->setEnabled(connected && !busy && hasOptions);
    }
    phase->setEnabled(hasOptions);
    counterReadButton->setEnabled(connected && !busy && hasOptions && currentSnapshot->machine.externalCounter);
    const bool canTrigger=currentSnapshot && currentSnapshot->machine.triggered
                          && !currentSnapshot->machine.negativeSlope && currentSnapshot->machine.ttlLevel;
    triggerButton->setEnabled(connected && !busy && canTrigger);
    connectionLabel->setText(busy ? tr("Übertragung …") : connected ? tr("Verbunden") : tr("Getrennt"));
    connectionLabel->setProperty("connected",connected);
    connectionLabel->setToolTip(tr("GPIB %1 · Adresse %2").arg(target.board).arg(target.address));
    refreshStyle(connectionLabel);
    if (connected && currentSnapshot) {
        outputLabel->setText(currentSnapshot->machine.externalCounter ? tr("Zählermodus aktiv")
                             : currentSnapshot->machine.waveform==0 ? tr("Ausgang aus") : tr("Ausgang ein"));
    } else {
        outputLabel->setText(tr("Ausgangsstatus unbekannt"));
    }
    updateReadback();
}

void MainWindow::start(Action action)
{
    if (busy) return;
    if ((action==Action::Connect && connected) || (action!=Action::Connect && !connected)) return;
    interpretInputs();
    QString command;
    try {
        switch (action) {
        case Action::Apply:
        case Action::StartSweep: {
            const auto prepared=settings();
            if (action==Action::StartSweep && prepared.sweep==0) {
                throw std::runtime_error(tr("Zuerst einen Sweep-Verlauf auswählen.").toUtf8().constData());
            }
            if (currentSnapshot && currentSnapshot->machine.model==8222
                && (prepared.frequency<20 || (prepared.sweep && prepared.sweepStop<20))) {
                throw std::runtime_error(tr("Mit Synthesizer-Option 22 beginnt der spezifizierte Frequenzbereich bei 20 Hz.").toUtf8().constData());
            }
            command=settingsCommand(prepared);
            break;
        }
        case Action::OutputOff: command="U0"; break;
        case Action::Trigger: command="TT"; break;
        case Action::StopSweep: command="S0"; break;
        case Action::Store: command="STO"+QString::number(int(memorySlot->value())); break;
        case Action::Recall: command="RCL"+QString::number(int(memorySlot->value())); break;
        case Action::Display: command="D"+QString::number(displayMode->currentIndex()); break;
        case Action::PhaseOn: command=phaseLockCommand(phase->value(),triggerSlope->currentIndex()==1,triggerThreshold->currentIndex()==1); break;
        case Action::PhaseOff: command="P0"; break;
        case Action::CounterOn:
            command=QString("S0 G0 T0 B0 P0 %1 %2 E1")
                .arg(triggerSlope->currentIndex()==1 ? "TS1" : "TS0",triggerThreshold->currentIndex()==1 ? "TL1" : "TL0");
            break;
        case Action::CounterOff: command="E0"; break;
        default: break;
        }
    } catch (const std::exception &error) {
        setStatus([message=QString::fromUtf8(error.what())] { return message; },true);
        return;
    }
    busy=true;
    active=action;
    setStatus([] { return tr("Gerätebefehl wird ausgeführt …"); });
    updateControls();
    const bool readValues=action==Action::Apply || action==Action::StartSweep || action==Action::Recall;
    QMetaObject::invokeMethod(instrument,[worker=instrument,action,command,readValues,target=target] {
        switch (action) {
        case Action::Connect: worker->connectDevice(target); break;
        case Action::Disconnect: worker->disconnectDevice(); break;
        case Action::ReadSettings: worker->readSettings(); break;
        case Action::ReadErrors: worker->readErrors(); break;
        case Action::ReadCounter: worker->readCounter(); break;
        case Action::Reset: worker->reset(); break;
        default: worker->execute(command,readValues); break;
        }
    },Qt::QueuedConnection);
}

void MainWindow::finish(const InstrumentResult &result)
{
    connected=result.connected;
    busy=false;
    currentSnapshot=result.snapshot;
    if (closing) {
        if (connected) start(Action::Disconnect);
        else close();
        return;
    }
    if (!result.error.isEmpty()) {
        setStatus([error=result.error] { return error; },true);
    } else {
        if (result.snapshot) {
            const bool adopt=active==Action::ReadSettings || active==Action::Recall || active==Action::Reset
                             || (active==Action::Apply && result.snapshot->machine.sweep==0);
            if (adopt) adoptSettings(*result.snapshot);
            if (active==Action::StopSweep) sweepMode->setCurrentIndex(0);
        }
        switch (active) {
        case Action::Connect:
            setStatus([address=target.address] { return tr("Verbunden mit GPIB-Adresse %1.").arg(address); }); break;
        case Action::Disconnect:
            setStatus([] { return tr("GPIB-Verbindung getrennt."); }); break;
        case Action::ReadSettings:
            setStatus([] { return tr("Gerätewerte übernommen."); }); break;
        case Action::ReadErrors:
            setStatus([] { return tr("Fehlerstatus gelesen und zurückgesetzt."); }); break;
        case Action::Reset:
            setStatus([] { return tr("Werkseinstellungen geladen, Ausgang eingeschaltet."); }); break;
        default:
            setStatus([] { return tr("Gerätebefehl bestätigt."); }); break;
        }
    }
    updateControls();
}

void MainWindow::closeEvent(QCloseEvent *event)
{
    if (connected || busy) {
        event->ignore();
        closing=true;
        if (!busy) start(Action::Disconnect);
        return;
    }
    event->accept();
}
