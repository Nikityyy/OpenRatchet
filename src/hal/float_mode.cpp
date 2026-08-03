#include "openratchet/float_mode.h"
#include <xmmintrin.h>
#include <pmmintrin.h>
#include <cmath>

void InitPS2FloatMode() {
    _MM_SET_FLUSH_ZERO_MODE(_MM_FLUSH_ZERO_ON);
    _MM_SET_DENORMALS_ZERO_MODE(_MM_DENORMALS_ZERO_ON);
    _MM_SET_ROUNDING_MODE(_MM_ROUND_TOWARD_ZERO);
}

float ClampPS2Float(float value) {
    if (std::isinf(value)) return (value > 0.0f) ? 3.402823466e+38f : -3.402823466e+38f;
    if (std::isnan(value)) return 0.0f;
    return value;
}
