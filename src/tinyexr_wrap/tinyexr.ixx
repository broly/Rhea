module;

#include <tinyexr.h>

export module tinyexr;

export namespace tinyexr
{
    using ::SaveEXR;
    using ::FreeEXRErrorMessage;
    using ::LoadEXR;
    
    using ::EXRVersion;
    using ::EXRHeader;
    using ::EXRImage;
    using ::InitEXRHeader;
    using ::InitEXRImage;
    using ::ParseEXRVersionFromFile;
    using ::ParseEXRHeaderFromFile;
    using ::LoadEXRImageFromFile;
    using ::FreeEXRImage;
    using ::FreeEXRHeader;
}