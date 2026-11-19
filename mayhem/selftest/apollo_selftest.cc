// Known-answer tests for the code the fuzz targets reach:
//  - apollo::bridge::BridgeHeader Serialize -> (receiver framing) -> Diserialize round trip
//  - vendored RTKLIB input_rtcm3 on an RTCM 1005 station-position message
//  - vendored RTKLIB input_oem4 on a NovAtel IONUTCB log
//  - input_oem4 over the upstream GNSS fixture modules/drivers/gnss/test_data/novatel.bin
// Prints "PASS <name>" / "FAIL <name>: why" per test; mayhem/test.sh counts them.
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#include "modules/bridge/common/bridge_header.h"
#include "modules/bridge/common/macro.h"
extern "C" {
#include "third_party/rtklib/rtklib.h"
}

using namespace apollo::bridge;

static int g_fail = 0;
static void report(const char *name, bool ok, const std::string &why = "") {
  if (ok) {
    printf("PASS %s\n", name);
  } else {
    printf("FAIL %s: %s\n", name, why.c_str());
    g_fail++;
  }
}

static std::vector<unsigned char> slurp(const std::string &path) {
  std::ifstream f(path, std::ios::binary);
  return std::vector<unsigned char>(std::istreambuf_iterator<char>(f), {});
}

static bool serialize_header(std::vector<char> *out) {
  BridgeHeader h;
  h.SetHeaderVer(1);
  h.SetMsgName("apollo.canbus.Chassis");
  h.SetMsgID(4242);
  h.SetMsgSize(3000);
  h.SetTotalFrames(3);
  h.SetFrameSize(952);
  h.SetFramePos(2048);
  h.SetIndex(2);
  h.SetTimeStamp(1234.5);
  out->assign(h.GetHeaderSize(), 0);
  return h.Serialize(out->data(), out->size());
}

static void test_bridge_roundtrip() {
  std::vector<char> buf;
  if (!serialize_header(&buf)) return report("bridge_roundtrip", false, "Serialize failed");
  BridgeHeader h;
  if (!h.IsAvailable(buf.data())) return report("bridge_roundtrip", false, "flag not found");
  size_t offset = sizeof(BRIDGE_HEADER_FLAG) + 1;
  hsize header_size;
  memcpy(&header_size, buf.data() + offset, sizeof(hsize));
  offset += sizeof(hsize) + 1;
  if (header_size != buf.size())
    return report("bridge_roundtrip", false, "header size " + std::to_string(header_size));
  if (!h.Diserialize(buf.data() + offset, header_size - offset))
    return report("bridge_roundtrip", false, "Diserialize failed");
  bool ok = h.GetHeaderVer() == 1 && h.GetMsgName() == "apollo.canbus.Chassis" &&
            h.GetMsgID() == 4242 && h.GetMsgSize() == 3000 && h.GetTotalFrames() == 3 &&
            h.GetFrameSize() == 952 && h.GetFramePos() == 2048 && h.GetIndex() == 2 &&
            h.GetTimeStamp() == 1234.5;
  report("bridge_roundtrip", ok,
         "got name=" + h.GetMsgName() + " id=" + std::to_string(h.GetMsgID()) +
             " pos=" + std::to_string(h.GetFramePos()));
}

static void test_bridge_flag() {
  std::vector<char> buf;
  serialize_header(&buf);
  BridgeHeader h;
  bool good = h.IsAvailable(buf.data());
  buf[3] = 'X';
  bool bad = h.IsAvailable(buf.data());
  report("bridge_flag", good && !bad, "IsAvailable good=" + std::to_string(good) +
                                          " corrupted=" + std::to_string(bad));
}

static void test_rtcm3_1005(const std::string &dir) {
  auto d = slurp(dir + "/rtcm3_1005.bin");
  rtcm_t rtcm;
  memset(&rtcm, 0, sizeof(rtcm));
  init_rtcm(&rtcm);
  int last = 0;
  for (unsigned char c : d) {
    int r = input_rtcm3(&rtcm, c);
    if (r) last = r;
  }
  bool ok = last == 5 && rtcm.staid == 321 && std::fabs(rtcm.sta.pos[0] - 1234567.8901) < 1e-6 &&
            std::fabs(rtcm.sta.pos[1] + 2345678.1234) < 1e-6 &&
            std::fabs(rtcm.sta.pos[2] - 3456789.5678) < 1e-6;
  char why[256];
  snprintf(why, sizeof why, "ret=%d staid=%d pos=%.4f %.4f %.4f", last, rtcm.staid,
           rtcm.sta.pos[0], rtcm.sta.pos[1], rtcm.sta.pos[2]);
  free_rtcm(&rtcm);
  report("rtcm3_1005", ok, why);
}

static void test_oem4_ionutc(const std::string &dir) {
  auto d = slurp(dir + "/oem4_ionutc.bin");
  raw_t raw;
  memset(&raw, 0, sizeof(raw));
  init_raw(&raw);
  int last = 0;
  for (unsigned char c : d) {
    int r = input_oem4(&raw, c);
    if (r) last = r;
  }
  bool ok = last == 9 && raw.nav.leaps == 18 && raw.nav.ion_gps[0] == 1.5e-8 &&
            raw.nav.ion_gps[7] == -327680.0 && raw.nav.utc_gps[2] == 589824 &&
            raw.nav.utc_gps[3] == 2100;
  char why[256];
  snprintf(why, sizeof why, "ret=%d leaps=%d ion0=%g utc2=%g", last, raw.nav.leaps,
           raw.nav.ion_gps[0], raw.nav.utc_gps[2]);
  free_raw(&raw);
  report("oem4_ionutc", ok, why);
}

static void test_oem4_fixture(const std::string &path) {
  auto d = slurp(path);
  raw_t raw;
  memset(&raw, 0, sizeof(raw));
  init_raw(&raw);
  long frames = 0, errors = 0;
  for (unsigned char c : d) {
    int r = input_oem4(&raw, c);
    if (r < 0) errors++;
    if (raw.nbyte == 0 && r != 0) frames++;
  }
  free_raw(&raw);
  // The fixture holds BESTGNSSPOS/VEL (1429/1430) long-header logs rtklib does
  // not decode plus short-header INS logs it must skip: nothing decodes, nothing errors.
  char why[128];
  snprintf(why, sizeof why, "bytes=%zu frames=%ld errors=%ld", d.size(), frames, errors);
  report("oem4_fixture", d.size() == 1070568 && errors == 0, why);
}

int main(int argc, char **argv) {
  if (argc == 3 && std::string(argv[1]) == "--emit-bridge") {
    std::vector<char> buf;
    serialize_header(&buf);
    std::vector<char> frame(buf);
    frame.resize(buf.size() + 952, 'P');
    std::ofstream(argv[2], std::ios::binary).write(frame.data(), frame.size());
    return 0;
  }
  std::string src = argc > 1 ? argv[1] : ".";
  test_bridge_roundtrip();
  test_bridge_flag();
  test_rtcm3_1005(src + "/mayhem/selftest/data");
  test_oem4_ionutc(src + "/mayhem/selftest/data");
  test_oem4_fixture(src + "/modules/drivers/gnss/test_data/novatel.bin");
  return g_fail ? 1 : 0;
}
