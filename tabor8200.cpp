#include "tabor8200.h"
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QRegularExpression>
#include <QThread>
#include <algorithm>
#include <array>
#include <cmath>
#include <stdexcept>
#include <limits>

namespace {

QString translate(const char *source)
{
    return QCoreApplication::translate("Tabor8200", source);
}
[[noreturn]] void fail(const QString &message)
{
    throw std::runtime_error(message.toUtf8().constData());
}
void require(bool valid, const char *message)
{
    if (!valid) {
        fail(translate(message));
    }
}
bool between(double value,double lower,double upper)
{
    return std::isfinite(value) && value >= lower && value <= upper;
}
QString number(double value)
{
    return QString::number(value,'G',9);
}
QString digits(const QByteArray &reply, const char *prefix, int length)
{
    QByteArray data = reply.trimmed();
    require(data.startsWith(prefix), QT_TRANSLATE_NOOP("Tabor8200", "Unerwartetes Präfix in der Geräteantwort."));
    data = data.mid(4).trimmed();
    require(data.size() == length, QT_TRANSLATE_NOOP("Tabor8200", "Die Geräteantwort hat eine ungültige Länge."));
    for (char digit : data) {
        require(digit >= '0' && digit <= '9', QT_TRANSLATE_NOOP("Tabor8200", "Die Statusantwort enthält ungültige Zeichen."));
    }
    return QString::fromLatin1(data);
}
void validateSymmetry(double frequency,double symmetry)
{
    require(between(symmetry,10,90), QT_TRANSLATE_NOOP("Tabor8200", "Symmetrie muss zwischen 10 und 90 % liegen."));
    const double shortest = std::min(symmetry,100-symmetry) / 100 / frequency;
    require(shortest >= 25e-9 * (1-1e-10),
            QT_TRANSLATE_NOOP("Tabor8200", "Die positive und negative Pulsdauer müssen jeweils mindestens 25 ns betragen."));
}
} // namespace

MachineStatus parseMachineStatus(const QByteArray &reply)
{
    const auto data=reply.trimmed();
    bool modelValid=false;
    const int model=data.left(4).toInt(&modelValid);
    require(modelValid && (model == 8200 || model == 8220 || model == 8222),
            QT_TRANSLATE_NOOP("Tabor8200", "Die Antwort gehört zu keiner unterstützten Tabor-8200-Variante."));
    const auto fields=digits(data,data.left(4).constData(),13);
    const auto value=[&](int index) { return fields[index].digitValue(); };
    require(value(1)<=8 && value(9)<=6 && value(10)<=3 && value(12)<=7,
            QT_TRANSLATE_NOOP("Tabor8200", "Die Betriebsart in der Statusantwort ist ungültig."));
    for (int index : {2,3,4,5,6,7,8}) {
        require(value(index)<=1, QT_TRANSLATE_NOOP("Tabor8200", "Die Statusantwort enthält ungültige Schaltzustände."));
    }
    MachineStatus result;
    result.model=model;
    result.display=value(0);
    result.sweep=value(1);
    result.externalCounter=value(2);
    result.phaseLock=value(3);
    result.gated=value(4);
    result.triggered=value(5);
    result.burst=value(6);
    result.negativeSlope=value(7);
    result.ttlLevel=value(8);
    result.waveform=value(9);
    return result;
}

