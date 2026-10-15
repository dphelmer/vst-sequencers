#pragma once

#include "public.sdk/source/vst/vstaudioeffect.h"
#include "params.h"

namespace Euclid {

/** 4-channel Euclidean MIDI drum sequencer.
 *
 * Each channel runs a Euclidean rhythm of `pulses` hits spread over `steps` 16th-note
 * slots, rotated by `rotate`. Pattern position is derived from the host's
 * ProcessContext::projectTimeMusic (never projectTimeSamples): global 16th index =
 * floor(ppq / kBeatsPerStep), channel pattern index = global % steps. Notes are emitted
 * sample-accurately by splitting each process() block at step boundaries and gate-off
 * points. Loop wraps are detected from ppq jumps and re-seat the grid; held notes are
 * flushed on stop/jump/deactivate.
 */
class EuclidProcessor : public Steinberg::Vst::AudioEffect
{
public:
	EuclidProcessor ();

	static Steinberg::FUnknown* createInstance (void*)
	{
		return static_cast<Steinberg::Vst::IAudioProcessor*> (new EuclidProcessor);
	}

	Steinberg::tresult PLUGIN_API initialize (Steinberg::FUnknown* context) SMTG_OVERRIDE;
	Steinberg::tresult PLUGIN_API terminate () SMTG_OVERRIDE;
	Steinberg::tresult PLUGIN_API setActive (Steinberg::TBool state) SMTG_OVERRIDE;
	Steinberg::tresult PLUGIN_API setProcessing (Steinberg::TBool state) SMTG_OVERRIDE;
	Steinberg::tresult PLUGIN_API setupProcessing (Steinberg::Vst::ProcessSetup& setup) SMTG_OVERRIDE;
	Steinberg::tresult PLUGIN_API canProcessSampleSize (Steinberg::int32 symbolicSampleSize) SMTG_OVERRIDE;
	Steinberg::tresult PLUGIN_API process (Steinberg::Vst::ProcessData& data) SMTG_OVERRIDE;

private:
	struct ChanParam
	{
		int pulses {4};
		int steps {16};
		int rotate {0};
		int pitch {36};
		int velocity {100};
		double gate {0.5};
		bool mute {false};
	};

	struct ActiveNote
	{
		bool used {false};
		Steinberg::int16 pitch {0};
		Steinberg::int32 noteId {0};
		int channel {-1};
		double endPpq {0.};
	};

	void resetState ();
	void handleParameterChanges (Steinberg::Vst::IParameterChanges* changes);

	ChanParam chan[kNumChannels];
	int midiChannel {9}; // 0-based; 9 = MIDI ch 10
	ActiveNote active[kNumChannels]; // one note at a time per channel (retrigger replaces)
	Steinberg::int32 nextNoteId {1};
	bool wasPlaying {false};
	bool havePrev {false};
	double prevEndPpq {0.};
	long long lastStep {-1}; // last global 16th index emitted
};

}
