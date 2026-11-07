#pragma once

#include "core/Project.h"
#include "core/TextureAssociation.h"

#include <QString>

namespace mcstudio {

// Association results depend only on the imported ZIP content, the chosen
// reference instance and the fallback switches. The cache lives next to
// reference.json in the project folder and is never part of the pack ZIP.
struct AssociationCacheKey {
    QString zipSha1;
    QString contentFingerprint; // fallback for created/folder projects without a ZIP
    QString referenceInstanceKey;
    bool fallbackModels = true;
    bool fallbackLang = true;
};

QString associationCacheFile(const Project &project);
bool loadAssociationCache(const Project &project, const AssociationCacheKey &key,
                          AssociationResult *result);
bool saveAssociationCache(const Project &project, const AssociationCacheKey &key,
                          const AssociationResult &result);
void removeAssociationCache(const Project &project);

} // namespace mcstudio
