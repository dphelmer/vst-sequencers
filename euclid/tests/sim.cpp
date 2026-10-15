// Offline simulation: drives EuclidProcessor::process() with a fake transport. Not a plug-in;
// build with `cmake --build build --target EuclidSim`.
#include "euclidprocessor.h"
#include "euclidean.h"

#include "public.sdk/source/vst/hosting/eventlist.h"
#include "pluginterfaces/vst/ivstprocesscontext.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

using namespace Steinberg;
using namespace Steinberg::Vst;

static int failures = 0;
#define CHECK(c) do { if (!(c)) { std::printf ("CHECK FAILED line %d: %s\n", __LINE__, #c); ++failures; } } while (0)

static void checkPattern (int pulses, int steps, int rot, const char* expect)
{
	char got[64] = {};
	for (int i = 0; i < steps; ++i)
		got[i] = Euclid::euclideanHit (pulses, steps, rot, i) ? 'x' : '.';
	got[steps] = 0;
	if (std::strcmp (got, expect) != 0)
	{
		std::printf ("pattern E(%d,%d) rot %d: got %s want %s\n", pulses, steps, rot, got, expect);
		++failures;
	}
}

int main ()
{
	// ---- 1) pattern unit tests
	checkPattern (3, 8, 0, "x..x..x.");
	checkPattern (5, 8, 0, "x.x.xx.x");
	checkPattern (2, 8, 0, "x...x...");
	checkPattern (2, 16, 4, "....x.......x...");
	checkPattern (4, 16, 0, "x...x...x...x...");
	checkPattern (7, 16, 0, "x..x.x.x..x.x.x.");
	checkPattern (0, 16, 0, "................");
	checkPattern (16, 16, 0, "xxxxxxxxxxxxxxxx");
	checkPattern (5, 8, 3, "x.xx.x.x");
	checkPattern (3, 16, 0, "x.....x....x....");
	checkPattern (5, 16, 7, ".x..x..x...x..x.");

	// ---- 2) transport sim
	Euclid::EuclidProcessor proc;
	proc.initialize (nullptr);
	ProcessSetup setup {kRealtime, kSample32, 256, 48000.};
	proc.setupProcessing (setup);
	proc.setActive (true);
	proc.setProcessing (true);

	const int N = 256;
	std::vector<float> l (N), r (N);
	float* chans[2] = {l.data (), r.data ()};
	AudioBusBuffers outBus;
	outBus.numChannels = 2;
	outBus.channelBuffers32 = chans;

	EventList outEvents (256), inEvents (8);
	ProcessContext ctx {};
	ProcessData data;
	data.numSamples = N;
	data.numOutputs = 1;
	data.outputs = &outBus;
	data.outputEvents = &outEvents;
	data.processContext = &ctx;

	const double tempo = 120., sr = 48000.;
	const double ppqPerSample = tempo / 60. / sr;
	const double ppqPerBlock = N * ppqPerSample;
	ctx.sampleRate = sr;
	ctx.tempo = tempo;
	ctx.timeSigNumerator = 4;
	ctx.timeSigDenominator = 4;

	struct Ev { double ppq; int type; int pitch; int channel; };
	std::vector<Ev> evs;
	auto run = [&] (double ppq, bool playing, int n = N) {
		ctx.state = ProcessContext::kTempoValid | ProcessContext::kProjectTimeMusicValid |
		            ProcessContext::kTimeSigValid | (playing ? ProcessContext::kPlaying : 0);
		ctx.projectTimeMusic = ppq;
		data.numSamples = n;
		outEvents.clear ();
		proc.process (data);
		for (int i = 0; i < outEvents.getEventCount (); ++i)
		{
			Event e;
			outEvents.getEvent (i, e);
			evs.push_back ({e.ppqPosition, e.type,
			                e.type == Event::kNoteOnEvent ? e.noteOn.pitch : e.noteOff.pitch,
			                e.type == Event::kNoteOnEvent ? e.noteOn.channel : e.noteOff.channel});
		}
	};

	// -- defaults: ch0 kick 4/16 pitch 36, ch1 snare 2/16 rot 4 pitch 38,
	//    ch2 hats 8/16 pitch 42, ch3 open hat 3/16 pitch 46. One bar = 16 steps = 4 beats.
	// Count events with ppq in [0, 4): beat 4.0 is the next bar's downbeat.
	evs.clear ();
	double ppq = 0.;
	for (; ppq < 4.0 + ppqPerBlock; ppq += ppqPerBlock)
		run (ppq, true);
	{
		int kick = 0, snare = 0, hat = 0, open = 0;
		for (auto& e : evs)
			if (e.type == Event::kNoteOnEvent && e.ppq < 4.0)
			{
				if (e.pitch == 36) ++kick;
				else if (e.pitch == 38) ++snare;
				else if (e.pitch == 42) ++hat;
				else if (e.pitch == 46) ++open;
			}
		std::printf ("1 bar: kick=%d snare=%d hat=%d open=%d\n", kick, snare, hat, open);
		CHECK (kick == 4);  // E(4,16): steps 0,4,8,12
		CHECK (snare == 2); // E(2,16) rot 4: steps 4,12
		CHECK (hat == 8);   // E(8,16): every other step
		CHECK (open == 3);  // E(3,16)
		for (auto& e : evs)
			CHECK (e.channel == 9); // all on MIDI ch 10 (0-based 9)
		// kick onsets at exact beats 0,1,2,3
		std::vector<double> kickPpq;
		for (auto& e : evs)
			if (e.type == Event::kNoteOnEvent && e.pitch == 36 && e.ppq < 4.0)
				kickPpq.push_back (e.ppq);
		CHECK (kickPpq.size () == 4);
		for (int i = 0; i < 4; ++i)
			CHECK (kickPpq[i] == i * 1.0);
	}

	// -- stop flushes held notes
	evs.clear ();
	proc.setProcessing (false);
	proc.setProcessing (true);
	run (0.5, true);  // step boundary: hat hits even step indices (hat E(8,16) rot 0: steps 0,2,4..)
	run (0.55, true);
	run (0.6, false); // stop -> flush
	{
		int ons = 0, offs = 0;
		for (auto& e : evs)
		{
			if (e.type == Event::kNoteOnEvent) ++ons;
			if (e.type == Event::kNoteOffEvent) ++offs;
		}
		CHECK (ons > 0);
		CHECK (offs >= ons);
	}

	// -- restart: defaults come back, kick fires 4 times per bar
	proc.setProcessing (false);
	proc.setProcessing (true);
	evs.clear ();
	ppq = 0.;
	for (; ppq < 4.0 + ppqPerBlock; ppq += ppqPerBlock)
		run (ppq, true);
	{
		int kick = 0;
		for (auto& e : evs)
			if (e.type == Event::kNoteOnEvent && e.pitch == 36 && e.ppq < 4.0)
				++kick;
		CHECK (kick == 4);
	}

	// -- loop wrap: loop 0..2 beats, split block at end (same pattern as ProbeSim)
	evs.clear ();
	proc.setProcessing (false);
	proc.setProcessing (true);
	ctx.cycleStartMusic = 0.;
	ctx.cycleEndMusic = 2.;
	for (int pass = 0; pass < 3; ++pass)
	{
		double p = 0.;
		while (p + N * ppqPerSample < 2.0)
		{
			run (p, true);
			p += N * ppqPerSample;
		}
		int n = static_cast<int> ((2.0 - p) / ppqPerSample) + 1;
		run (p, true, n); // short block overshooting loop end; next starts at 0
	}
	{
		int ons = 0, offs = 0;
		for (auto& e : evs)
		{
			if (e.type == Event::kNoteOnEvent) ++ons;
			if (e.type == Event::kNoteOffEvent) ++offs;
		}
		CHECK (ons > 0);
		CHECK (offs >= ons - 4); // held notes at wrap may be flushed on the next jump-start;
		                         // offs can lag by at most the active count (4)
		std::printf ("loop wrap ons=%d offs=%d\n", ons, offs);
	}

	proc.setProcessing (false);
	proc.setActive (false);

	// ---- 3) parameter display strings: normalized -> text -> normalized
	{
		using namespace Euclid;
		struct { Euclid::ParamID id; double norm; const char* want; } cases[] = {
			{kPulses, 4. / kMaxSteps, "4"},
			{kSteps, 15. / (kMaxSteps - 1), "16"},
			{kRotate, 7. / (kMaxSteps - 1), "7"},
			{kPitch, 36. / 127., "36"},
			{kVelocity, 110. / 127., "110"},
			{kGate, 0.5, "50%"},
			{kGate, 1.0, "100%"},
			{kMute, 1.0, "On"},
			{kMute, 0.0, "Off"},
			{kMidiChannel, 9. / 15., "10"},
		};
		for (auto& c : cases)
		{
			char buf[32];
			paramToString (c.id, c.norm, buf, sizeof (buf));
			CHECK (std::strcmp (buf, c.want) == 0);
			double back = -1.;
			CHECK (paramFromString (c.id, buf, back));
			CHECK (std::abs (back - c.norm) < 1e-9);
		}
		// typed-in values the host may receive
		double v = -1.;
		CHECK (paramFromString (kSteps, "16", v) && std::abs (plainFromNorm (kSteps, v) - 16.) < 1e-9);
		CHECK (paramFromString (kGate, "50%", v) && std::abs (plainFromNorm (kGate, v) - 50.) < 1e-9);
		CHECK (paramFromString (kMidiChannel, "10", v) && std::abs (plainFromNorm (kMidiChannel, v) - 10.) < 1e-9);
		CHECK (!paramFromString (kMute, "maybe", v));
		// out-of-range plain values clamp instead of exploding
		CHECK (std::abs (normFromPlain (kPulses, 99.) - 1.0) < 1e-9);
		CHECK (std::abs (normFromPlain (kSteps, 0.) - 0.0) < 1e-9);
		CHECK (std::abs (normFromPlain (kMidiChannel, 99.) - 1.0) < 1e-9);
	}

	std::printf (failures ? "FAILED (%d)\n" : "ALL OK\n", failures);
	return failures;
}
