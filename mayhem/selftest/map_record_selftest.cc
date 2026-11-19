// Known-answer tests for the OpenDRIVE adapter and the Cyber RT record reader.
// Prints "PASS <name>" / "FAIL <name>: why"; mayhem/test.sh counts them.
#include <unistd.h>

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>

#include "glog/logging.h"
#include "cyber/proto/record.pb.h"
#include "cyber/record/file/record_file_writer.h"
#include "cyber/record/header_builder.h"
#include "cyber/record/record_message.h"
#include "cyber/record/record_reader.h"
#include "modules/drivers/lidar/velodyne/parser/calibration.h"
#include "modules/map/hdmap/adapter/opendrive_adapter.h"

static int g_fail = 0;
static void report(const char *name, bool ok, const std::string &why) {
  if (ok) {
    printf("PASS %s\n", name);
  } else {
    printf("FAIL %s: %s\n", name, why.c_str());
    g_fail++;
  }
}

static void test_opendrive(const std::string &xml) {
  apollo::hdmap::Map map;
  bool ok = apollo::hdmap::adapter::OpendriveAdapter::LoadData(xml, &map);
  std::string lane_id = map.lane_size() ? map.lane(0).id().id() : "";
  double x = 0, y = 0;
  if (map.lane_size() && map.lane(0).central_curve().segment_size() &&
      map.lane(0).central_curve().segment(0).line_segment().point_size()) {
    x = map.lane(0).central_curve().segment(0).line_segment().point(0).x();
    y = map.lane(0).central_curve().segment(0).line_segment().point(0).y();
  }
  // (-122.015, 37.41498) in UTM zone 10 (the header's north/west pick the zone).
  bool pos_ok = x > 587160 && x < 587170 && y > 4141360 && y < 4141370;
  char why[256];
  snprintf(why, sizeof why, "ok=%d roads=%d lanes=%d lane0=%s x=%.2f y=%.2f",
           ok, map.road_size(), map.lane_size(), lane_id.c_str(), x, y);
  report("opendrive_load", ok && map.road_size() == 1 && map.lane_size() == 1 &&
                               lane_id == "road_1_-1" && pos_ok,
         why);
  apollo::hdmap::Map bad;
  bool bad_ok = apollo::hdmap::adapter::OpendriveAdapter::LoadData(
      xml + ".does-not-exist", &bad);
  report("opendrive_missing_file", !bad_ok, "LoadData accepted a missing file");
}

static bool write_record(const std::string &path) {
  using namespace apollo::cyber::record;
  using apollo::cyber::proto::Channel;
  using apollo::cyber::proto::SingleMessage;
  RecordFileWriter w;
  if (!w.Open(path)) return false;
  auto hdr = HeaderBuilder::GetHeaderWithSegmentParams(0, 0);
  hdr.set_chunk_interval(0);
  hdr.set_chunk_raw_size(0);
  if (!w.WriteHeader(hdr)) return false;
  Channel chan;
  chan.set_name("/apollo/canbus/chassis");
  chan.set_message_type("apollo.canbus.Chassis");
  chan.set_proto_desc("desc0123456789");
  if (!w.WriteChannel(chan)) return false;
  for (int i = 0; i < 3; i++) {
    SingleMessage m;
    m.set_channel_name(chan.name());
    m.set_content("payload-" + std::to_string(i));
    m.set_time(1000000000ULL + i);
    if (!w.WriteMessage(m)) return false;
  }
  w.Close();
  return true;
}

static void test_record(const std::string &path) {
  using namespace apollo::cyber::record;
  if (!write_record(path)) return report("record_roundtrip", false, "write failed");
  RecordReader r(path);
  std::string got;
  RecordMessage msg;
  int n = 0;
  while (r.ReadMessage(&msg)) {
    got += msg.channel_name + "|" + msg.content + "|" + std::to_string(msg.time) + ";";
    n++;
  }
  std::string want =
      "/apollo/canbus/chassis|payload-0|1000000000;"
      "/apollo/canbus/chassis|payload-1|1000000001;"
      "/apollo/canbus/chassis|payload-2|1000000002;";
  bool ok = r.IsValid() && n == 3 && got == want &&
            r.GetMessageNumber("/apollo/canbus/chassis") == 3 &&
            r.GetMessageType("/apollo/canbus/chassis") == "apollo.canbus.Chassis" &&
            r.GetChannelList().size() == 1;
  report("record_roundtrip", ok, "valid=" + std::to_string(r.IsValid()) + " got=" + got);
}

static void test_calibration(const std::string &yaml) {
  apollo::drivers::velodyne::Calibration c;
  c.read(yaml);
  bool ok = c.initialized_ && c.num_lasers_ == 16 && c.laser_corrections_.size() == 16 &&
            std::fabs(c.laser_corrections_[0].vert_correction + 0.2617993877991494f) < 1e-6 &&
            std::fabs(c.laser_corrections_[1].vert_correction - 0.017453292519943295f) < 1e-6;
  char why[160];
  snprintf(why, sizeof why, "init=%d lasers=%d n=%zu", c.initialized_, c.num_lasers_,
           c.laser_corrections_.size());
  report("velodyne_vlp16_calibration", ok, why);
}

int main(int argc, char **argv) {
  FLAGS_minloglevel = 3;
  FLAGS_logtostderr = true;
  google::InitGoogleLogging(argv[0]);
  if (argc == 3 && std::string(argv[1]) == "--emit-record") {
    return write_record(argv[2]) ? 0 : 1;
  }
  std::string src = argc > 1 ? argv[1] : ".";
  const char *tmpdir = getenv("TMPDIR");
  std::string rec = std::string(tmpdir && *tmpdir ? tmpdir : "/tmp") +
                    "/selftest." + std::to_string(getpid()) + ".record";
  test_opendrive(src + "/mayhem/selftest/data/simple_road.xml");
  test_record(rec);
  test_calibration(src + "/modules/drivers/lidar/velodyne/params/VLP16_calibration.yaml");
  unlink(rec.c_str());
  return g_fail ? 1 : 0;
}
