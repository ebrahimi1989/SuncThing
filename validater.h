#ifndef VALIDATOR_H
#define VALIDATOR_H

#include "caster.h"

class Validator
{
public:
    // Validate a Device object. Returns true if valid.
    static bool validateDevice(const Device &dev);

    // Validate a Folder object. Returns true if valid.
    static bool validateFolder(const Folder &folder);
};

#endif // VALIDATOR_H