double parseNumericReply(int parameter,const QByteArray &reply)
{
    static const std::array<const char *,12> prefixes{{
        "FREQ","AMPL","OFST","SYMM","SWPS","SWPT","SWPM","PHAS","BRST","TRGP","DCVO","EXTF"
    }};
    require(parameter >= 0 && parameter < int(prefixes.size()),
            QT_TRANSLATE_NOOP("Tabor8200", "Unbekannter Geräteparameter."));
    const auto data=reply.trimmed();
    const auto prefix=data.left(4);
    bool prefixValid=prefix == prefixes[parameter];
    if (parameter == 0) {
        prefixValid=prefixValid || prefix == "FRLK" || prefix == "FRSY";
    }
    if (parameter == 10) {
        prefixValid=prefixValid || prefix == "DCV0";
    }
    require(prefixValid,QT_TRANSLATE_NOOP("Tabor8200", "Unerwartetes Präfix in der Geräteantwort."));
    auto payload=QString::fromLatin1(data.mid(4)).trimmed();
    // Fixed-width replies pad the mantissa after its sign, e.g. FREQ+  20.00E+6.
    if (payload.startsWith('+') || payload.startsWith('-')) {
        payload=payload.left(1)+payload.mid(1).trimmed();
    }
    static const QRegularExpression numeric(QStringLiteral("^[+-]?(?:[0-9]+(?:\\.[0-9]*)?|\\.[0-9]+)(?:[Ee][+-]?[0-9]+)?$"));
    require(numeric.match(payload).hasMatch(),QT_TRANSLATE_NOOP("Tabor8200", "Ungültiger Zahlenwert in der Geräteantwort."));
    bool valid=false;
    const double value=payload.toDouble(&valid);
    require(valid && std::isfinite(value),QT_TRANSLATE_NOOP("Tabor8200", "Ungültiger Zahlenwert in der Geräteantwort."));
    static const std::array<std::pair<double,double>,12> bounds{{
        {0.002,20e6},{0.001,15},{-6.7,6.7},{10,90},{0.002,20e6},{0.01,1000},
        {-20e6,20e6},{-90,90},{2,500000},{-90,90},{-7.5,7.5},{0,std::numeric_limits<double>::max()}
    }};
    require(between(value,bounds[parameter].first,bounds[parameter].second),
            QT_TRANSLATE_NOOP("Tabor8200","Der gelesene Parameter liegt außerhalb des zulässigen Bereichs."));
    require(parameter!=8 || value==std::floor(value),
            QT_TRANSLATE_NOOP("Tabor8200","Die gelesene Burstzahl ist keine ganze Zahl."));
    return value;
}

QString parseErrorReply(const QByteArray &reply)
{
    const auto fields=digits(reply,"STAT",11);
    for (const auto digit : fields) {
        require(digit == QLatin1Char('0') || digit == QLatin1Char('1'),
                QT_TRANSLATE_NOOP("Tabor8200", "Die Fehlerantwort enthält ungültige Kennzeichen."));
    }
    static const std::array<const char *,8> labels{{
        QT_TRANSLATE_NOOP("Tabor8200","Ungültiger Befehl"),
        QT_TRANSLATE_NOOP("Tabor8200","Ungültiger Parameter"),
        QT_TRANSLATE_NOOP("Tabor8200","Offsetfehler"),
        QT_TRANSLATE_NOOP("Tabor8200","Symmetriefehler bei Ausgangsfrequenz"),
        QT_TRANSLATE_NOOP("Tabor8200","Symmetriefehler bei Sweep-Stopp"),
        QT_TRANSLATE_NOOP("Tabor8200","Gerätespeicher nicht verfügbar"),
        QT_TRANSLATE_NOOP("Tabor8200","Benötigte Option fehlt"),
        QT_TRANSLATE_NOOP("Tabor8200","Phasenregelung nicht eingerastet")
    }};
    QStringList errors;
    for (int i=0; i<int(labels.size()); ++i) {
        if (fields[i] == QLatin1Char('1')) {
            errors.append(translate(labels[i]));
        }
    }
    if (fields.mid(8) != "000") {
        errors.append(QCoreApplication::translate("Tabor8200","Weiterer Gerätefehler"));
    }
    return errors.join(QStringLiteral(", "));
}

double maximumOffset(double amplitude)
{
    // Tables 1-1 and 3-2. Offset also shares the selected attenuator window.
    struct Window { double lowerAmplitude; double limit; double offset; };
    static constexpr std::array<Window,6> windows{{
        {1.6,7.5,6.7},{0.48,2.37,2.13},{0.16,0.75,0.670},
        {0.048,0.237,0.213},{0.016,0.075,0.067},{0.001,0.0237,0.0232}
    }};
    for (const auto &window : windows) {
        if (amplitude >= window.lowerAmplitude) {
            return std::max(0.0,std::min(window.offset,window.limit-amplitude/2));
        }
    }
    return 0;
}

