#pragma once
#include "gpib.h"
#include <QObject>
#include <QMap>
#include <QStringList>
#include <optional>
#include <functional>

enum class Waveform { Sine = 1, Triangle, Square, PositivePulse, NegativePulse, DC };
enum class RunMode { Continuous, Triggered, Gated, Burst };

struct TaborSettings
{
    Waveform waveform = Waveform::Sine;
    double frequency = 10000;
    double amplitude = 1;
    double offset = 0;
    double symmetry = 50;
    double dcLevel = 0;
    RunMode runMode = RunMode::Continuous;
    bool negativeSlope = false;
    bool ttlLevel = false;
    double triggerPhase = 0;
    int burstCount = 2;
    int sweep = 0; // 0 off, 1..4 linear, 5..8 logarithmic.
    double sweepStop = 2000;
    double sweepTime = 1;
    double sweepMarker = 5000;
};

struct MachineStatus
{
    int model = 8200;
    int display = 0;
    int sweep = 0;
    bool externalCounter = false;
    bool phaseLock = false;
    bool gated = false;
    bool triggered = false;
    bool burst = false;
    bool negativeSlope = false;
    bool ttlLevel = false;
    int waveform = 0;
    bool hasOptions() const { return model == 8220 || model == 8222; }
};

MachineStatus parseMachineStatus(const QByteArray &reply);
double parseNumericReply(int parameter, const QByteArray &reply);
QString parseErrorReply(const QByteArray &reply);
double maximumOffset(double amplitude);
QString settingsCommand(const TaborSettings &settings);
QString phaseLockCommand(double phase, bool negativeSlope, bool ttlLevel);

struct InstrumentSnapshot
{
    MachineStatus machine;
    QMap<int,double> values;
    std::optional<double> externalFrequency;
};
struct InstrumentResult
{
    bool connected = false;
    QString error;
    std::optional<InstrumentSnapshot> snapshot;
};
Q_DECLARE_METATYPE(InstrumentResult)

class Tabor8200 final : public QObject
{
    Q_OBJECT
public:
    explicit Tabor8200(QObject *parent = nullptr);
    void connectDevice(GpibTarget target);
    void disconnectDevice();
    void execute(const QString &command, bool readValues = false);
    void readSettings();
    void readErrors();
    void readCounter();
    void reset();

signals:
    void finished(const InstrumentResult &result);

private:
    void waitReady();
    void send(const QByteArray &command, bool checkErrors = true);
    QByteArray query(int parameter);
    QString errorStatus();
    InstrumentSnapshot snapshot(bool readValues);
    void release();
    void run(const std::function<void(InstrumentResult &)> &operation);
    std::unique_ptr<GpibSession> device;
};
