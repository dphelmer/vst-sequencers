#pragma once

#include "public.sdk/source/vst/vstparameters.h"

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace Polyriff {

constexpr int kMaxSteps = 64;          // riff length cap (live.step clamps nstep to 64)
constexpr double kBeatsPerStep = 0.25; // 16th notes
constexpr double kSeedMax = 4294967295.0;
constexpr int kKickPitch = 36; // C1 (patcher constant)

enum ParamID : Steinberg::Vst::ParamID
{
	kSeed = 0, // int 0..4294967295; typed text = number, else FNV-1a 32 of the text
	kRiffLen,      // int 4..64  (N, riff length in 16ths)
	kHyperBars,    // int 1..64  (L, truncate every L bars)
	kDensity,      // int 0..100 (%)
	kGroupSpread,  // int 0..100 (%)
	kPitchLow,     // int 0..127
	kPitchRange,   // int 0..24 (semitones above low)
	kPitchDensity, // int 0..100 (%)
	kKickMode,     // 0=Off 1=Follow 2=Accent
	kAccentVel,    // int 1..127
	kGatePct,      // int 50..200 (% of a 16th)
	kSlidePct,     // int 0..100 (%)
	kRiffChannel,  // int 1..16 (emitted riff lane)
	kKickChannel,  // int 1..16 (emitted kick lane)
	kRandomize,    // momentary trigger (controller turns it into a new kSeed)
	kNumParams
};

// Plain-value semantics shared by processor (decode) and controller (display/automation).
// The SDK's default toString prints the raw normalized value (0..1), so the controller
// overrides the conversion methods with these.

inline double defaultPlain (Steinberg::Vst::ParamID id)
{
	switch (id)
	{
		case kSeed:        return 12345.;
		case kRiffLen:     return 21.;
		case kHyperBars:   return 8.;
		case kDensity:     return 70.;
		case kGroupSpread: return 50.;
		case kPitchLow:    return 28.;
		case kPitchRange:  return 3.;
		case kPitchDensity:return 30.;
		case kKickMode:    return 1.;
		case kAccentVel:   return 127.;
		case kGatePct:     return 100.;
		case kSlidePct:    return 0.;
		case kRiffChannel: return 1.;
		case kKickChannel: return 2.;
		case kRandomize:   return 0.;
		default:           return 0.;
	}
}

// Host parameter granularity (stepCount): integer params expose max-min steps so the
// normalized grid lands exactly on the plain integers. kSeed overflows int32 as a step
// count (4294967295) and stays continuous; plainFromNorm rounds to the integer anyway.
inline int paramStepCount (Steinberg::Vst::ParamID id)
{
	switch (id)
	{
		case kSeed:        return 0;
		case kRiffLen:     return 60;
		case kHyperBars:   return 63;
		case kDensity:     return 100;
		case kGroupSpread: return 100;
		case kPitchLow:    return 127;
		case kPitchRange:  return 24;
		case kPitchDensity:return 100;
		case kKickMode:    return 2;
		case kAccentVel:   return 126;
		case kGatePct:     return 150;
		case kSlidePct:    return 100;
		case kRiffChannel: return 15;
		case kKickChannel: return 15;
		case kRandomize:   return 1;
		default:           return 0;
	}
}

inline double plainFromNorm (Steinberg::Vst::ParamID id, double n)
{
	if (n < 0.)
		n = 0.;
	else if (n > 1.)
		n = 1.;
	switch (id)
	{
		case kSeed:        return static_cast<double> (static_cast<std::uint32_t> (std::llround (n * kSeedMax)));
		case kRiffLen:     return 4. + std::lround (n * 60.);
		case kHyperBars:   return 1. + std::lround (n * 63.);
		case kDensity:
		case kGroupSpread:
		case kPitchDensity:
		case kSlidePct:    return static_cast<double> (std::lround (n * 100.));
		case kPitchLow:    return static_cast<double> (std::lround (n * 127.));
		case kPitchRange:  return static_cast<double> (std::lround (n * 24.));
		case kKickMode:    return static_cast<double> (std::lround (n * 2.));
		case kAccentVel:   return 1. + std::lround (n * 126.);
		case kGatePct:     return 50. + std::lround (n * 150.);
		case kRiffChannel:
		case kKickChannel: return 1. + std::lround (n * 15.);
		case kRandomize:   return n >= 0.5 ? 1. : 0.;
		default:           return n;
	}
}

inline double normFromPlain (Steinberg::Vst::ParamID id, double p)
{
	const auto cl = [] (double v, double lo, double hi) { return v < lo ? lo : (v > hi ? hi : v); };
	switch (id)
	{
		case kSeed:        return cl (p, 0., kSeedMax) / kSeedMax;
		case kRiffLen:     return (cl (p, 4., 64.) - 4.) / 60.;
		case kHyperBars:   return (cl (p, 1., 64.) - 1.) / 63.;
		case kDensity:
		case kGroupSpread:
		case kPitchDensity:
		case kSlidePct:    return cl (p, 0., 100.) / 100.;
		case kPitchLow:    return cl (p, 0., 127.) / 127.;
		case kPitchRange:  return cl (p, 0., 24.) / 24.;
		case kKickMode:    return cl (p, 0., 2.) / 2.;
		case kAccentVel:   return (cl (p, 1., 127.) - 1.) / 126.;
		case kGatePct:     return (cl (p, 50., 200.) - 50.) / 150.;
		case kRiffChannel:
		case kKickChannel: return (cl (p, 1., 16.) - 1.) / 15.;
		case kRandomize:   return p >= 0.5 ? 1. : 0.;
		default:           return cl (p, 0., 1.);
	}
}

// FNV-1a 32-bit: seed text that is not a plain number hashes deterministically.
inline std::uint32_t fnv1a32 (const char* s)
{
	std::uint32_t h = 2166136261u;
	for (const unsigned char* p = reinterpret_cast<const unsigned char*> (s); *p; ++p)
	{
		h ^= static_cast<std::uint32_t> (*p);
		h *= 16777619u;
	}
	return h;
}

inline void ieqLower (const char* s, char* dst, int dstSize)
{
	int i = 0;
	for (; s[i] && i + 1 < dstSize; ++i)
	{
		char c = s[i];
		if (c >= 'A' && c <= 'Z')
			c += 32;
		dst[i] = c;
	}
	dst[i] = 0;
}

inline bool paramToString (Steinberg::Vst::ParamID id, double norm, char* buf, int bufSize)
{
	const double plain = plainFromNorm (id, norm);
	switch (id)
	{
		case kSeed:
			std::snprintf (buf, bufSize, "%u",
			               static_cast<unsigned> (static_cast<std::uint32_t> (plain)));
			return true;
		case kKickMode:
		{
			const int m = static_cast<int> (plain);
			std::snprintf (buf, bufSize, m == 0 ? "Off" : m == 1 ? "Follow" : "Accent");
			return true;
		}
		case kGatePct:
			std::snprintf (buf, bufSize, "%d%%", static_cast<int> (plain));
			return true;
		case kRandomize:
			std::snprintf (buf, bufSize, plain >= 0.5 ? "On" : "Off");
			return true;
		default:
			std::snprintf (buf, bufSize, "%d", static_cast<int> (plain));
			return true;
	}
}

inline bool paramFromString (Steinberg::Vst::ParamID id, const char* str, double& norm)
{
	if (id == kSeed)
	{
		// Numeric string -> value; anything else -> FNV-1a of the typed text.
		const char* p = str;
		while (*p == ' ' || *p == '\t')
			++p;
		if (*p >= '0' && *p <= '9')
		{
			char* end = nullptr;
			const unsigned long long v = std::strtoull (p, &end, 10);
			while (*end == ' ' || *end == '\t' || *end == '\r' || *end == '\n')
				++end;
			if (*end == 0)
			{
				const std::uint32_t seed = v > static_cast<unsigned long long> (kSeedMax)
				                               ? static_cast<std::uint32_t> (kSeedMax)
				                               : static_cast<std::uint32_t> (v);
				norm = static_cast<double> (seed) / kSeedMax;
				return true;
			}
		}
		norm = static_cast<double> (fnv1a32 (str)) / kSeedMax;
		return true;
	}

	char lower[16];
	ieqLower (str, lower, sizeof (lower));
	if (id == kRandomize)
	{
		if (std::strcmp (lower, "on") == 0 || std::strcmp (lower, "1") == 0)
		{
			norm = 1.;
			return true;
		}
		if (std::strcmp (lower, "off") == 0 || std::strcmp (lower, "0") == 0)
		{
			norm = 0.;
			return true;
		}
	}
	if (id == kKickMode && std::strcmp (lower, "off") == 0)
	{
		norm = 0.;
		return true;
	}
	if (id == kKickMode && std::strcmp (lower, "follow") == 0)
	{
		norm = normFromPlain (kKickMode, 1.);
		return true;
	}
	if (id == kKickMode && std::strcmp (lower, "accent") == 0)
	{
		norm = normFromPlain (kKickMode, 2.);
		return true;
	}
	char* end = nullptr;
	const double plain = std::strtod (lower, &end);
	if (end == lower)
		return false;
	norm = normFromPlain (id, plain); // trailing '%' from gate strings is ignored
	return true;
}

}
