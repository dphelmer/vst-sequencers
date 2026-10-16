#include "polyriffprocessor.h"
#include "cids.h"

#include "public.sdk/source/vst/vsteventshelper.h"
#include "pluginterfaces/vst/ivstevents.h"
#include "pluginterfaces/vst/ivstparameterchanges.h"
#include "pluginterfaces/vst/ivstprocesscontext.h"
#include "pluginterfaces/vst/vstspeaker.h"

#include <algorithm>
#include <cmath>
#include <cstring>

using namespace Steinberg;
using namespace Steinberg::Vst;

namespace Polyriff {

namespace {
constexpr double kJumpToleranceBeats = 0.02;
constexpr int kMaxOut = 256; // ~2 lanes x steps per block + flushes; block span is small

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

inline long long modPos (long long v, int m)
{
	long long r = v % m;
	return r < 0 ? r + m : r;
}
}

//------------------------------------------------------------------------
PolyriffProcessor::PolyriffProcessor ()
{
	setControllerClass (ControllerUID);
	// Without this the host may fill none of ProcessContext (SDK >= 3.7).
	processContextRequirements.needTransportState ()
	    .needProjectTimeMusic ()
	    .needTempo ()
	    .needTimeSignature ()
	    .needCycleMusic ();

	gp = defaultGenParams ();
	generatePattern (pat, gp, rotShift);
}

//------------------------------------------------------------------------
tresult PLUGIN_API PolyriffProcessor::initialize (FUnknown* context)
{
	tresult result = AudioEffect::initialize (context);
	if (result != kResultOk)
		return result;

	// Live will not load a MIDI-only VST3: needs an audio output (kept silent).
	addAudioOutput (STR16 ("Silent Out"), SpeakerArr::kStereo);
	// Live also refuses an instrument with no event input bus ("plugin has instrument category,
	// but no valid event input bus") even though Polyriff ignores incoming MIDI.
	addEventInput (STR16 ("Event In"), 16);
	addEventOutput (STR16 ("Event Out"), 16);
	// Virtual CoreMIDI source: Live tracks take it via Ext. In with per-channel choosers
	// (the external-input path preserves channels; track-to-track routing merges them).
	// Skipped when context is null (PolyriffSim).
	if (context)
		virtOut.create ("Polyriff MIDI");
	return kResultOk;
}

tresult PLUGIN_API PolyriffProcessor::terminate ()
{
	virtOut.destroy ();
	return AudioEffect::terminate ();
}

tresult PLUGIN_API PolyriffProcessor::canProcessSampleSize (int32 symbolicSampleSize)
{
	return symbolicSampleSize == kSample32 ? kResultTrue : kResultFalse;
}

tresult PLUGIN_API PolyriffProcessor::setupProcessing (ProcessSetup& setup)
{
	return AudioEffect::setupProcessing (setup);
}

tresult PLUGIN_API PolyriffProcessor::setActive (TBool state)
{
	if (!state)
	{
		// No ProcessData here, so VST events cannot be emitted -- but the virtual
		// source must not hold notes in Live's Ext. In path.
		flushVirtualNotes ();
	}
	resetState ();
	return AudioEffect::setActive (state);
}

tresult PLUGIN_API PolyriffProcessor::setProcessing (TBool state)
{
	if (!state)
	{
		flushVirtualNotes ();
		resetState ();
	}
	return AudioEffect::setProcessing (state);
}

void PolyriffProcessor::resetState ()
{
	for (auto& lane : active)
		for (auto& a : lane)
			a.used = false;
	wasPlaying = false;
	havePrev = false;
	lastStep = -1;
}

void PolyriffProcessor::flushVirtualNotes ()
{
	for (auto& lane : active)
		for (auto& a : lane)
			if (a.used)
			{
				virtOut.noteOff (a.midiCh, a.pitch);
				a.used = false;
			}
}

//------------------------------------------------------------------------
void PolyriffProcessor::handleParameterChanges (IParameterChanges* changes)
{
	if (!changes)
		return;
	bool rebuild = false;
	const int32 count = changes->getParameterCount ();
	for (int32 i = 0; i < count; ++i)
	{
		IParamValueQueue* queue = changes->getParameterData (i);
		if (!queue)
			continue;
		const int32 points = queue->getPointCount ();
		if (points <= 0)
			continue;
		ParamValue norm;
		int32 sampleOffset;
		if (queue->getPoint (points - 1, sampleOffset, norm) != kResultOk)
			continue;
		const auto id = queue->getParameterId ();
		const double plain = plainFromNorm (id, norm); // decode via shared helpers
		switch (id)
		{
			case kSeed:
			{
				const auto v = static_cast<std::uint32_t> (plain);
				if (v != gp.seed)
				{
					gp.seed = v;
					rotShift = 0; // JS clearRot(): rebuilds restart the cycle from riff[0]
					rebuild = true;
				}
				break;
			}
			case kRiffLen:
			{
				const int v = static_cast<int> (plain);
				if (v != gp.riffLen)
				{
					gp.riffLen = v;
					rotShift = 0;
					rebuild = true;
				}
				break;
			}
			case kDensity:
			{
				const int v = static_cast<int> (plain);
				if (v != gp.density) { gp.density = v; rebuild = true; }
				break;
			}
			case kGroupSpread:
			{
				const int v = static_cast<int> (plain);
				if (v != gp.groupSpread) { gp.groupSpread = v; rebuild = true; }
				break;
			}
			case kPitchLow:
			{
				const int v = static_cast<int> (plain);
				if (v != gp.pitchLow) { gp.pitchLow = v; rebuild = true; }
				break;
			}
			case kPitchRange:
			{
				const int v = static_cast<int> (plain);
				if (v != gp.pitchRange) { gp.pitchRange = v; rebuild = true; }
				break;
			}
			case kPitchDensity:
			{
				const int v = static_cast<int> (plain);
				if (v != gp.pitchDensity) { gp.pitchDensity = v; rebuild = true; }
				break;
			}
			case kKickMode:
			{
				const int v = static_cast<int> (plain);
				if (v != gp.kickMode) { gp.kickMode = v; rebuild = true; }
				break;
			}
			case kAccentVel:
			{
				const int v = static_cast<int> (plain);
				if (v != gp.accentVel) { gp.accentVel = v; rebuild = true; }
				break;
			}
			case kSlidePct:
			{
				const int v = static_cast<int> (plain);
				if (v != gp.slidePct) { gp.slidePct = v; rebuild = true; }
				break;
			}
			case kGatePct:
				gp.gatePct = static_cast<int> (plain); // read live at emit; no rebuild
				break;
			case kHyperBars:
				gp.hyperBars = static_cast<int> (plain); // boundary spacing; read in walk
				break;
			case kRiffChannel:
				gp.riffChannel = static_cast<int> (plain);
				break;
			case kKickChannel:
				gp.kickChannel = static_cast<int> (plain);
				break;
			case kRandomize:
				// Momentary trigger: the controller turns it into a kSeed edit
				// (performEdit), which arrives here. The processor must not invent a
				// second, different seed of its own.
				break;
			default:
				break;
		}
	}
	if (rebuild)
		generatePattern (pat, gp, rotShift);
}

//------------------------------------------------------------------------
tresult PLUGIN_API PolyriffProcessor::process (ProcessData& data)
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

