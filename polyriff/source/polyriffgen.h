// Pure pattern generator: bit-exact port of the data half of
// meshuggah-polymeter-core-live-step.js (mulberry32 + makeGroups + computeRiff +
// the two slide-roll streams). No host types, no allocation -- safe to call from
// process(). RNG draw order matters: same seed+params must give the same riff as
// the Max JS (covered by golden tables generated from the original file in
// tools/gen_golden.js and checked by PolyriffSim).
#pragma once

#include "params.h"

#include <cmath>
#include <cstdint>
#include <cstring>

namespace Polyriff {

// mulberry32. All JS ops in the original (|0, >>>, Math.imul polyfill) are uint32
// wrap-arithmetic, so the uint32 port is bit-identical to the SpiderMonkey output.
struct Rng32
{
	std::uint32_t s;
	explicit Rng32 (std::uint32_t seed) : s (seed) {}
	double operator() ()
	{
		s += 0x6d2b79f5u;
		std::uint32_t t = (s ^ (s >> 15)) * (1u | s);
		t = (t + ((t ^ (t >> 7)) * (61u | t))) ^ t;
		return static_cast<double> (t ^ (t >> 14)) / 4294967296.0;
	}
};

// Everything that feeds pattern generation (hyperBars/chan/gate only affect the
// processor, but living here keeps one defaults source).
struct GenParams
{
	std::uint32_t seed {12345};
	int riffLen {21};
	int hyperBars {8};
	int density {70};
	int groupSpread {50};
	int pitchLow {28};
	int pitchRange {3};
	int pitchDensity {30};
	int kickMode {1};
	int accentVel {127};
	int gatePct {100};
	int slidePct {0};
	int riffChannel {1}; // 1-based MIDI channel
	int kickChannel {2};
};

inline GenParams defaultGenParams ()
{
	GenParams g;
	g.seed = static_cast<std::uint32_t> (defaultPlain (kSeed));
	g.riffLen = static_cast<int> (defaultPlain (kRiffLen));
	g.hyperBars = static_cast<int> (defaultPlain (kHyperBars));
	g.density = static_cast<int> (defaultPlain (kDensity));
	g.groupSpread = static_cast<int> (defaultPlain (kGroupSpread));
	g.pitchLow = static_cast<int> (defaultPlain (kPitchLow));
	g.pitchRange = static_cast<int> (defaultPlain (kPitchRange));
	g.pitchDensity = static_cast<int> (defaultPlain (kPitchDensity));
	g.kickMode = static_cast<int> (defaultPlain (kKickMode));
	g.accentVel = static_cast<int> (defaultPlain (kAccentVel));
	g.gatePct = static_cast<int> (defaultPlain (kGatePct));
	g.slidePct = static_cast<int> (defaultPlain (kSlidePct));
	g.riffChannel = static_cast<int> (defaultPlain (kRiffChannel));
	g.kickChannel = static_cast<int> (defaultPlain (kKickChannel));
	return g;
}

// Source-indexed riff (i = riff index, NOT transport slot): the processor plays
// index idx = (S - rotShift) mod n at sixteenth S. Slide flags are stored under
// the same source index -- the JS draws them in display order (depends on
// rotShift), so they must be rebuilt whenever the rotation changes.
struct Pattern
{
	int n {21};
	std::uint8_t pitch[kMaxSteps] {};   // valid when hit
	std::uint8_t riffVel[kMaxSteps] {}; // 0 = rest
	std::uint8_t kickVel[kMaxSteps] {}; // 0 = rest
	bool hit[kMaxSteps] {};
	bool accent[kMaxSteps] {};
	bool riffSlide[kMaxSteps] {};
	bool kickSlide[kMaxSteps] {};
};

// Partition N into uneven group sizes (Meshuggah-style). Returns group count.
inline int makeGroups (int n, int spread, Rng32& rng, int* sizes)
{
	if (n <= 0)
		return 0;
	int count = 0;
	int remaining = n;
	const double evenness = 1.0 - spread / 100.0; // 1 = even, 0 = chaotic
	while (remaining > 0)
	{
		int base = 2 + static_cast<int> (std::floor (rng () * 2)); // 2-3
		if (evenness > 0.5) // spread < 50: fewer, longer groups
			base += 1;
		const int jitter = static_cast<int> (std::floor (rng () * (1.0 + spread / 25.0)));
		int g = base + jitter;
		if (g > remaining)
			g = remaining;
		if (remaining - g == 1 && count > 0)
			g = remaining; // swallow a trailing singleton
		sizes[count++] = g;
		remaining -= g;
	}
	return count;
}

inline void generatePattern (Pattern& p, const GenParams& gp, int rotShift)
{
	Pattern out {};
	const int n = gp.riffLen < 4 ? 4 : (gp.riffLen > kMaxSteps ? kMaxSteps : gp.riffLen);
	out.n = n;

	Rng32 rng (gp.seed);

	int sizes[kMaxSteps];
	bool groupStart[kMaxSteps] = {};
	const int nGroups = makeGroups (n, gp.groupSpread, rng, sizes);
	int acc = 0;
	for (int g = 0; g < nGroups; ++g)
	{
		groupStart[acc] = true;
		acc += sizes[g];
	}

	for (int i = 0; i < n; ++i)
	{
		bool hit = rng () * 100.0 < gp.density; // density draw happens for every step
		if (groupStart[i])
			hit = true; // group starts always hit (the "one" of each grouping)
		const bool accent = groupStart[i];
		std::uint8_t vel = 0;
		if (hit)
			vel = accent ? static_cast<std::uint8_t> (gp.accentVel)
			             : static_cast<std::uint8_t> (std::lround (gp.accentVel * 0.75));
		std::uint8_t pitch = static_cast<std::uint8_t> (gp.pitchLow);
		if (hit && gp.pitchRange > 0 && rng () * 100.0 < gp.pitchDensity)
		{
			const int shifted =
			    gp.pitchLow + 1 + static_cast<int> (std::floor (rng () * gp.pitchRange));
			pitch = static_cast<std::uint8_t> (shifted > 127 ? 127 : shifted);
		}
		out.hit[i] = hit;
		out.accent[i] = accent;
		out.riffVel[i] = vel;
		out.pitch[i] = pitch;
	}

	for (int i = 0; i < n; ++i)
	{
		std::uint8_t v = 0;
		if (gp.kickMode == 1 && out.hit[i])
			v = out.accent[i] ? static_cast<std::uint8_t> (gp.accentVel)
			                  : static_cast<std::uint8_t> (std::lround (gp.accentVel * 0.8));
		else if (gp.kickMode == 2 && out.accent[i])
			v = static_cast<std::uint8_t> (gp.accentVel);
		out.kickVel[i] = v;
	}

	// Slide rolls: separate streams (seed ^ 0x511de riff, seed ^ 0x0acc kick), drawn in
	// display order i = 0..n-1 with src = (i - shift) mod n -- exactly the pushAll loops.
	const int shift = ((rotShift % n) + n) % n;
	if (gp.slidePct > 0)
	{
		Rng32 slideRng (gp.seed ^ 0x511deu);
		for (int i = 0; i < n; ++i)
		{
			const int src = ((i - shift) % n + n) % n;
			if (out.hit[src] && out.hit[(src + 1) % n])
				out.riffSlide[src] = slideRng () * 100.0 < gp.slidePct;
		}
		Rng32 kickSlideRng (gp.seed ^ 0x0accu);
		for (int i = 0; i < n; ++i)
		{
			const int src = ((i - shift) % n + n) % n;
			if (out.kickVel[src] > 0 && out.hit[(src + 1) % n])
				out.kickSlide[src] = kickSlideRng () * 100.0 < gp.slidePct;
		}
	}

	p = out;
}

}
