/* Fuzz the vendored RTKLIB NovAtel OEM4/OEM6 binary log decoder
 * (third_party/rtklib/novatel.c), the receiver raw-data path Apollo builds
 * into its GNSS stack. The input is the serial byte stream from the receiver:
 * init, feed every byte to input_oem4(), free. */
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "third_party/rtklib/rtklib.h"

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
  static raw_t raw;
  memset(&raw, 0, sizeof(raw));
  if (!init_raw(&raw)) return 0;
  for (size_t i = 0; i < size; i++) {
    input_oem4(&raw, data[i]);
  }
  free_raw(&raw);
  return 0;
}