	handleParameterChanges (data.inputParameterChanges);

	const ProcessContext* c = data.processContext;
	const uint32 st = c ? c->state : 0;
	const bool playing = (st & ProcessContext::kPlaying) != 0;
	const bool ppqValid = (st & ProcessContext::kProjectTimeMusicValid) != 0;
	const bool tempoValid = (st & ProcessContext::kTempoValid) != 0;
	const bool playStart = playing && !wasPlaying;

	// 16ths per bar from the time signature (fallback 4/4); drives hypermetric boundaries.
	int spb = 16;
	if (c && (st & ProcessContext::kTimeSigValid) && c->timeSigNumerator > 0 &&
	    c->timeSigDenominator > 0)
		spb = static_cast<int> (std::lround (c->timeSigNumerator * 16.0 / c->timeSigDenominator));
	if (spb < 1)
		spb = 16;
	const int hyperBars = gp.hyperBars < 1 ? 1 : gp.hyperBars;

	OutList out;

	auto flushNotes = [&] (int32 offset, double ppq) {
		for (auto& lane : active)
			for (auto& a : lane)
				if (a.used)
				{
					Event e {};
					e.busIndex = 0;
					e.sampleOffset = offset;
					e.ppqPosition = ppq;
					e.type = Event::kNoteOffEvent;
					e.noteOff.channel = static_cast<uint8> (a.midiCh);
					e.noteOff.pitch = a.pitch;
					e.noteOff.noteId = a.noteId;
					e.noteOff.velocity = 0.f;
					out.add (e);
					virtOut.noteOff (a.midiCh, a.pitch);
					a.used = false;
				}
	};

