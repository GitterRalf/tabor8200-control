#pragma once
#include "tabor8200.h"
#include <QMainWindow>
#include <QSettings>
#include <QThread>
#include <QTranslator>
#include <functional>
#include <optional>
#include <vector>

class QAction;
class QBoxLayout;
class QCheckBox;
class QCloseEvent;
class QComboBox;
class QDoubleSpinBox;
class QFormLayout;
class QLabel;
class QMenu;
class QPushButton;
class QTabWidget;
class QVBoxLayout;
class ScientificSpinBox;

class MainWindow final : public QMainWindow
{
    Q_OBJECT
public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow() override;

protected:
    void closeEvent(QCloseEvent *event) override;

private:
    enum class Action { Connect, Disconnect, Apply, OutputOff, ReadSettings, ReadErrors, Trigger,
                        StartSweep, StopSweep, Store, Recall, Display, PhaseOn, PhaseOff,
                        CounterOn, CounterOff, ReadCounter, Reset };
    template<typename Widget,typename Setter>
    Widget *localized(Widget *widget,const char *source,Setter setter)
    {
        auto update=[widget,source,setter] { (widget->*setter)(tr(source)); };
        textUpdates.push_back(update);
        update();
        return widget;
    }
    QLabel *caption(const char *source);
    QPushButton *button(const char *source,const char *name);
    QComboBox *comboBox(const char *name,std::initializer_list<const char *> sources);
    ScientificSpinBox *numberInput(const char *name,double lower,double upper,double initial,const char *unit,int decimals=12);
    void row(QFormLayout *form,const char *source,QWidget *input);
    void unitRow(QFormLayout *form,const char *source,ScientificSpinBox *input,
                 const char *baseUnit,const char *alternateUnit,double alternateScale);
    QFormLayout *panel(QBoxLayout *page,const char *source);
    QWidget *page(const char *source);
    QPushButton *actionButton(const char *source,const char *name,Action action);
    void createMenus();
    void createSignalPage();
    void createTriggerSweepPage();
    void createOptionsPage();
    void createDevicePage();
    void editConnection();
    void setDarkTheme(bool dark);
    void installLanguage(const QString &code);
    void changeLanguage(const QString &code);
    void showAbout();
    void checkForUpdates();
    void checkPrivateRelease();
    void finishUpdateCheck(const QByteArray &data, const QString &error = {});
    void loadSettings();
    void saveSettings();
    void interpretInputs();
    TaborSettings settings() const;
    void updateControls();
    void updateReadback();
    void adoptSettings(const InstrumentSnapshot &snapshot);
    void setStatus(std::function<QString()> text={},bool error=false);
    void start(Action action);
    void finish(const InstrumentResult &result);

    QSettings preferences;
    QTranslator appTranslator;
    QTranslator qtTranslator;
    QString language;
    bool darkTheme=true;
    GpibTarget target;
    bool connected=false;
    bool busy=false;
    bool closing=false;
    Action active=Action::Connect;
    std::optional<InstrumentSnapshot> currentSnapshot;
    std::function<QString()> statusText;
    std::vector<std::function<void()>> textUpdates;
    QList<QPushButton *> deviceButtons;
    QList<QPushButton *> optionButtons;
    QMap<int,QDoubleSpinBox *> parameterInputs;
    QThread workerThread;
    Tabor8200 *instrument;

    QTabWidget *tabs;
    QAction *connectionAction;
    QAction *updateAction = nullptr;
    QMenu *languageMenu=nullptr;
    QLabel *connectionLabel;
    QLabel *outputLabel;
    QLabel *statusLabel;
    QLabel *frequencyReadout;
    QLabel *readbackSummary;
    QLabel *optionStatus;
    QLabel *externalReadout;
    QLabel *sweepTimeLabel;
    QPushButton *connectButton;
    QPushButton *triggerButton;
    QPushButton *counterReadButton;
    QComboBox *waveform;
    ScientificSpinBox *frequency;
    ScientificSpinBox *amplitude;
    ScientificSpinBox *offset;
    QDoubleSpinBox *symmetry;
    ScientificSpinBox *dcLevel;
    QComboBox *runMode;
    QComboBox *triggerSlope;
    QComboBox *triggerThreshold;
    QDoubleSpinBox *triggerPhase;
    QDoubleSpinBox *burstCount;
    QComboBox *sweepMode;
    ScientificSpinBox *sweepStop;
    QDoubleSpinBox *sweepTime;
    ScientificSpinBox *sweepMarker;
    QDoubleSpinBox *phase;
    QDoubleSpinBox *memorySlot;
    QComboBox *displayMode;
};
