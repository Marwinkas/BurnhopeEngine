// Filament color grade and GT7. Included after frame.slang and core/math.slang.

float3 gradeMul(float3x3 m, float3 v) {
    return mul(m, v);
}

float3 linearToLogC(float3 x) {
    return 0.244161 * log10(5.555556 * max(x, 0.0) + 0.047996) + 0.386036;
}

float3 logCToLinear(float3 x) {
    return (pow(10.0, (x - 0.386036) / 0.244161) - 0.047996) * (1.0 / 5.555556);
}

float3 nightAdapt(float3 v, float night) {
    const float3 L = float3(7.696847, 18.424824, 2.068096);
    const float3 M = float3(2.431137, 18.697937, 3.012463);
    const float3 S = float3(0.289117, 1.401833, 13.792292);
    const float3 R = float3(0.466386, 15.564362, 10.059963);
    const float3x3 toRgb = float3x3(
        0.18838381, -0.18656951, 0.01250249,
        -0.02425494, 0.07839353, -0.01348551,
        -0.00148370, -0.00405692, 0.07361284);
    v *= 380.0;
    const float4 q = float4(dot(v, L), dot(v, M), dot(v, S), dot(v, R));
    const float3 m = float3(0.63721, 0.39242, 1.6064);
    const float3 k = float3(0.2, 0.2, 0.3);
    const float3 g = rsqrt(1.0 + max((0.33 / m) * (q.xyz + k * q.w), 0.0));
    const float3x3 rod = float3x3(
        -1.04376893, 2.48473574, 0.0,
        0.52448329, 0.52442281, 0.84038845,
        8.74138824, 8.74038020, 0.0);
    const float3x3 opp = float3x3(
        -0.5, 0.0, 0.5,
        0.5, 0.0, 0.5,
        0.0, 1.0, 1.0);
    const float3 qHat = q.xyz + gradeMul(opp, gradeMul(rod, g * q.w * night));
    return gradeMul(toRgb, qHat) / 380.0;
}

float3 xyYToXyz(float x, float y) {
    const float a = 1.0 / max(y, 1e-5);
    return float3(x * a, 1.0, (1.0 - x - y) * a);
}

float3 whiteBalance(float3 v, float temperature, float tint) {
    const float k = temperature;
    const float x = 0.31271 - k * (k < 0.0 ? 0.0214 : 0.066);
    const float y = (2.87 * x - 3.0 * x * x - 0.275) + tint * 0.066;
    const float3x3 xyzToLms = float3x3(
        0.401288, 0.650173, -0.051461,
        -0.250268, 1.204414, 0.045854,
        -0.002079, 0.048952, 0.953127);
    const float3x3 lmsToXyz = float3x3(
        1.862068, -1.011255, 0.149187,
        0.387527, 0.621447, -0.008974,
        -0.015841, -0.034123, 1.049964);
    const float3x3 srgbToXyz = float3x3(
        0.4124560, 0.3575760, 0.1804380,
        0.2126730, 0.7151520, 0.0721750,
        0.0193339, 0.1191920, 0.9503040);
    const float3x3 xyzToSrgb = float3x3(
        3.2404542, -1.5371385, -0.4985314,
        -0.9692660, 1.8760108, 0.0415560,
        0.0556434, -0.2040259, 1.0572252);
    const float3 lms = gradeMul(xyzToLms, xyYToXyz(x, y));
    const float3 d65 = float3(0.975533, 1.016483, 1.084837);
    const float3 scale = d65 / max(lms, 1e-5);
    const float3 adapted = gradeMul(lmsToXyz, gradeMul(xyzToLms, gradeMul(srgbToXyz, v)) * scale);
    return max(gradeMul(xyzToSrgb, adapted), 0.0);
}

float pqEncode(float x) {
    const float N = 2610.0 / 4096.0 / 4.0;
    const float M = 2523.0 / 4096.0 * 128.0;
    const float C1 = 3424.0 / 4096.0;
    const float C2 = 2413.0 / 4096.0 * 32.0;
    const float C3 = 2392.0 / 4096.0 * 32.0;
    const float g = pow(max(x, 0.0) / 10000.0, N);
    return pow((C1 + C2 * g) / (1.0 + C3 * g), M);
}

float pqDecode(float x) {
    const float N = 2610.0 / 4096.0 / 4.0;
    const float M = 2523.0 / 4096.0 * 128.0;
    const float C1 = 3424.0 / 4096.0;
    const float C2 = 2413.0 / 4096.0 * 32.0;
    const float C3 = 2392.0 / 4096.0 * 32.0;
    const float g = pow(saturate(x), 1.0 / M);
    const float num = max(g - C1, 0.0);
    const float den = C2 - C3 * g;
    return pow(num / max(den, 1e-6), 1.0 / N) * 10000.0;
}

