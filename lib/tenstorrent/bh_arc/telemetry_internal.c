#include <zephyr/kernel.h>

#include "telemetry_internal.h"
#include "pvt.h"
#include "regulator.h"

static int64_t last_update_time;
static TelemetryInternalData internal_data;

/**
 * @brief Read telemetry values that are shared by multiple components
 *
 * This function will update the cached TelemetryInternalData values if necessary.
 * Then return a copy of the values through the *data pointer.
 *
 * @param max_staleness Maximum time interval in milliseconds since the last update
 * @param data Pointer to the TelemetryInternalData struct to fill with the values
 */
void ReadTelemetryInternal(int64_t max_staleness, TelemetryInternalData *data) {
  int64_t reftime = last_update_time;
  if (k_uptime_delta(&reftime) >= max_staleness) {
    // Get all dynamically updated values
    internal_data.vcore_power = GetVcorePower();
    internal_data.vcore_current = GetVcoreCurrent();
    internal_data.asic_temperature = GetAvgChipTemp();

    // reftime was updated to the current uptime by the k_uptime_delta() call
    last_update_time = reftime;
  }

  *data = internal_data;
}