#pragma once

#include "public.sdk/source/vst/vstaudioeffect.h"
#include "params.h"
#include "polyriffgen.h"
#include "virtualmidi.h"

namespace Polyriff {

/** Meshuggah-style polymetric MIDI riff sequencer.
 *
 * Pattern core lives in polyriffgen.h (bit-exact port of the Max JS). Transport logic
 * mirrors Euclid: 16th cursor derived from ProcessContext::projectTimeMusic, ppq-jump
 * detection for loop wraps/locates, cycle-end clamp, held notes flushed on stop/jump/
 * deactivate. At every hypermetric boundary (bar0 % hyperBars == 0, where bars come
 * from the time signature) the pattern rotation is re-seated so riff index 0 lands on
 * the hypermetric downbeat -- the truncate-at-L cadence of the original patch. Notes
 * go to the VST event bus (riff/kick channels are parameters) and are mirrored to the
 * CoreMIDI virtual source "Polyriff MIDI" (Live merges channels on track-to-track;
 * Ext. In preserves them).
 */
class PolyriffProcessor : public Steinberg::Vst::AudioEffect
{
public:
	PolyriffProcessor ();

	static Steinberg::FUnknown* createInstance (void*)
	{
		return static_cast<Steinberg::Vst::IAudioProcessor*> (new PolyriffProcessor);
	}

	Steinberg::tresult PLUGIN_API initialize (Steinberg::FUnknown* context) SMTG_OVERRIDE;
	Steinberg::tresult PLUGIN_API terminate () SMTG_OVERRIDE;
	Steinberg::tresult PLUGIN_API setActive (Steinberg::TBool state) SMTG_OVERRIDE;
	Steinberg::tresult PLUGIN_API setProcessing (Steinberg::TBool state) SMTG_OVERRIDE;
	Steinberg::tresult PLUGIN_API setupProcessing (Steinberg::Vst::ProcessSetup& setup) SMTG_OVERRIDE;
	Steinberg::tresult PLUGIN_API canProcessSampleSize (Steinberg::int32 symbolicSampleSize) SMTG_OVERRIDE;
	Steinberg::tresult PLUGIN_API process (Steinberg::Vst::ProcessData& data) SMTG_OVERRIDE;

private:
	struct ActiveNote
	{
		bool used {false};
		Steinberg::int16 pitch {0};
		Steinberg::int32 noteId {0};
		int midiCh {0}; // 0-based channel used at note-on
		double endPpq {0.};
	};

	void resetState ();
	void handleParameterChanges (Steinberg::Vst::IParameterChanges* changes);
	void flushVirtualNotes ();

	static constexpr int kLanes = 2;   // 0 = riff, 1 = kick
	static constexpr int kSlots = 4;   // concurrent notes per lane (slide overlaps)

	GenParams gp;
	Pattern pat;
	int rotShift {0}; // pattern index that plays at the current hypermetric cycle start

	ActiveNote active[kLanes][kSlots];
	Steinberg::int32 nextNoteId {1};
	bool wasPlaying {false};
	bool havePrev {false};
	double prevEndPpq {0.};
	long long lastStep {-1}; // last global 16th index emitted

	VirtualMidiOut virtOut;
};

}
