#pragma once

#include "../../Utils/DirectXMathCompat.hpp"

namespace burnhope {

struct ProbeData {
  float4 positionAndRadius;
};

struct ProbesInfo {
  int count;
  ProbeData data[16];
};

struct DecalDataGpu {
  float4x4 invModelMatrix;
  float4 params;
};

struct DecalBlock {
  int decalCount;
  int pad[3];
  DecalDataGpu decals[1000];
};

} // namespace burnhope
