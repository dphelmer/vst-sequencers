#include "cids.h"
#include "polyriffcontroller.h"
#include "polyriffprocessor.h"
#include "version.h"

#include "public.sdk/source/main/pluginfactory.h"
#include "pluginterfaces/vst/ivstaudioprocessor.h"

using namespace Steinberg;
using namespace Steinberg::Vst;

BEGIN_FACTORY_DEF (stringCompanyName, stringCompanyWeb, stringCompanyEmail)

	DEF_CLASS2 (INLINE_UID_FROM_FUID (Polyriff::ProcessorUID), PClassInfo::kManyInstances,
	            kVstAudioEffectClass, stringPluginName, Vst::kDistributable,
	            PlugType::kInstrument, // Live needs an instrument category to accept MIDI-out plug-ins
	            FULL_VERSION_STR, kVstVersionString, Polyriff::PolyriffProcessor::createInstance)

	DEF_CLASS2 (INLINE_UID_FROM_FUID (Polyriff::ControllerUID), PClassInfo::kManyInstances,
	            kVstComponentControllerClass, stringPluginName "Controller", 0, "",
	            FULL_VERSION_STR, kVstVersionString,
	            Polyriff::PolyriffController::createInstance)

END_FACTORY
