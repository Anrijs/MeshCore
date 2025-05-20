#pragma once

#include <helpers/SimpleMeshTables.h>

class LoggerMeshTables : public SimpleMeshTables {
public:
  bool hasSeen(const mesh::Packet* pkt) override {
    return false; // This will allow logging same packet from multiple sources
  }

  bool hasSeen2(const mesh::Packet* pkt) {
    // if hasSeen2 returns true, change action to release
    return SimpleMeshTables::hasSeen(pkt);
  }
};
