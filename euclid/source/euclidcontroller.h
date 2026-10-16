#pragma once

#include "public.sdk/source/vst/vsteditcontroller.h"
#include "vstgui/plugin-bindings/vst3editor.h"

namespace Euclid {

class EuclidController : public Steinberg::Vst::EditController
{
public:
	static Steinberg::FUnknown* createInstance (void*)
	{
		return static_cast<Steinberg::Vst::IEditController*> (new EuclidController);
	}

	Steinberg::tresult PLUGIN_API initialize (Steinberg::FUnknown* context) SMTG_OVERRIDE;
	Steinberg::IPlugView* PLUGIN_API createView (Steinberg::FIDString name) SMTG_OVERRIDE;

	// Show plain values (16, C3, 50%, On) instead of the SDK default normalized 0..1 floats.
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
};

}
