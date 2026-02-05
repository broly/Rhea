export module tinyexr;

#include <tinyexr.h>

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