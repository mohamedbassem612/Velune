#pragma once
#include "core/Library.h"
namespace velune {
// Android Mp3Library: FieldKey.CUSTOM1 = COMM/Songs-DB_Custom1, value velune:<videoId>.
struct Mp3Info {
    Track track;
    QByteArray cover;
    bool valid = false;
};
Mp3Info readVeluneMp3(const QString &path);
bool writeVeluneId(const QString &path, const QString &id);
QString downloadFileName(const Track &track);
} // namespace velune
