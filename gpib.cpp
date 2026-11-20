// NI-488.2 transport adapted from RacalDana1991 and Boonton4200-Control.
#include "gpib.h"

#include <QCoreApplication>
#include <QDir>
#include <QLibrary>
#include <QString>
#include <windows.h>

#include <array>
#include <stdexcept>

namespace {

constexpr unsigned int gpibError = 0x8000;
constexpr unsigned int gpibTimeout = 0x4000;
constexpr int timeout300ms = 10;
constexpr int timeout3s = 12;
constexpr int timeoutConfig = 0x0003; // IbcTMO
constexpr int repeatAddressing = 0x0006; // IbcREADDR
constexpr int unaddress = 0x001b; // IbcUnAddr
constexpr int lineFeedEos = 0x1400 | '\n'; // REOS | BIN | LF

[[noreturn]] void fail(const QString &message)
{
    throw std::runtime_error(message.toUtf8().constData());
}

class NiGpib final : public GpibSession
{
public:
    explicit NiGpib(GpibTarget target)
    {
        wchar_t systemDirectory[MAX_PATH]{};
        const auto length = GetSystemDirectoryW(systemDirectory, MAX_PATH);
        if (!length || length >= MAX_PATH) {
            fail(QCoreApplication::translate("NiGpib", "Das Windows-Systemverzeichnis konnte nicht ermittelt werden."));
        }
        library.setFileName(QDir(QString::fromWCharArray(systemDirectory)).filePath(QStringLiteral("ni4882.dll")));
        if (!library.load()) {
            fail(QCoreApplication::translate("NiGpib", "NI-488.2 konnte nicht geladen werden. %1").arg(library.errorString()));
        }

        const auto open = resolve<int (WINAPI *)(int, int, int, int, int, int)>("ibdev");
        const auto listen = resolve<unsigned int (WINAPI *)(int, int, int, short *)>("ibln");
        const auto enableRemote = resolve<void (WINAPI *)(int, const unsigned short *)>("EnableRemote");
        const auto busStatus = resolve<unsigned int (WINAPI *)()>("ThreadIbsta");
        configure = resolve<decltype(configure)>("ibconfig");
        setLocal = resolve<decltype(setLocal)>("ibloc");
        clearDevice = resolve<decltype(clearDevice)>("ibclr");
        offline = resolve<decltype(offline)>("ibonl");
        writeBytes = resolve<decltype(writeBytes)>("ibwrt");
        readBytes = resolve<decltype(readBytes)>("ibrd");
        serialPoll = resolve<decltype(serialPoll)>("ibrsp");
        error = resolve<decltype(error)>("ThreadIberr");
        count = resolve<decltype(count)>("ThreadIbcnt");

        descriptor = open(target.board, target.address, 0, timeout3s, 1, lineFeedEos);
        if (descriptor < 0) {
            fail(QCoreApplication::translate("NiGpib", "GPIB %1, Adresse %2 konnte nicht geöffnet werden (NI-Fehler %3).")
                .arg(target.board).arg(target.address).arg(error()));
        }
        try {
            short present = 0;
            check(listen(descriptor, target.address, 0, &present), QCoreApplication::translate("NiGpib", "GPIB-Adressprüfung"));
            if (!present) {
                fail(QCoreApplication::translate("NiGpib", "Auf GPIB %1, Adresse %2 antwortet kein Teilnehmer.")
                    .arg(target.board).arg(target.address));
            }
            check(configure(descriptor, repeatAddressing, 1), QCoreApplication::translate("NiGpib", "GPIB-Neuadressierung"));
            check(configure(descriptor, unaddress, 1), QCoreApplication::translate("NiGpib", "GPIB-Abadressierung"));
            const std::array<unsigned short, 2> addresses{static_cast<unsigned short>(target.address), 0xffff};
            remote = true;
            enableRemote(target.board, addresses.data());
            check(busStatus(), QCoreApplication::translate("NiGpib", "Fernsteuerung einschalten"));
        } catch (...) {
            close();
            throw;
        }
    }

    ~NiGpib() override { close(); }

    void local() override
    {
        check(setLocal(descriptor), QCoreApplication::translate("NiGpib", "Rückgabe an die Gerätebedienung"));
        remote = false;
    }

