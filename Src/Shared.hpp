#pragma once
#pragma warning(push,0)
#include <Corrade/Utility/Debug.h>
using Corrade::Utility::Debug;
using Corrade::Utility::Error;
using Corrade::Utility::Fatal;
using Corrade::Utility::Warning;
#define ERROR \
    Error() << Debug::color(Debug::Color::Red) \
            << "Error:"
#define WARNING                                     \
    Warning() << Debug::color(Debug::Color::Yellow) \
              << "Warning:"
#define FATAL \
    Fatal() << Debug::color(Debug::Color::Red) \
            << "Fatal:"
#define DEBUG \
    Debug() << Debug::color(Debug::Color::White) \
            << "Debug:"
#define INFO \
    Debug() << Debug::color(Debug::Color::Green) \
            << "Info:"
#define ASSERT(expr, msg) if(!(expr))FATAL<<\
__FUNCTION__<<"(at line "<<__LINE__<<"):"<<msg;
#pragma warning(pop)
