#pragma once

#include "core/AssetStack.h"
#include "core/GameEnvironment.h"
#include "core/GameSettings.h"
#include "core/Project.h"

#include <QStringList>
#include <memory>

namespace mcstudio {

class ModCatalog;

struct ReferenceMatch {
    bool found = false;
    bool compatible = true;
    QString instanceKey;
    QString instanceName;
    QString clientVersion;
    QString loader;
    QString mode; // auto / default / manual / none
    QString reason;
    QString warning;
};

// Picks the reference instance for a pack using (in order) the project
// override, the global default, or pack_format matching.
ReferenceMatch matchReferenceInstance(const GameEnvironmentSnapshot &snapshot,
                                      const GameSettings &settings, int packFormat,
                                      const QVector<int> &supportedFormats,
                                      const QString &projectOverrideKey);

struct ReferenceStackOptions {
    bool fallbackModels = true;
    bool fallbackLang = true;
};

struct ReferenceStackResult {
    std::shared_ptr<AssetStack> stack;
    QStringList diagnostics;
    int referenceJarCount = 0;
    bool hasVanilla = false;
};

// pack/ + source are always present. Reference mod jars (current instance
// only), the client jar and assets/indexes+objects are appended when enabled.
ReferenceStackResult buildReferenceAssetStack(const Project &project,
                                              const InstanceInfo *reference,
                                              const ModCatalog &catalog,
                                              const QStringList &namespaces,
                                              const ReferenceStackOptions &options);

} // namespace mcstudio
