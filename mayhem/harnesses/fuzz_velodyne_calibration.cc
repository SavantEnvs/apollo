// Fuzz the Velodyne lidar calibration loader:
// apollo::drivers::velodyne::Calibration::read(path), which parses the
// per-laser correction YAML a driver is configured with (yaml-cpp, then the
// node -> LaserCorrection extraction in calibration.cc). It takes a path, so
// each input is written to a fresh temp file.
#include <unistd.h>

#include <cstdint>
#include <cstdlib>
#include <string>

#include "modules/drivers/lidar/velodyne/parser/calibration.h"

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
  const char *tmpdir = getenv("TMPDIR");
  std::string path =
      std::string(tmpdir && *tmpdir ? tmpdir : "/tmp") + "/calXXXXXX.yaml";
  int fd = mkstemps(&path[0], 5);
  if (fd < 0) return 0;
  size_t off = 0;
  while (off < size) {
    ssize_t n = write(fd, data + off, size - off);
    if (n <= 0) break;
    off += static_cast<size_t>(n);
  }
  close(fd);
  apollo::drivers::velodyne::Calibration calibration;
  calibration.read(path);
  unlink(path.c_str());
  return 0;
}