float3 rec2020ToICtCp(float3 x) {
    const float3 lms = float3(
        pqEncode((x.r * 1688.0 + x.g * 2146.0 + x.b * 262.0) / 4096.0),
        pqEncode((x.r * 683.0 + x.g * 2951.0 + x.b * 462.0) / 4096.0),
        pqEncode((x.r * 99.0 + x.g * 309.0 + x.b * 3688.0) / 4096.0));
    return float3(
        (2048.0 * lms.x + 2048.0 * lms.y) / 4096.0,
        (6610.0 * lms.x - 13613.0 * lms.y + 7003.0 * lms.z) / 4096.0,
        (17933.0 * lms.x - 17390.0 * lms.y - 543.0 * lms.z) / 4096.0);
}

float3 iCtCpToRec2020(float3 x) {
    const float3 linear = float3(
        pqDecode(x.r + 0.00860904 * x.g + 0.11103 * x.b),
        pqDecode(x.r - 0.00860904 * x.g - 0.11103 * x.b),
        pqDecode(x.r + 0.560031 * x.g - 0.320627 * x.b));
    return max(float3(
        3.43661 * linear.x - 2.50645 * linear.y + 0.0698454 * linear.z,
        -0.79133 * linear.x + 1.9836 * linear.y - 0.192271 * linear.z,
        -0.0259499 * linear.x - 0.0989137 * linear.y + 1.12486 * linear.z), 0.0);
}

float3 srgbToRec2020(float3 c) {
    const float3x3 m = float3x3(
        0.62750742, 0.32927771, 0.04330387,
        0.06910774, 0.91950443, 0.01135908,
        0.01639648, 0.08802434, 0.89551352);
    return max(gradeMul(m, max(c, 0.0)), 0.0);
}

float3 rec2020ToSrgb(float3 c) {
    const float3x3 m = float3x3(
        1.66021335, -0.58756431, -0.07282758,
        -0.12455204, 1.13294619, -0.00834833,
        -0.01815503, -0.10060479, 1.11883167);
    return max(gradeMul(m, max(c, 0.0)), 0.0);
}

float3 genericTone(float3 x, float contrast, float midIn, float midOut, float hdrMax) {
    const float a = pow(max(midIn, 1e-4), contrast);
    const float b = pow(max(hdrMax, midIn + 0.01), contrast);
    const float c = a - midOut * b;
    const float inputScale = (a * b * (midOut - 1.0)) / c;
    const float outputScale = midOut * (a - b) / c;
    const float3 xp = pow(max(x, 0.0), contrast);
    return outputScale * xp / (xp + inputScale);
}

float3 vibranceGrade(float3 v, float vibrance) {
    const float r = v.r - max(v.g, v.b);
    const float s = (vibrance - 1.0) / (1.0 + exp(-r * 3.0)) + 1.0;
    const float3 l = (1.0 - s) * float3(0.2126, 0.7152, 0.0722);
    return float3(
        dot(v, l + float3(s, 0.0, 0.0)),
        dot(v, l + float3(0.0, s, 0.0)),
        dot(v, l + float3(0.0, 0.0, s)));
}

float3 curvesGrade(float3 v, float shadowGamma, float midPoint, float highlightScale) {
    const float d = 1.0 / pow(max(midPoint, 1e-5), shadowGamma - 1.0);
    const float3 dark = pow(max(v, 0.0), shadowGamma) * d;
    const float3 light = highlightScale * (v - midPoint) + midPoint;
    return float3(
        v.r <= midPoint ? dark.r : light.r,
        v.g <= midPoint ? dark.g : light.g,
        v.b <= midPoint ? dark.b : light.b);
}

float3 oklabFromSrgb(float3 c) {
    const float3x3 toLms = float3x3(
        0.4122214708, 0.5363325363, 0.0514459929,
        0.2119034982, 0.6806995451, 0.1073969566,
        0.0883024619, 0.2817188376, 0.6299787005);
    const float3x3 toLab = float3x3(
        0.2104542553, 0.7936177850, -0.0040720468,
        1.9779984951, -2.4285922050, 0.4505937099,
        0.0259040371, 0.7827717662, -0.8086757660);
    return gradeMul(toLab, pow(max(gradeMul(toLms, max(c, 0.0)), 0.0), 1.0 / 3.0));
}