    void clear() override
    {
        setTimeout(timeout3s);
        check(clearDevice(descriptor), QCoreApplication::translate("NiGpib", "Gerät zurücksetzen"));
    }

    void write(const QByteArray &commands) override
    {
        setTimeout(timeout3s);
        // Tabor manual 4.9 requires CR to execute the command string.
        if (commands.isEmpty() || commands.contains('\r') || commands.contains('\n')) {
            fail(QCoreApplication::translate("NiGpib", "Ungültige Tabor-Befehlszeile."));
        }
        const QByteArray payload = commands + "\r";
        const auto result = writeBytes(descriptor, payload.constData(), static_cast<size_t>(payload.size()));
        const auto transferred = count();
        check(result, QCoreApplication::translate("NiGpib", "Befehl %1 senden (%2/%3 Byte)")
                          .arg(QString::fromLatin1(commands)).arg(transferred).arg(payload.size()));
        if (transferred != payload.size()) {
            fail(QCoreApplication::translate("NiGpib", "Der GPIB-Befehl wurde nur teilweise übertragen."));
        }
    }

    QByteArray read() override
    {
        setTimeout(timeout3s);
        QByteArray data(64, '\0');
        const auto result = readBytes(descriptor, data.data(), static_cast<size_t>(data.size()));
        const auto bytes = count();
        if (result & gpibTimeout) {
            fail(QCoreApplication::translate("NiGpib", "Der Tabor hat keine vollständige GPIB-Antwort gesendet."));
        }
        check(result, QCoreApplication::translate("NiGpib", "Messdaten lesen"));
        if (bytes <= 0 || bytes > data.size() || !(result & 0x2000)) {
            fail(QCoreApplication::translate("NiGpib", "Ungültige GPIB-Datenlänge."));
        }
        data.truncate(bytes);
        return data;
    }

    unsigned char status() override
    {
        setTimeout(timeout300ms);
        char byte = 0;
        check(serialPoll(descriptor, &byte), QCoreApplication::translate("NiGpib", "Gerätestatus lesen"));
        return static_cast<unsigned char>(byte);
    }

private:
    template<typename Function>
    Function resolve(const char *name)
    {
        const auto symbol = library.resolve(name);
        if (!symbol) {
            fail(QCoreApplication::translate("NiGpib", "NI-488.2 stellt %1 nicht bereit.").arg(QString::fromLatin1(name)));
        }
        return reinterpret_cast<Function>(symbol);
    }

    void check(unsigned int state, const QString &operation) const
    {
        if (state & gpibError) {
            fail(QCoreApplication::translate("NiGpib", "%1 fehlgeschlagen (NI-Fehler %2, Status 0x%3).")
                .arg(operation).arg(error()).arg(state & 0xffff, 0, 16));
        }
    }

    void setTimeout(int timeout)
    {
        if (currentTimeout != timeout) {
            check(configure(descriptor, timeoutConfig, timeout), QCoreApplication::translate("NiGpib", "GPIB-Zeitlimit setzen"));
            currentTimeout = timeout;
        }
    }

    void close()
    {
        if (remote) {
            setLocal(descriptor);
        }
        if (descriptor >= 0) {
            offline(descriptor, 0);
            descriptor = -1;
        }
    }

    QLibrary library;
    int descriptor = -1;
    int currentTimeout = timeout3s;
    bool remote = false;
    unsigned int (WINAPI *configure)(int, int, int) = nullptr;
    unsigned int (WINAPI *setLocal)(int) = nullptr;
    unsigned int (WINAPI *clearDevice)(int) = nullptr;
    unsigned int (WINAPI *offline)(int, int) = nullptr;
    unsigned int (WINAPI *writeBytes)(int, const void *, size_t) = nullptr;
    unsigned int (WINAPI *readBytes)(int, void *, size_t) = nullptr;
    unsigned int (WINAPI *serialPoll)(int, char *) = nullptr;
    unsigned int (WINAPI *error)() = nullptr;
    unsigned int (WINAPI *count)() = nullptr;
};

} // namespace

std::unique_ptr<GpibSession> openGpib(GpibTarget target)
{
    return std::make_unique<NiGpib>(target);
}
