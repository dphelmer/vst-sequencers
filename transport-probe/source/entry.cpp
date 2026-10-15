#include "cids.h"
#include "probecontroller.h"
#include "probeprocessor.h"
#include "version.h"

#include "public.sdk/source/main/pluginfactory.h"
#include "pluginterfaces/vst/ivstaudioprocessor.h"

using namespace Steinberg;
using namespace Steinberg::Vst;

BEGIN_FACTORY_DEF (stringCompanyName, stringCompanyWeb, stringCompanyEmail)

	DEF_CLASS2 (INLINE_UID_FROM_FUID (TransportProbe::ProcessorUID), PClassInfo::kManyInstances,
	            kVstAudioEffectClass, stringPluginName, Vst::kDistributable,
	            PlugType::kInstrument, // Live needs an instrument category to accept MIDI-out plug-ins
	            FULL_VERSION_STR, kVstVersionString, TransportProbe::ProbeProcessor::createInstance)

	DEF_CLASS2 (INLINE_UID_FROM_FUID (TransportProbe::ControllerUID), PClassInfo::kManyInstances,
	            kVstComponentControllerClass, stringPluginName "Controller", 0, "",
	            FULL_VERSION_STR, kVstVersionString,
	            TransportProbe::ProbeController::createInstance)

END_FACTORY
