#pragma once
#include <QByteArray>
#include <QString>
#include <memory>

struct GpibTarget
{
    int board = 0;
    int address = 16;
};
class GpibSession
{
public:
    virtual ~GpibSession() = default;
    virtual void write(const QByteArray &command) = 0;
    virtual QByteArray read() = 0;
    virtual unsigned char status() = 0;
    virtual void clear() = 0;
    virtual void local() = 0;
};
std::unique_ptr<GpibSession> openGpib(GpibTarget target);
