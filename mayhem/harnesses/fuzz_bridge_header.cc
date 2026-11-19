// Fuzz the UDP bridge datagram header parser (apollo::bridge::BridgeHeader).
//
// Mirrors the framing in UDPBridgeReceiverComponent<T>::MsgHandle
// (modules/bridge/udp_bridge_receiver_component.cc): the input is one UDP
// datagram. The receiver reads at most 2*FRAME_SIZE bytes into a zeroed
// buffer, checks the "ApolloBridgeHeader" flag, reads the header size that
// follows it, bounds it to [offset, FRAME_SIZE] and hands the rest of the
// header to BridgeHeader::Diserialize, then validates the frame fields.
//
// The datagram is copied into a heap buffer of exactly the bytes the
// receiver would look at (the datagram, zero-padded up to the declared header
// size like the receiver's zeroed stack buffer) so ASan catches any read past
// what the header claims.
#include <cstdint>
#include <cstring>
#include <vector>

#include "modules/bridge/common/bridge_header.h"
#include "modules/bridge/common/macro.h"

using apollo::bridge::BRIDGE_HEADER_FLAG;
using apollo::bridge::BridgeHeader;
using apollo::bridge::FRAME_SIZE;
using apollo::bridge::HEADER_FLAG_SIZE;
using apollo::bridge::hsize;

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
  // recvfrom() with a 2*FRAME_SIZE buffer: bytes <= 0 or larger are rejected.
  if (size == 0 || size > 2 * FRAME_SIZE) return -1;
  const size_t min = sizeof(BRIDGE_HEADER_FLAG) + 1 + sizeof(hsize) + 1;
  // The receiver memcpy()s the flag and the size field out of its fixed
  // buffer; a datagram must carry at least those bytes.
  if (size < min) return -1;

  // Same flag check as MsgHandle (strcmp of the NUL-terminated flag copy).
  char header_flag[sizeof(BRIDGE_HEADER_FLAG) + 1] = {0};
  memcpy(header_flag, data, HEADER_FLAG_SIZE);
  if (strcmp(header_flag, BRIDGE_HEADER_FLAG) != 0) return 0;

  size_t offset = sizeof(BRIDGE_HEADER_FLAG) + 1;
  hsize header_size;
  memcpy(&header_size, data + offset, sizeof(hsize));
  offset += sizeof(hsize) + 1;
  if (header_size > FRAME_SIZE || header_size < offset) return 0;

  std::vector<char> total_buf(size > header_size ? size : header_size, 0);
  memcpy(total_buf.data(), data, size);

  BridgeHeader header;
  if (!header.IsAvailable(total_buf.data())) return 0;
  if (!header.Diserialize(total_buf.data() + offset, header_size - offset)) {
    return 0;
  }

  // The fields MsgHandle consults next.
  volatile size_t sink = header.GetMsgName().size();
  sink += header.GetMsgID();
  sink += header.GetTotalFrames();
  sink += header.GetIndex();
  volatile double ts = header.GetTimeStamp();
  (void)ts;
  if (header.GetFramePos() > header.GetMsgSize()) return 0;
  if (header.GetFrameSize() > (2 * FRAME_SIZE - header_size)) return 0;
  (void)sink;
  return 0;
}
