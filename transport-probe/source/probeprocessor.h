#pragma once

#include "probelog.h"

#include "public.sdk/source/vst/vstaudioeffect.h"

namespace TransportProbe {

/** MIDI-out transport probe.
 *
 * While the host transport plays, emits on every quarter-note boundary (computed from
 * ProcessContext::projectTimeMusic):
 *   - note on  (pitch 72 on bar downbeat assuming bars start at multiples of timeSigNumerator,
 *               else pitch 60), note off 1/4 beat later
 *   - legacy MIDI CC #1 (mod wheel) with value = (beat * 8) & 127, to test CC-out support
 * and logs everything the host gives us to ~/Library/Logs/TransportProbe/.
 */
class ProbeProcessor : public Steinberg::Vst::AudioEffect
{
public:
	ProbeProcessor ();

	static Steinberg::FUnknown* createInstance (void*)
	{
		return static_cast<Steinberg::Vst::IAudioProcessor*> (new ProbeProcessor);
	}

	Steinberg::tresult PLUGIN_API initialize (Steinberg::FUnknown* context) SMTG_OVERRIDE;
	Steinberg::tresult PLUGIN_API terminate () SMTG_OVERRIDE;
	Steinberg::tresult PLUGIN_API setActive (Steinberg::TBool state) SMTG_OVERRIDE;
	Steinberg::tresult PLUGIN_API setProcessing (Steinberg::TBool state) SMTG_OVERRIDE;
	Steinberg::tresult PLUGIN_API setupProcessing (Steinberg::Vst::ProcessSetup& setup) SMTG_OVERRIDE;
	Steinberg::tresult PLUGIN_API canProcessSampleSize (Steinberg::int32 symbolicSampleSize) SMTG_OVERRIDE;
	Steinberg::tresult PLUGIN_API process (Steinberg::Vst::ProcessData& data) SMTG_OVERRIDE;
	Steinberg::uint32 PLUGIN_API getProcessContextRequirements () SMTG_OVERRIDE;

private:
	struct ActiveNote
	{
		bool used {false};
		Steinberg::int16 pitch {0};
		Steinberg::int32 noteId {0};
		double endPpq {0.};
	};
	static constexpr int kMaxActive = 16;

	void resetState ();

	ProbeLog log;
	ActiveNote active[kMaxActive];
	uint64_t blockSeq {0};
	bool wasPlaying {false};
	bool havePrev {false};
	double prevEndPpq {0.};
	long long lastBeat {-1};
	Steinberg::int32 nextNoteId {1};
};

}
