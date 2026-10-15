#pragma once

#include "public.sdk/source/vst/vsteditcontroller.h"

namespace TransportProbe {

// No parameters, no GUI. Exists because VST3 wants a controller class.
class ProbeController : public Steinberg::Vst::EditController
{
public:
	static Steinberg::FUnknown* createInstance (void*)
	{
		return static_cast<Steinberg::Vst::IEditController*> (new ProbeController);
	}
};

}
