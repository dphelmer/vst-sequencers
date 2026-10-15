#include "euclidprocessor.h"
#include "cids.h"
#include "euclidean.h"

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

namespace Euclid {

namespace {
constexpr double kJumpToleranceBeats = 0.02;
constexpr int kMaxOut = 128; // 4 channels x (on+off) x a few steps per block

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
EuclidProcessor::EuclidProcessor ()
{
	setControllerClass (ControllerUID);
	// Without this the host may fill none of ProcessContext (SDK >= 3.7).
	processContextRequirements.needTransportState ()
	    .needProjectTimeMusic ()
	    .needTempo ()
	    .needCycleMusic ();

	// defaults matching the controller
	chan[0] = {4, 16, 0, 36, 100, 0.5, false};
	chan[1] = {2, 16, 4, 38, 110, 0.5, false};
	chan[2] = {8, 16, 0, 42, 80, 0.5, false};
	chan[3] = {3, 16, 0, 46, 90, 0.5, false};
}

//------------------------------------------------------------------------
tresult PLUGIN_API EuclidProcessor::initialize (FUnknown* context)
{
	tresult result = AudioEffect::initialize (context);
	if (result != kResultOk)
		return result;

	// Live will not load a MIDI-only VST3: needs an audio output (kept silent).
	addAudioOutput (STR16 ("Silent Out"), SpeakerArr::kStereo);
	// Live also refuses an instrument with no event input bus ("plugin has instrument category,
	// but no valid event input bus") even though Euclid ignores incoming MIDI.
	addEventInput (STR16 ("Event In"), 16);
	addEventOutput (STR16 ("Event Out"), 16);
	return kResultOk;
}

tresult PLUGIN_API EuclidProcessor::terminate ()
{
	return AudioEffect::terminate ();
}

tresult PLUGIN_API EuclidProcessor::canProcessSampleSize (int32 symbolicSampleSize)
{
	return symbolicSampleSize == kSample32 ? kResultTrue : kResultFalse;
}

tresult PLUGIN_API EuclidProcessor::setupProcessing (ProcessSetup& setup)
{
	return AudioEffect::setupProcessing (setup);
}

tresult PLUGIN_API EuclidProcessor::setActive (TBool state)
{
	resetState (); // cannot emit note-offs here (no ProcessData)
	return AudioEffect::setActive (state);
}

tresult PLUGIN_API EuclidProcessor::setProcessing (TBool state)
{
	if (!state)
		resetState ();
	return AudioEffect::setProcessing (state);
}

void EuclidProcessor::resetState ()
{
	for (auto& a : active)
		a.used = false;
	wasPlaying = false;
	havePrev = false;
	lastStep = -1;
}

//------------------------------------------------------------------------
void EuclidProcessor::handleParameterChanges (IParameterChanges* changes)
{
	if (!changes)
		return;
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
		auto id = queue->getParameterId ();
		if (id == kMidiChannel)
		{
			midiChannel = static_cast<int> (plainFromNorm (id, norm)) - 1; // 1-based display -> 0-based
			continue;
		}
		const int ch = static_cast<int> (id) / kParamsPerChannel;
		if (ch < 0 || ch >= kNumChannels)
			continue;
		ChanParam& p = chan[ch];
		// decode through the shared helpers so what the host displays is what plays
		const double plain = plainFromNorm (id, norm);
		switch (id % kParamsPerChannel)
		{
			case kPulses: p.pulses = static_cast<int> (plain); break;
			case kSteps: p.steps = static_cast<int> (plain); break;
			case kRotate: p.rotate = static_cast<int> (plain); break;
			case kPitch: p.pitch = static_cast<int> (plain); break;
			case kVelocity: p.velocity = static_cast<int> (plain); break;
			case kGate: p.gate = plain / 100.; break; // plain is 1..100 percent
			case kMute: p.mute = plain >= 0.5; break;
			default: break;
		}
	}
}

//------------------------------------------------------------------------
tresult PLUGIN_API EuclidProcessor::process (ProcessData& data)
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
	const bool playStop = !playing && wasPlaying;

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
				e.noteOff.channel = static_cast<uint8> (midiChannel);
				e.noteOff.pitch = a.pitch;
				e.noteOff.noteId = a.noteId;
				e.noteOff.velocity = 0.f;
				out.add (e);
				a.used = false;
			}
	};

	if (!playing)
	{
		if (playStop)
			flushNotes (0, c ? c->projectTimeMusic : 0.);
		havePrev = false;
	}
	else if (ppqValid && tempoValid && c->tempo > 0. && c->sampleRate > 0.)
	{
		const double p0 = c->projectTimeMusic;
		const double pps = c->tempo / 60.0 / c->sampleRate; // beats per sample
		double p1 = p0 + data.numSamples * pps;
		// clamp to loop end (tempo math can overshoot cycleEnd slightly; see AGENTS.md)
		if ((st & ProcessContext::kCycleActive) && (st & ProcessContext::kCycleValid) &&
		    p0 < c->cycleEndMusic && p1 > c->cycleEndMusic)
			p1 = c->cycleEndMusic;

		const bool jump = havePrev && !playStart && std::fabs (p0 - prevEndPpq) > kJumpToleranceBeats;
		if (playStart || jump)
		{
			flushNotes (0, p0);
			lastStep = static_cast<long long> (std::floor (p0 / kBeatsPerStep + 1e-9)) - 1;
		}

		auto clampOffset = [&] (double ppq) {
			double off = std::round ((ppq - p0) / pps);
			return static_cast<int32> (std::min<double> (std::max (off, 0.), data.numSamples - 1));
		};

		// note-offs due inside this block (from earlier blocks)
		auto emitDueOffs = [&] {
			for (auto& a : active)
				if (a.used && a.endPpq <= p1 + 1e-9)
				{
					Event e {};
					e.busIndex = 0;
					e.sampleOffset = clampOffset (a.endPpq);
					e.ppqPosition = a.endPpq;
					e.type = Event::kNoteOffEvent;
					e.noteOff.channel = static_cast<uint8> (midiChannel);
					e.noteOff.pitch = a.pitch;
					e.noteOff.noteId = a.noteId;
					e.noteOff.velocity = 0.f;
					out.add (e);
					a.used = false;
				}
		};
		emitDueOffs ();

		// step grid: global 16th index from ppq; each channel's pattern = global % steps
		for (long long s = lastStep + 1; ; ++s)
		{
			const double sPpq = static_cast<double> (s) * kBeatsPerStep;
			if (sPpq >= p1)
				break;
			const int32 off = clampOffset (sPpq);
			for (int ch = 0; ch < kNumChannels; ++ch)
			{
				ChanParam& p = chan[ch];
				if (p.mute || p.pulses <= 0)
					continue;
				const int steps = std::min (p.steps, kMaxSteps);
				const int idx = static_cast<int> (((s % steps) + steps) % steps);
				if (!euclideanHit (p.pulses, steps, p.rotate, idx))
					continue;

				// retrigger: the previous note on this channel ends here
				ActiveNote& a = active[ch];
				if (a.used)
				{
					Event off {};
					off.busIndex = 0;
					off.sampleOffset = clampOffset (sPpq);
					off.ppqPosition = sPpq;
					off.type = Event::kNoteOffEvent;
					off.noteOff.channel = static_cast<uint8> (midiChannel);
					off.noteOff.pitch = a.pitch;
					off.noteOff.noteId = a.noteId;
					off.noteOff.velocity = 0.f;
					out.add (off);
				}

				Event on {};
				on.busIndex = 0;
				on.sampleOffset = off;
				on.ppqPosition = sPpq;
				on.type = Event::kNoteOnEvent;
				on.noteOn.channel = static_cast<uint8> (midiChannel);
				on.noteOn.pitch = static_cast<int16> (p.pitch);
				on.noteOn.velocity = static_cast<float> (p.velocity / 127.);
				on.noteOn.length = 0;
				on.noteOn.tuning = 0.f;
				on.noteOn.noteId = nextNoteId++;
				out.add (on);

				a.used = true;
				a.pitch = on.noteOn.pitch;
				a.noteId = on.noteOn.noteId;
				a.channel = ch;
				a.endPpq = sPpq + p.gate * kBeatsPerStep;
			}
			lastStep = s;
		}
		emitDueOffs (); // gates that end inside this block

		prevEndPpq = p1;
		havePrev = true;
	}
	else
	{
		havePrev = false; // playing but no usable ppq/tempo
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
	return kResultOk;
}

}