	// Re-seat the rotation so riff index 0 plays at sixteenth s (hypermetric downbeat).
	// Slide rolls are rotation-dependent (the JS re-rolls them in display order), so
	// a shift change rebuilds the pattern.
	auto setRot = [&] (long long s) {
		const int r = static_cast<int> (modPos (s, pat.n));
		if (r != rotShift)
		{
			rotShift = r;
			generatePattern (pat, gp, rotShift);
		}
	};

	if (!playing)
	{
		flushNotes (0, c ? c->projectTimeMusic : 0.); // covers play->stop; no-op otherwise
		havePrev = false;
	}
	else if (ppqValid && tempoValid && c->tempo > 0. && c->sampleRate > 0.)
	{
		const double p0 = c->projectTimeMusic;
		const double pps = c->tempo / 60.0 / c->sampleRate; // beats per sample
		double p1 = p0 + data.numSamples * pps;
		// Loop-end clamp: ppq-from-tempo can overshoot cycleEnd by ~1e-5 beat and fire a
		// ghost note on the loop-end beat one sample before the real loop-start note.
		if ((st & ProcessContext::kCycleActive) && (st & ProcessContext::kCycleValid) &&
		    p0 < c->cycleEndMusic && p1 > c->cycleEndMusic)
			p1 = c->cycleEndMusic;

		const bool jump = havePrev && !playStart && std::fabs (p0 - prevEndPpq) > kJumpToleranceBeats;
		if (playStart || jump)
		{
			flushNotes (0, p0);
			lastStep = static_cast<long long> (std::floor (p0 / kBeatsPerStep + 1e-9)) - 1;
			// Max resyncs when the bar VALUE becomes a boundary bar (change->expr->sel):
			// locating into the middle of a boundary bar counts; landing anywhere else
			// keeps the previous rotation.
			const long long s0 = lastStep + 1;
			if ((s0 / spb) % hyperBars == 0)
				setRot (s0);
		}

		auto clampOffset = [&] (double ppq) {
			double off = std::round ((ppq - p0) / pps);
			return static_cast<int32> (std::min<double> (std::max (off, 0.), data.numSamples - 1));
		};

		// note-offs due inside this block (from earlier blocks); frees slide-overlap slots
		auto emitDueOffs = [&] {
			for (auto& lane : active)
				for (auto& a : lane)
					if (a.used && a.endPpq <= p1 + 1e-9)
					{
						Event e {};
						e.busIndex = 0;
						e.sampleOffset = clampOffset (a.endPpq);
						e.ppqPosition = a.endPpq;
						e.type = Event::kNoteOffEvent;
						e.noteOff.channel = static_cast<uint8> (a.midiCh);
						e.noteOff.pitch = a.pitch;
						e.noteOff.noteId = a.noteId;
						e.noteOff.velocity = 0.f;
						out.add (e);
						virtOut.noteOff (a.midiCh, a.pitch);
						a.used = false;
					}
		};
		emitDueOffs ();

		auto emitNote = [&] (int lane, int pitch, int vel, bool slide, double sPpq, int32 off) {
			ActiveNote* slot = nullptr;
			ActiveNote* oldest = nullptr;
			for (auto& a : active[lane])
			{
				if (!a.used) { slot = &a; break; }
				if (!oldest || a.endPpq < oldest->endPpq)
					oldest = &a;
			}
			if (!slot)
			{
				// All 4 slots ringing (not reachable with gate <= 0.5 beats, but never
				// leak a note): cut the oldest here, off strictly before the on below.
				slot = oldest;
				Event offE {};
				offE.busIndex = 0;
				offE.sampleOffset = off;
				offE.ppqPosition = sPpq;
				offE.type = Event::kNoteOffEvent;
				offE.noteOff.channel = static_cast<uint8> (slot->midiCh);
				offE.noteOff.pitch = slot->pitch;
				offE.noteOff.noteId = slot->noteId;
				offE.noteOff.velocity = 0.f;
				out.add (offE);
				virtOut.noteOff (slot->midiCh, slot->pitch);
			}

			const double gateBeats = kBeatsPerStep * gp.gatePct / 100.0;
			const double dur = slide ? std::max (gateBeats, 0.5) : gateBeats;

			Event on {};
			on.busIndex = 0;
			on.sampleOffset = off;
			on.ppqPosition = sPpq;
			on.type = Event::kNoteOnEvent;
			on.noteOn.channel = static_cast<uint8> (lane == 0 ? gp.riffChannel - 1 : gp.kickChannel - 1);
			on.noteOn.pitch = static_cast<int16> (pitch);
			on.noteOn.velocity = vel / 127.f;
			on.noteOn.length = 0;
			on.noteOn.tuning = 0.f;
			on.noteOn.noteId = nextNoteId++;
			out.add (on);
			virtOut.noteOn (on.noteOn.channel, pitch, vel);

			slot->used = true;
			slot->pitch = on.noteOn.pitch;
			slot->noteId = on.noteOn.noteId;
			slot->midiCh = on.noteOn.channel;
			slot->endPpq = sPpq + dur;
		};

		// 16th cursor: boundary re-seat, then both lanes. Pattern index advances
		// forward from the last boundary (truncate-at-L tail between resyncs).
		for (long long s = lastStep + 1;; ++s)
		{
			const double sPpq = static_cast<double> (s) * kBeatsPerStep;
			if (sPpq >= p1)
				break;
			const int32 off = clampOffset (sPpq);
			if (s % spb == 0 && (s / spb) % hyperBars == 0)
				setRot (s); // hypermetric boundary: riff[0] lands on this downbeat
			const int idx = static_cast<int> (modPos (s - rotShift, pat.n));
			if (pat.riffVel[idx] > 0)
				emitNote (0, pat.pitch[idx], pat.riffVel[idx], pat.riffSlide[idx], sPpq, off);
			if (pat.kickVel[idx] > 0)
				emitNote (1, kKickPitch, pat.kickVel[idx], pat.kickSlide[idx], sPpq, off);
			lastStep = s;
		}
		emitDueOffs (); // gates that start and end inside this block

		prevEndPpq = p1;
		havePrev = true;
	}
	else
	{
		havePrev = false; // playing but no usable ppq/tempo
	}

	// ---- emit in sample-offset order (stable: offs added before ons at equal offsets)
	if (data.outputEvents && out.n > 0)
	{
		std::stable_sort (out.ev, out.ev + out.n,
		                  [] (const Event& a, const Event& b) { return a.sampleOffset < b.sampleOffset; });
		for (int i = 0; i < out.n; ++i)
			data.outputEvents->addEvent (out.ev[i]);
	}

	wasPlaying = playing;
	return kResultOk;
}

}
