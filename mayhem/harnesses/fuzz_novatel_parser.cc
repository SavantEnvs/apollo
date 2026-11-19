// Fuzz Apollo's NovAtel binary log parser (modules/drivers/gnss/parser/novatel_parser.cc): the
// GNSS driver's own decoder for BESTPOS/BESTVEL/INSPVA(X)/CORRIMU/RAWIMU(X)/INSCOV/HEADING and the
// GPS/GLONASS/BDS ephemeris logs, plus RANGE through RTKLIB. The input is the receiver's serial
// byte stream, fed the way the GNSS data parser feeds it: Update(data, size) then GetMessages().
// A fresh parser per input, so nothing carries over between inputs.
//
// NovatelParser is defined in an anonymous namespace inside novatel_parser.cc, so the .cc is
// compiled as part of this TU (build.sh does not compile it separately). That is only needed for
// one thing: NovatelParser's constructor calls RTKLIB init_raw(&raw_) (~70 KB of obs/nav tables)
// and it has no destructor that calls free_raw(), so one parser per input leaks until libFuzzer's
// RSS limit kills the run after ~20k inputs. The driver makes one parser for its lifetime, so the
// leak is harmless there. After each input the harness frees raw_ the way free_raw() is meant to
// be used, through a pointer-to-member (the standard explicit-instantiation access idiom). It does
// not touch the parser's behaviour on the input.
#include "modules/drivers/gnss/parser/novatel_parser.cc"

#include <cstddef>
#include <cstdint>
#include <memory>

#include "glog/logging.h"

namespace {
using apollo::drivers::gnss::NovatelParser;
template <raw_t NovatelParser::*M>
struct RawAccess {
  friend raw_t NovatelParser::*raw_member() { return M; }
};
raw_t NovatelParser::*raw_member();
template struct RawAccess<&NovatelParser::raw_>;
}  // namespace

extern "C" int LLVMFuzzerInitialize(int *argc, char ***argv) {
  // The parser logs (AINFO/AERROR) on every message; keep only FATAL so the logging does not
  // dominate exec time. Set once, never changed by an input.
  FLAGS_minloglevel = 3;
  FLAGS_logtostderr = true;
  google::InitGoogleLogging((*argv)[0]);
  return 0;
}

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
  apollo::drivers::gnss::config::Config config;
  std::unique_ptr<apollo::drivers::gnss::Parser> parser(
      apollo::drivers::gnss::Parser::CreateNovatel(config));
  parser->Update(data, size);
  apollo::drivers::gnss::MessageInfoVec messages;
  parser->GetMessages(&messages);
  free_raw(&(static_cast<NovatelParser *>(parser.get())->*raw_member()));
  return 0;
}
