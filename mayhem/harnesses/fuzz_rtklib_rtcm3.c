/* Fuzz the vendored RTKLIB RTCM v3 stream decoder (third_party/rtklib).
 * This is the decoder Apollo's GNSS driver feeds the base-station correction
 * stream through (modules/drivers/gnss/parser/rtcm3_parser.cc calls
 * input_rtcm3() one byte at a time). Same here: init, feed every byte,
 * free. */
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "third_party/rtklib/rtklib.h"

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
  static rtcm_t rtcm;
  memset(&rtcm, 0, sizeof(rtcm));
  if (!init_rtcm(&rtcm)) return 0;
  for (size_t i = 0; i < size; i++) {
    input_rtcm3(&rtcm, data[i]);
  }
  free_rtcm(&rtcm);
  return 0;
}
