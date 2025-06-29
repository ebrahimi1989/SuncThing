#include "validater.h"

bool Validator::validateDevice(const Device &dev)
{
    return !dev.id.trimmed().isEmpty();
}

bool Validator::validateFolder(const Folder &folder)
{
    return (!folder.id.trimmed().isEmpty() && !folder.label.trimmed().isEmpty());
}
