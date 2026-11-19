// Fuzz Apollo's OpenDRIVE XML map loader:
// apollo::hdmap::adapter::OpendriveAdapter::LoadData(path, &map), the entry
// point that turns a vendor .xml HD map into Apollo's map proto (header,
// roads, lanes, junctions, signals, objects, then ProtoOrganizer). It takes a
// file path, so each input is written to a fresh temp file.
#include <unistd.h>

#include <cstdint>
#include <cstdlib>
#include <string>

#include "glog/logging.h"
#include "modules/map/hdmap/adapter/opendrive_adapter.h"

extern "C" int LLVMFuzzerInitialize(int *argc, char ***argv) {
  // Quiet glog once; read-only for every input after this.
  FLAGS_minloglevel = 3;
  FLAGS_logtostderr = true;
  google::InitGoogleLogging((*argv)[0]);
  return 0;
}

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
  const char *tmpdir = getenv("TMPDIR");
  std::string path =
      std::string(tmpdir && *tmpdir ? tmpdir : "/tmp") + "/odrXXXXXX";
  int fd = mkstemp(&path[0]);
  if (fd < 0) return 0;
  size_t off = 0;
  while (off < size) {
    ssize_t n = write(fd, data + off, size - off);
    if (n <= 0) break;
    off += static_cast<size_t>(n);
  }
  close(fd);
  apollo::hdmap::Map map;
  apollo::hdmap::adapter::OpendriveAdapter::LoadData(path, &map);
  unlink(path.c_str());
  return 0;
}
