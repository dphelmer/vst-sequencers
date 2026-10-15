#pragma once

#include "public.sdk/source/vst/vstparameters.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace Euclid {

constexpr int kNumChannels = 4;
constexpr int kMaxSteps = 32;
constexpr double kBeatsPerStep = 0.25; // 16th notes

enum ParamID : Steinberg::Vst::ParamID
{
	// per channel, ch in [0, kNumChannels): base + ch * kParamsPerChannel + k
	kPulses = 0,   // int 0..kMaxSteps
	kSteps,        // int 1..kMaxSteps
	kRotate,       // int 0..kMaxSteps-1
	kPitch,        // int 0..127
	kVelocity,     // int 0..127
	kGate,         // int 1..100 (percent of a step)
	kMute,         // On / Off
	kParamsPerChannel,

	// global
	kMidiChannel = 100, // int 1..16 (1-based, default 10 = GM drums)
};

// Plain-value semantics shared by processor (decode) and controller (display/automation).
// The SDK's default toString prints the raw normalized value (0..1), so the controller
// overrides the conversion methods with these.
inline int paramKind (Steinberg::Vst::ParamID id)
{
	if (id == kMidiChannel)
		return kMidiChannel;
	if (id < kNumChannels * kParamsPerChannel)
		return static_cast<int> (id) % kParamsPerChannel;
	return -1;
}

inline double plainFromNorm (Steinberg::Vst::ParamID id, double n)
{
	if (n < 0.)
		n = 0.;
	else if (n > 1.)
		n = 1.;
	switch (paramKind (id))
	{
		case kMidiChannel: return 1. + std::lround (n * 15.);            // 1..16
		case kPulses:      return std::lround (n * kMaxSteps);           // 0..32
		case kSteps:       return 1. + std::lround (n * (kMaxSteps - 1)); // 1..32
		case kRotate:      return std::lround (n * (kMaxSteps - 1));     // 0..31
		case kPitch:       return std::lround (n * 127.);                // 0..127
		case kVelocity:    return std::lround (n * 127.);                // 0..127
		case kGate:        return std::fmin (100., std::fmax (1., std::lround (n * 100.))); // 1..100 %
		case kMute:        return n >= 0.5 ? 1. : 0.;
		default:           return n;
	}
}

inline double normFromPlain (Steinberg::Vst::ParamID id, double p)
{
	switch (paramKind (id))
	{
		case kMidiChannel: return (std::fmin (16., std::fmax (1., p)) - 1.) / 15.;
		case kPulses:      return std::fmin (kMaxSteps, std::fmax (0., p)) / kMaxSteps;
		case kSteps:       return (std::fmin (kMaxSteps, std::fmax (1., p)) - 1.) / (kMaxSteps - 1);
		case kRotate:      return std::fmin (kMaxSteps - 1., std::fmax (0., p)) / (kMaxSteps - 1);
		case kPitch:       return std::fmin (127., std::fmax (0., p)) / 127.;
		case kVelocity:    return std::fmin (127., std::fmax (0., p)) / 127.;
		case kGate:        return std::fmin (100., std::fmax (1., p)) / 100.;
		case kMute:        return p >= 0.5 ? 1. : 0.;
		default:           return std::fmin (1., std::fmax (0., p));
	}
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
	switch (paramKind (id))
	{
		case kMute:
			std::snprintf (buf, bufSize, plain >= 0.5 ? "On" : "Off");
			return true;
		case kGate:
			std::snprintf (buf, bufSize, "%d%%", static_cast<int> (plain));
			return true;
		case -1:
			return false;
		default:
			std::snprintf (buf, bufSize, "%d", static_cast<int> (plain));
			return true;
	}
}

inline bool paramFromString (Steinberg::Vst::ParamID id, const char* str, double& norm)
{
	char lower[16];
	ieqLower (str, lower, sizeof (lower));
	if (paramKind (id) == kMute)
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
	char* end = nullptr;
	const double plain = std::strtod (lower, &end);
	if (end == lower)
		return false;
	norm = normFromPlain (id, plain); // trailing '%' from gate strings is ignored
	return true;
}

}