QString settingsCommand(const TaborSettings &settings)
{
    const int waveform=int(settings.waveform);
    require(waveform>=1 && waveform<=6 && int(settings.runMode)>=0 && int(settings.runMode)<=3,
            QT_TRANSLATE_NOOP("Tabor8200","Ungültige Signalform oder Betriebsart."));
    require(between(settings.frequency,0.002,20e6),
            QT_TRANSLATE_NOOP("Tabor8200","Frequenz muss zwischen 2 mHz und 20 MHz liegen."));
    require(between(settings.amplitude,0.001,15) && between(settings.offset,-6.7,6.7),
            QT_TRANSLATE_NOOP("Tabor8200","Amplitude muss zwischen 1 mVss und 15 Vss und Offset zwischen −6,7 und +6,7 V liegen."));
    require(std::abs(settings.offset)<=maximumOffset(settings.amplitude)+1e-12,
            QT_TRANSLATE_NOOP("Tabor8200","Offset und Amplitude überschreiten das gemeinsame Pegelfenster. Siehe Handbuch Tabelle 3-2."));
    require(between(settings.dcLevel,-7.5,7.5),QT_TRANSLATE_NOOP("Tabor8200","DC-Pegel muss zwischen −7,5 und +7,5 V liegen."));
    validateSymmetry(settings.frequency,settings.symmetry);
    require(between(settings.triggerPhase,-90,90) && settings.burstCount>=2 && settings.burstCount<=500000,
            QT_TRANSLATE_NOOP("Tabor8200","Triggerphase muss zwischen −90 und +90° und Burstzahl zwischen 2 und 500000 liegen."));
    if (settings.runMode != RunMode::Continuous && settings.triggerPhase != 0) {
        require(settings.frequency<=1e6,QT_TRANSLATE_NOOP("Tabor8200","Eine variable Triggerphase ist bis 1 MHz spezifiziert."));
    }
    require(settings.sweep>=0 && settings.sweep<=8,QT_TRANSLATE_NOOP("Tabor8200","Ungültige Sweep-Betriebsart."));
    if (settings.sweep != 0) {
        require(settings.waveform != Waveform::DC,QT_TRANSLATE_NOOP("Tabor8200","Ein Frequenz-Sweep benötigt eine Wechselspannungs-Signalform."));
        require(between(settings.sweepStop,0.002,20e6) && between(std::abs(settings.sweepMarker),0.002,20e6)
                && between(settings.sweepTime,0.01,1000) && settings.frequency != settings.sweepStop,
                QT_TRANSLATE_NOOP("Tabor8200","Sweep-Grenzen, Marke oder Zeit liegen außerhalb des zulässigen Bereichs."));
        validateSymmetry(settings.sweepStop,settings.symmetry);
        if (settings.sweep<=4) {
            require(std::max(settings.frequency,settings.sweepStop)/std::min(settings.frequency,settings.sweepStop)<=1000,
                    QT_TRANSLATE_NOOP("Tabor8200","Ein linearer Sweep ist auf ein Frequenzverhältnis von 1000:1 begrenzt."));
        }
    }
    // The whole string is validated by the instrument before being applied.
    // Spaces are ignored. The transport appends the mandatory CR terminator.
    QStringList commands{"E0","P0","S0","G0","T0","B0"};
    commands << "FR"+number(settings.frequency) << "AM"+number(settings.amplitude) << "OF"+number(settings.offset)
             << "SY"+number(settings.symmetry) << "DC"+number(settings.dcLevel)
             << "TP"+number(settings.triggerPhase) << "TB"+QString::number(settings.burstCount)
             << (settings.negativeSlope ? "TS1" : "TS0") << (settings.ttlLevel ? "TL1" : "TL0");
    if (settings.sweep != 0) {
        commands << "SS"+number(settings.sweepStop) << "ST"+number(settings.sweepTime)
                 << "SM"+number(settings.sweepMarker) << "S"+QString::number(settings.sweep);
    }
    switch (settings.runMode) {
    case RunMode::Continuous: break;
    case RunMode::Triggered: commands << "T1"; break;
    case RunMode::Gated: commands << "G1"; break;
    case RunMode::Burst: commands << "B1"; break;
    }
    // U1..U6 both choose the waveform and enable its output (manual 4.9.5).
    commands << "U"+QString::number(waveform);
    return commands.join(QLatin1Char(' '));
}

QString phaseLockCommand(double phase,bool negativeSlope,bool ttlLevel)
{
    require(between(phase,-90,90),QT_TRANSLATE_NOOP("Tabor8200","Phasenabweichung muss zwischen −90 und +90° liegen."));
    return QString("E0 S0 G0 T0 B0 %1 %2 PH%3 P1")
        .arg(negativeSlope ? "TS1" : "TS0",ttlLevel ? "TL1" : "TL0",number(phase));
}

