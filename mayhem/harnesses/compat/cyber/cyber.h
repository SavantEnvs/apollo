// Stand-in for cyber/cyber.h, for the GNSS parser targets only (first on their include path).
// The real header pulls in the whole Cyber RT framework (component/node/task/timer, and with them
// fastdds). novatel_parser.cc and parser_common.h only use three things from it: the A* logging
// macros (cyber/common/log.h), cyber::Time::Now() (cyber/time/time.h, compiled from upstream), and
// cyber::OK(), the "framework still running" flag GetMessages() loops on. Those first two are the
// real upstream headers; OK() is the state of a framework that was initialized and not shut down,
// which is what cyber::OK() returns for the GNSS driver while it runs.
#pragma once

#include <memory>
#include <string>
#include <utility>

#include "cyber/common/log.h"
#include "cyber/time/time.h"

namespace apollo {
namespace cyber {
inline bool OK() { return true; }
}  // namespace cyber
}  // namespace apollo
