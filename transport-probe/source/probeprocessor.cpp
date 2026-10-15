#include "probeprocessor.h"
#include "cids.h"

#include "public.sdk/source/vst/vsteventshelper.h"
#include "pluginterfaces/vst/ivstevents.h"
#include "pluginterfaces/vst/ivstmidicontrollers.h"
#include "pluginterfaces/vst/ivstprocesscontext.h"
#include "pluginterfaces/vst/vstspeaker.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>

using namespace Steinberg;
using namespace Steinberg::Vst;

namespace TransportProbe {

namespace {
constexpr double kNoteLengthBeats = 0.25;
constexpr double kJumpToleranceBeats = 0.02;
constexpr int kMaxOut = 64;

struct OutList
{
	Event ev[kMaxOut];
	int n {0};
	void add (const Event& e)
	{
		if (n < kMaxOut)
			ev[n++] = e;
	}
};
}

//------------------------------------------------------------------------
ProbeProcessor::ProbeProcessor ()
{
	setControllerClass (ControllerUID);
	// Without this the host may fill none of ProcessContext (SDK >= 3.7). Fixed once setActive is reached.
	processContextRequirements.needSystemTime ()
	    .needTransportState ()
	    .needProjectTimeMusic ()
	    .needTempo ()
	    .needTimeSignature ()
	    .needBarPositionMusic ()
	    .needCycleMusic ();
}

uint32 PLUGIN_API ProbeProcessor::getProcessContextRequirements ()
{
	log.text ("getProcessContextRequirements -> 0x%x", processContextRequirements.flags);
	return AudioEffect::getProcessContextRequirements ();
}

//------------------------------------------------------------------------
tresult PLUGIN_API ProbeProcessor::initialize (FUnknown* context)
{
	tresult result = AudioEffect::initialize (context);
	if (result != kResultOk)
		return result;

	// Live will not load a MIDI-only VST3: needs an audio output (kept silent).
	addAudioOutput (STR16 ("Silent Out"), SpeakerArr::kStereo);
	addEventInput (STR16 ("Event In"), 16);
	addEventOutput (STR16 ("Event Out"), 16);
	log.text ("initialize");
	return kResultOk;
}

tresult PLUGIN_API ProbeProcessor::terminate ()
{
	log.text ("terminate");
	log.stop ();
	return AudioEffect::terminate ();
}

tresult PLUGIN_API ProbeProcessor::canProcessSampleSize (int32 symbolicSampleSize)
{
	return symbolicSampleSize == kSample32 ? kResultTrue : kResultFalse;
}

tresult PLUGIN_API ProbeProcessor::setupProcessing (ProcessSetup& setup)
{
	log.text ("setupProcessing sampleRate=%.1f maxBlock=%d processMode=%d", setup.sampleRate,
	          setup.maxSamplesPerBlock, setup.processMode);
	return AudioEffect::setupProcessing (setup);
}

tresult PLUGIN_API ProbeProcessor::setActive (TBool state)
{
	log.text ("setActive(%d)", state ? 1 : 0);
	if (state)
	{
		resetState ();
		blockSeq = 0;
		log.start ();
	}
	else
	{
		// Cannot emit note-offs here (no ProcessData). Downstream may hold notes if the host
		// deactivates mid-note; real sequencers should flush on the last process() instead.
		resetState ();
		log.stop ();
	}
	return AudioEffect::setActive (state);
}

tresult PLUGIN_API ProbeProcessor::setProcessing (TBool state)
{
	log.text ("setProcessing(%d)", state ? 1 : 0);
	if (!state)
		resetState ();
	return AudioEffect::setProcessing (state);
}

void ProbeProcessor::resetState ()
{
	for (auto& a : active)
		a.used = false;
	wasPlaying = false;
	havePrev = false;
	lastBeat = -1;
}

//------------------------------------------------------------------------
tresult PLUGIN_API ProbeProcessor::process (ProcessData& data)
{
	// ---- silent audio out
	for (int32 b = 0; b < data.numOutputs; ++b)
	{
		AudioBusBuffers& bus = data.outputs[b];
		for (int32 ch = 0; ch < bus.numChannels; ++ch)
			if (bus.channelBuffers32 && bus.channelBuffers32[ch])
				std::memset (bus.channelBuffers32[ch], 0, sizeof (Sample32) * data.numSamples);
		bus.silenceFlags = bus.numChannels >= 64 ? ~0ull : ((1ull << bus.numChannels) - 1);
	}

	BlockRecord rec {};
	rec.seq = blockSeq++;
	rec.numSamples = data.numSamples;
	rec.processMode = data.processMode;
	rec.expectedPpq = std::numeric_limits<double>::quiet_NaN ();

	// ---- log incoming events (routed MIDI / live notes), capped per block
	if (data.inputEvents)
	{
		rec.numInEvents = data.inputEvents->getEventCount ();
		for (int32 i = 0; i < rec.numInEvents && i < 8; ++i)
		{
			Event e;
			if (data.inputEvents->getEvent (i, e) != kResultOk)
				continue;
			EventRecord er {};
			er.seq = rec.seq;
			er.type = e.type;
			er.sampleOffset = e.sampleOffset;
			er.flags = e.flags;
			er.ppqPosition = e.ppqPosition;
			er.pitch = e.type == Event::kNoteOnEvent    ? e.noteOn.pitch :
			           e.type == Event::kNoteOffEvent   ? e.noteOff.pitch : -1;
			log.push (er);
		}
	}

	const ProcessContext* c = data.processContext;
	uint32 st = 0;
	if (c)
	{
		rec.flags |= kHasContext;
		st = c->state;
		rec.state = st;
		rec.sampleRate = c->sampleRate;
		rec.projectTimeSamples = c->projectTimeSamples;
		rec.systemTime = c->systemTime;
		rec.ppq = c->projectTimeMusic;
		rec.bar = c->barPositionMusic;
		rec.cycleStart = c->cycleStartMusic;
		rec.cycleEnd = c->cycleEndMusic;
		rec.tempo = c->tempo;
		rec.tsNum = c->timeSigNumerator;
		rec.tsDen = c->timeSigDenominator;
	}

	const bool playing = (st & ProcessContext::kPlaying) != 0;
	const bool ppqValid = (st & ProcessContext::kProjectTimeMusicValid) != 0;
	const bool tempoValid = (st & ProcessContext::kTempoValid) != 0;
	const bool playStart = playing && !wasPlaying;
	const bool playStop = !playing && wasPlaying;
	if (playStart)
		rec.flags |= kPlayStart;
	if (playStop)
		rec.flags |= kPlayStop;

	OutList out;

	auto flushNotes = [&] (int32 offset, double ppq) {
		for (auto& a : active)
			if (a.used)
			{
				Event e {};
				e.busIndex = 0;
				e.sampleOffset = offset;
				e.ppqPosition = ppq;
				e.type = Event::kNoteOffEvent;
				e.noteOff.channel = 0;
				e.noteOff.pitch = a.pitch;
				e.noteOff.noteId = a.noteId;
				e.noteOff.velocity = 0.f;
				out.add (e);
				a.used = false;
				++rec.offsEmitted;
			}
	};

	if (!playing)
	{
		flushNotes (0, rec.ppq); // covers play->stop; no-op otherwise
		havePrev = false;
	}
	else if (ppqValid && tempoValid && c->tempo > 0. && c->sampleRate > 0.)
	{
		rec.flags |= kGenerated;
		const double p0 = c->projectTimeMusic;
		const double pps = c->tempo / 60.0 / c->sampleRate; // beats per sample
		double p1 = p0 + data.numSamples * pps;
		// Live splits blocks at the loop end, but our tempo-based p1 can overshoot cycleEnd by a
		// hair and fire a ghost note on the loop-end beat right before the real loop-start beat.
		// The region past cycleEnd is never played, so clamp.
		if ((st & ProcessContext::kCycleActive) && (st & ProcessContext::kCycleValid) &&
		    p0 < c->cycleEndMusic && p1 > c->cycleEndMusic)
			p1 = c->cycleEndMusic;

		if (havePrev && !playStart)
		{
			rec.expectedPpq = prevEndPpq;
			if (std::fabs (p0 - prevEndPpq) > kJumpToleranceBeats)
				rec.flags |= kJump;
		}
		if (playStart || (rec.flags & kJump))
		{
			flushNotes (0, p0);
			lastBeat = static_cast<long long> (std::ceil (p0 - 1e-6)) - 1;
		}

		// note-offs that fall inside this block (events are sorted by offset before emitting)
		auto clampOffset = [&] (double ppq) {
			double off = std::round ((ppq - p0) / pps);
			return static_cast<int32> (std::min<double> (std::max (off, 0.), data.numSamples - 1));
		};
		auto emitDueOffs = [&] {
			for (auto& a : active)
				if (a.used && a.endPpq < p1)
				{
					Event e {};
					e.busIndex = 0;
					e.sampleOffset = clampOffset (a.endPpq);
					e.ppqPosition = a.endPpq;
					e.type = Event::kNoteOffEvent;
					e.noteOff.channel = 0;
					e.noteOff.pitch = a.pitch;
					e.noteOff.noteId = a.noteId;
					e.noteOff.velocity = 0.f;
					out.add (e);
					a.used = false;
					++rec.offsEmitted;
				}
		};
		emitDueOffs (); // from earlier blocks

		const int num = (c->state & ProcessContext::kTimeSigValid) && c->timeSigNumerator > 0 ?
		                    c->timeSigNumerator : 4;
		for (long long b = lastBeat + 1; static_cast<double> (b) < p1; ++b)
		{
			const int32 off = clampOffset (static_cast<double> (b));
			ActiveNote* slot = nullptr;
			for (auto& a : active)
				if (!a.used) { slot = &a; break; }

			Event on {};
			on.busIndex = 0;
			on.sampleOffset = off;
			on.ppqPosition = static_cast<double> (b);
			on.type = Event::kNoteOnEvent;
			on.noteOn.channel = 0;
			on.noteOn.pitch = (b % num == 0) ? 72 : 60;
			on.noteOn.velocity = 0.8f;
			on.noteOn.length = 0;
			on.noteOn.tuning = 0.f;
			on.noteOn.noteId = nextNoteId++;
			out.add (on);
			++rec.beatsEmitted;
			if (slot)
			{
				slot->used = true;
				slot->pitch = on.noteOn.pitch;
				slot->noteId = on.noteOn.noteId;
				slot->endPpq = static_cast<double> (b) + kNoteLengthBeats;
			}

			Event cc {};
			Helpers::initLegacyMIDICCOutEvent (cc, kCtrlModWheel, 0,
			                                   static_cast<int8> ((b * 8) & 127));
			cc.sampleOffset = off;
			cc.ppqPosition = static_cast<double> (b);
			out.add (cc);
			++rec.ccEmitted;
			lastBeat = b;
		}
		emitDueOffs (); // short notes that start and end in this block

		prevEndPpq = p1;
		havePrev = true;
	}
	else
	{
		havePrev = false; // playing but no usable ppq/tempo: log only
	}

	// ---- emit in sample-offset order (stable)
	if (data.outputEvents && out.n > 0)
	{
		std::stable_sort (out.ev, out.ev + out.n,
		                  [] (const Event& a, const Event& b) { return a.sampleOffset < b.sampleOffset; });
		for (int i = 0; i < out.n; ++i)
			data.outputEvents->addEvent (out.ev[i]);
	}

	wasPlaying = playing;
	log.push (rec);
	return kResultOk;
}

}
