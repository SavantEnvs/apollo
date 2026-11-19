// Fuzz the Cyber RT .record file reader (cyber/record): RecordReader opens the
// file (RecordFileReader: header section, index section at the offset the
// header names, channel sections) and ReadMessage() walks every chunk and
// message. Record files are recorded on one machine and replayed or analysed
// on another, so their bytes are untrusted. Each input goes to a temp file.
#include <unistd.h>

#include <cstdint>
#include <cstdlib>
#include <string>

#include "glog/logging.h"
#include "cyber/record/record_message.h"
#include "cyber/record/record_reader.h"

extern "C" int LLVMFuzzerInitialize(int *argc, char ***argv) {
  FLAGS_minloglevel = 3;
  FLAGS_logtostderr = true;
  google::InitGoogleLogging((*argv)[0]);
  return 0;
}

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
  const char *tmpdir = getenv("TMPDIR");
  std::string path =
      std::string(tmpdir && *tmpdir ? tmpdir : "/tmp") + "/recXXXXXX";
  int fd = mkstemp(&path[0]);
  if (fd < 0) return 0;
  size_t off = 0;
  while (off < size) {
    ssize_t n = write(fd, data + off, size - off);
    if (n <= 0) break;
    off += static_cast<size_t>(n);
  }
  close(fd);
  {
    apollo::cyber::record::RecordReader reader(path);
    if (reader.IsValid()) {
      for (const auto &channel : reader.GetChannelList()) {
        reader.GetMessageNumber(channel);
        reader.GetMessageType(channel);
        reader.GetProtoDesc(channel);
      }
      apollo::cyber::record::RecordMessage msg;
      while (reader.ReadMessage(&msg)) {
      }
    }
  }
  unlink(path.c_str());
  return 0;
}
