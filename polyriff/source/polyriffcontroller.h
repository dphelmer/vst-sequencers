#pragma once

#include "public.sdk/source/vst/vsteditcontroller.h"

#include <cstdint>
#include <random>

namespace Polyriff {

/** Host-generic parameter surface (no VSTGUI).
 *
 * Overrides the string/plain conversions so the host shows real values (21, 70%,
 * Follow, 12345) instead of normalized 0..1 floats. kRandomize is a momentary
 * trigger: any pulse >= 0.5 draws a fresh seed and performs a kSeed edit (which
 * reaches the processor), then the trigger displays 0 again.
 */
class PolyriffController : public Steinberg::Vst::EditController
{
public:
	static Steinberg::FUnknown* createInstance (void*)
	{
		return static_cast<Steinberg::Vst::IEditController*> (new PolyriffController);
	}

	Steinberg::tresult PLUGIN_API initialize (Steinberg::FUnknown* context) SMTG_OVERRIDE;

	// Show plain values (21, 70%, Follow, 12345) instead of the SDK default normalized floats.
	Steinberg::tresult PLUGIN_API getParamStringByValue (Steinberg::Vst::ParamID id,
	                                                     Steinberg::Vst::ParamValue valueNormalized,
	                                                     Steinberg::Vst::String128 string) SMTG_OVERRIDE;
	Steinberg::tresult PLUGIN_API getParamValueByString (Steinberg::Vst::ParamID id,
	                                                     Steinberg::Vst::TChar* string,
	                                                     Steinberg::Vst::ParamValue& valueNormalized) SMTG_OVERRIDE;
	Steinberg::Vst::ParamValue PLUGIN_API normalizedParamToPlain (Steinberg::Vst::ParamID id,
	                                                              Steinberg::Vst::ParamValue valueNormalized) SMTG_OVERRIDE;
	Steinberg::Vst::ParamValue PLUGIN_API plainParamToNormalized (Steinberg::Vst::ParamID id,
	                                                              Steinberg::Vst::ParamValue plainValue) SMTG_OVERRIDE;
	Steinberg::tresult PLUGIN_API setParamNormalized (Steinberg::Vst::ParamID id,
	                                                  Steinberg::Vst::ParamValue valueNormalized) SMTG_OVERRIDE;

private:
	std::mt19937 rng;
	double lastRandomize {0.};
};

}
