#include "logger.h"

int main() { 
    logTrace("hello %d", 1);
    logDebug("hello %d", 1);
    logInfo("hello %d %s", 1, "hi");
    logWarning("hello %d", 1);
    logError("hello %d", 1);
    logFatal("hello %d", 1);
}