float3 srgbFromOklab(float3 c) {
    const float3x3 toLms = float3x3(
        1.0, 0.3963377774, 0.2158037573,
        1.0, -0.1055613458, -0.0638541728,
        1.0, -0.0894841775, -1.2914855480);
    const float3x3 toSrgb = float3x3(
        4.0767416621, -3.3077115913, 0.2309699292,
        -1.2684380046, 2.6097574011, -0.3413193965,
        -0.0041960863, -0.7034186147, 1.7076147010);
    const float3 lms = gradeMul(toLms, c);
    return gradeMul(toSrgb, lms * lms * lms);
}

float maxSaturation(float a, float b) {
    float k0, k1, k2, k3, k4, wl, wm, ws;
    if (-1.88170328 * a - 0.80936493 * b > 1.0) {
        k0 = 1.19086277; k1 = 1.76576728; k2 = 0.59662641; k3 = 0.75515197; k4 = 0.56771245;
        wl = 4.0767416621; wm = -3.3077115913; ws = 0.2309699292;
    } else if (1.81444104 * a - 1.19445276 * b > 1.0) {
        k0 = 0.73956515; k1 = -0.45954404; k2 = 0.08285427; k3 = 0.12541070; k4 = 0.14503204;
        wl = -1.2681437731; wm = 2.6097574011; ws = -0.3413193965;
    } else {
        k0 = 1.35733652; k1 = -0.00915799; k2 = -1.15130210; k3 = -0.50559606; k4 = 0.00692167;
        wl = -0.0041960863; wm = -0.7034186147; ws = 1.7076147010;
    }
    float S = k0 + k1 * a + k2 * b + k3 * a * a + k4 * a * b;
    const float kl = 0.3963377774 * a + 0.2158037573 * b;
    const float km = -0.1055613458 * a - 0.0638541728 * b;
    const float ks = -0.0894841775 * a - 1.2914855480 * b;
    const float l_ = 1.0 + S * kl;
    const float m_ = 1.0 + S * km;
    const float s_ = 1.0 + S * ks;
    const float l = l_ * l_ * l_;
    const float m = m_ * m_ * m_;
    const float s = s_ * s_ * s_;
    const float ld = 3.0 * kl * l_ * l_;
    const float md = 3.0 * km * m_ * m_;
    const float sd = 3.0 * ks * s_ * s_;
    const float ld2 = 6.0 * kl * kl * l_;
    const float md2 = 6.0 * km * km * m_;
    const float sd2 = 6.0 * ks * ks * s_;
    const float f = wl * l + wm * m + ws * s;
    const float f1 = wl * ld + wm * md + ws * sd;
    const float f2 = wl * ld2 + wm * md2 + ws * sd2;
    return S - f * f1 / (f1 * f1 - 0.5 * f * f2);
}

float3 gamutClip(float3 rgb) {
    if (rgb.r <= 1.03 && rgb.g <= 1.03 && rgb.b <= 1.03 && rgb.r >= -0.03 && rgb.g >= -0.03 && rgb.b >= -0.03) {
        return rgb;
    }
    const float3 lab = oklabFromSrgb(rgb);
    const float L = lab.x;
    const float C = max(1e-5, sqrt(lab.y * lab.y + lab.z * lab.z));
    const float a = lab.y / C;
    const float b = lab.z / C;
    const float Sc = maxSaturation(a, b);
    const float3 rgbMax = srgbFromOklab(float3(1.0, Sc * a, Sc * b));
    const float peak = max(rgbMax.r, max(rgbMax.g, rgbMax.b));
    const float Lc = pow(1.0 / max(peak, 1e-5), 1.0 / 3.0);
    const float Cc = Lc * Sc;
    const float Ld = L - 0.5;
    const float e1 = 0.5 + abs(Ld) + 0.05 * C;
    const float L0 = 0.5 * (1.0 + sign(Ld) * (e1 - sqrt(max(e1 * e1 - 2.0 * abs(Ld), 0.0))));
    float t;
    if (((L - L0) * Cc - (Lc - L0) * C) <= 0.0) {
        t = Cc * L0 / max(C * Lc + Cc * (L0 - L), 1e-5);
    } else {
        t = Cc * (L0 - 1.0) / max(C * (Lc - 1.0) + Cc * (L0 - L), 1e-5);
    }
    t = saturate(t);
    return srgbFromOklab(float3(L0 * (1.0 - t) + t * L, t * C * a, t * C * b));
}
