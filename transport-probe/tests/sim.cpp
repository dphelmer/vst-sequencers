// Offline simulation: drives ProbeProcessor::process() with a fake transport and prints the
// events it emits. Not a plug-in; build with `cmake --build build --target ProbeSim`.
#include "probeprocessor.h"

#include "public.sdk/source/vst/hosting/eventlist.h"
#include "pluginterfaces/vst/ivstprocesscontext.h"

#include <cstdio>
#include <cstdlib>
#include <vector>

using namespace Steinberg;
using namespace Steinberg::Vst;

static int failures = 0;
#define CHECK(c) do { if (!(c)) { std::printf ("CHECK FAILED line %d: %s\n", __LINE__, #c); ++failures; } } while (0)

int main ()
{
	TransportProbe::ProbeProcessor proc;
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

	EventList outEvents (64), inEvents (8);
	ProcessContext ctx {};
	ProcessData data;
	data.numSamples = N;
	data.numOutputs = 1;
	data.outputs = &outBus;
	data.inputEvents = &inEvents;
	data.outputEvents = &outEvents;
	data.processContext = &ctx;

	const double tempo = 120., sr = 48000.;
	const double ppqPerBlock = N * tempo / 60. / sr;
	ctx.sampleRate = sr;
	ctx.tempo = tempo;
	ctx.timeSigNumerator = 4;
	ctx.timeSigDenominator = 4;

	int ons = 0, offs = 0, ccs = 0;
	std::vector<double> onPpq;
	auto run = [&] (double ppq, bool playing, const char* tag) {
		ctx.state = ProcessContext::kTempoValid | ProcessContext::kProjectTimeMusicValid |
		            ProcessContext::kTimeSigValid | (playing ? ProcessContext::kPlaying : 0);
		ctx.projectTimeMusic = ppq;
		outEvents.clear ();
		proc.process (data);
		for (int i = 0; i < outEvents.getEventCount (); ++i)
		{
			Event e;
			outEvents.getEvent (i, e);
			const double exact = ppq + e.sampleOffset * tempo / 60. / sr;
			if (e.type == Event::kNoteOnEvent)
			{
				++ons;
				onPpq.push_back (e.ppqPosition);
				CHECK (std::abs (exact - e.ppqPosition) <= tempo / 60. / sr); // within 1 sample
				std::printf ("%-6s ppq=%.4f NOTE ON  pitch=%d off=%d\n", tag, ppq, e.noteOn.pitch, e.sampleOffset);
			}
			else if (e.type == Event::kNoteOffEvent)
			{
				++offs;
				std::printf ("%-6s ppq=%.4f NOTE OFF pitch=%d off=%d\n", tag, ppq, e.noteOff.pitch, e.sampleOffset);
			}
			else
			{
				++ccs;
			}
		}
	};

	// 1) start mid-grid at 0.3, play to ~4.5: beats 1,2,3,4 fire exactly once each
	double ppq = 0.3;
	for (; ppq < 4.5; ppq += ppqPerBlock)
		run (ppq, true, "play");
	CHECK (ons == 4);
	CHECK (offs == 4); // each note ends 0.25 beat later; beat 4's end (4.25) < 4.5
	CHECK (ccs == 4);
	for (int i = 0; i < 4; ++i)
		CHECK (onPpq[i] == i + 1.0);

	// 2) loop wrap: jump back to 1.9; beats 2 and 3 must fire again, 1 must not
	ons = offs = ccs = 0;
	onPpq.clear ();
	ppq = 1.9;
	for (; ppq < 3.5; ppq += ppqPerBlock)
		run (ppq, true, "loop");
	CHECK (ons == 2);
	CHECK (onPpq.size () == 2 && onPpq[0] == 2.0 && onPpq[1] == 3.0);

	// 3) stop while a note is held: start a new play at 3.99 -> beat 4 note on, stop in the
	// same beat -> flush. Offs must equal ons afterwards.
	ons = offs = ccs = 0;
	ppq = 3.99;
	run (ppq, true, "play2");               // block covers beat 4 -> note on, off not yet due?
	ppq += ppqPerBlock;
	run (ppq, true, "play2");
	run (ppq + ppqPerBlock, false, "stop"); // stop -> any held note must be released
	CHECK (ons == 1);
	CHECK (offs == ons);

	// 4) stopped blocks emit nothing
	ons = offs = ccs = 0;
	for (int i = 0; i < 10; ++i)
		run (4.0, false, "idle");
	CHECK (ons == 0 && offs == 0 && ccs == 0);

	proc.setProcessing (false);
	proc.setActive (false);
	std::printf (failures ? "FAILED (%d)\n" : "ALL OK\n", failures);
	return failures;
}