Tabor8200::Tabor8200(QObject *parent) : QObject(parent) {}

void Tabor8200::waitReady()
{
    QElapsedTimer timer;
    timer.start();
    while (timer.elapsed()<3000) {
        if (device->status() & 0x01) {
            return;
        }
        QThread::msleep(10);
    }
    fail(tr("Der Tabor hat die Befehlsbereitschaft nicht innerhalb von 3 Sekunden bestätigt."));
}

QString Tabor8200::errorStatus()
{
    device->write("N12");
    return parseErrorReply(device->read());
}

void Tabor8200::send(const QByteArray &command,bool checkErrors)
{
    device->write(command);
    waitReady();
    if (checkErrors && (device->status() & 0x04)) {
        const auto errors=errorStatus(); // N12 reads and clears the latched error flags.
        if (!errors.isEmpty()) {
            fail(tr("Tabor meldet %1.").arg(errors));
        }
        fail(tr("Der Fehlerstatus war gesetzt, die Fehlerantwort enthielt jedoch keine Ursache."));
    }
}

QByteArray Tabor8200::query(int parameter)
{
    // Manual 4.10.1 requires the data read immediately after its N command.
    device->write("N"+QByteArray::number(parameter));
    return device->read();
}

InstrumentSnapshot Tabor8200::snapshot(bool readValues)
{
    InstrumentSnapshot result;
    result.machine=parseMachineStatus(query(13));
    if (readValues) {
        if (result.machine.externalCounter) {
            result.externalFrequency=parseNumericReply(11,query(11));
        } else {
            for (int index=0; index<=10; ++index) {
                if (index != 7 || result.machine.hasOptions()) {
                    result.values[index]=parseNumericReply(index,query(index));
                }
            }
        }
    }
    return result;
}

void Tabor8200::run(const std::function<void(InstrumentResult &)> &operation)
{
    InstrumentResult result;
    try {
        if (!device) {
            fail(tr("GPIB ist nicht verbunden."));
        }
        operation(result);
    } catch (const std::exception &error) {
        result.error=QString::fromUtf8(error.what());
    }
    result.connected=bool(device);
    emit finished(result);
}

void Tabor8200::connectDevice(GpibTarget target)
{
    InstrumentResult result;
    try {
        if (device) {
            release();
        }
        device=openGpib(target);
        send("X0 Z0 Q5");
        result.snapshot=snapshot(false);
    } catch (const std::exception &error) {
        result.error=QString::fromUtf8(error.what());
        try { release(); } catch (...) {}
    }
    result.connected=bool(device);
    emit finished(result);
}

void Tabor8200::release()
{
    auto connection=std::move(device);
    if (connection) {
        connection->local();
    }
}

void Tabor8200::disconnectDevice()
{
    InstrumentResult result;
    try {
        release();
    } catch (const std::exception &error) {
        result.error=QString::fromUtf8(error.what());
    }
    emit finished(result);
}

void Tabor8200::execute(const QString &command,bool readValues)
{
    run([&](InstrumentResult &result) {
        send(command.toLatin1());
        result.snapshot=snapshot(readValues);
    });
}
void Tabor8200::readSettings()
{
    run([&](InstrumentResult &result) { result.snapshot=snapshot(true); });
}
void Tabor8200::readErrors()
{
    run([&](InstrumentResult &result) {
        const auto errors=errorStatus();
        result.snapshot=snapshot(false);
        if (!errors.isEmpty()) {
            result.error=tr("Tabor meldet %1.").arg(errors);
        }
    });
}
void Tabor8200::readCounter()
{
    run([&](InstrumentResult &result) {
        result.snapshot=snapshot(false);
        if (!result.snapshot->machine.hasOptions() || !result.snapshot->machine.externalCounter) {
            fail(tr("Der externe Frequenzzähler benötigt Option 20 oder 22 und den aktiven Zählermodus."));
        }
        result.snapshot->externalFrequency=parseNumericReply(11,query(11));
    });
}
void Tabor8200::reset()
{
    run([&](InstrumentResult &result) {
        device->clear();
        send("X0 Z0 Q5");
        result.snapshot=snapshot(true);
    });
}
