#include "core/Version.h"

#include <QtGlobal>

#ifndef MCSTUDIO_VERSION
#define MCSTUDIO_VERSION "0.2.2"
#endif

#ifndef MCSTUDIO_BUILD_TYPE
#define MCSTUDIO_BUILD_TYPE "Unknown"
#endif

namespace mcstudio {

QString version()
{
    return QString::fromLatin1(MCSTUDIO_VERSION);
}

QString buildSummary()
{
    QString compiler = QStringLiteral("Unknown compiler");
#ifdef _MSC_VER
    compiler = QStringLiteral("MSVC %1").arg(_MSC_VER);
#endif

    return QStringLiteral("Qt %1 · %2 · %3")
        .arg(QString::fromLatin1(qVersion()), compiler, QString::fromLatin1(MCSTUDIO_BUILD_TYPE));
}

} // namespace mcstudio
