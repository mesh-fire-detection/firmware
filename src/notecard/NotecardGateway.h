#pragma once

#include "configuration.h"

#if defined(MESH_FIRE_DETECTION) && defined(ARCH_NRF52) && !MESHTASTIC_EXCLUDE_I2C
#include "mesh/generated/meshtastic/mesh.pb.h"

void initNotecardGateway();
void queueNotecardTelemetry(const meshtastic_MeshPacket &encrypted, const meshtastic_MeshPacket &decoded, uint8_t channelIndex);
#endif
