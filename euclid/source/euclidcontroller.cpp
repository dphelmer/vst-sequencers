#include "euclidcontroller.h"
#include "params.h"

#include "pluginterfaces/base/ustring.h"

#include <cstdio>
#include <cstring>

using namespace Steinberg;
using namespace Steinberg::Vst;

namespace Euclid {

namespace {
// per channel defaults: {pulses, steps, rotate, pitch, velocity}
constexpr int kDefaults[kNumChannels][5] = {
	{4, 16, 0, 36, 100}, // kick 4-on-floor
	{2, 16, 4, 38, 110}, // snare on 2 & 4
	{8, 16, 0, 42, 80},  // 8th-note hats
	{3, 16, 0, 46, 90},  // open hat
};

// snprintf into a char buffer, then widen into a TChar buffer.
void wideName (TChar* out, size_t outSize, const char* fmt, int ch)
{
	char tmp[32];
	std::snprintf (tmp, sizeof (tmp), fmt, ch);
	size_t i = 0;
	for (; tmp[i] && i + 1 < outSize; ++i)
		out[i] = static_cast<TChar> (tmp[i]);
	out[i] = 0;
}
}

//------------------------------------------------------------------------
tresult PLUGIN_API EuclidController::initialize (FUnknown* context)
{
	tresult result = EditController::initialize (context);
	if (result != kResultOk)
		return result;

	TChar name[32];
	for (int ch = 0; ch < kNumChannels; ++ch)
	{
		const ParamID base = static_cast<ParamID> (ch * kParamsPerChannel);

		wideName (name, 32, "Ch%d Pulses", ch + 1);
		parameters.addParameter (name, nullptr, kMaxSteps, kDefaults[ch][0] / (double)kMaxSteps,
		                         ParameterInfo::kCanAutomate, base + kPulses);

		wideName (name, 32, "Ch%d Steps", ch + 1);
		parameters.addParameter (name, nullptr, kMaxSteps - 1,
		                         (kDefaults[ch][1] - 1) / (double)(kMaxSteps - 1),
		                         ParameterInfo::kCanAutomate, base + kSteps);

		wideName (name, 32, "Ch%d Rotate", ch + 1);
		parameters.addParameter (name, nullptr, kMaxSteps - 1, kDefaults[ch][2] / (double)(kMaxSteps - 1),
		                         ParameterInfo::kCanAutomate, base + kRotate);

		wideName (name, 32, "Ch%d Pitch", ch + 1);
		parameters.addParameter (name, nullptr, 127, kDefaults[ch][3] / 127.,
		                         ParameterInfo::kCanAutomate, base + kPitch);

		wideName (name, 32, "Ch%d Velocity", ch + 1);
		parameters.addParameter (name, nullptr, 127, kDefaults[ch][4] / 127.,
		                         ParameterInfo::kCanAutomate, base + kVelocity);

		wideName (name, 32, "Ch%d Gate", ch + 1);
		parameters.addParameter (name, nullptr, 0, 0.5, ParameterInfo::kCanAutomate, base + kGate);

		wideName (name, 32, "Ch%d Mute", ch + 1);
		parameters.addParameter (name, nullptr, 1, 0, ParameterInfo::kCanAutomate, base + kMute);
	}

	const TChar midiChName[] = {'M','I','D','I',' ','C','h','a','n','n','e','l',0};
	parameters.addParameter (midiChName, nullptr, 15, 9 / 15., ParameterInfo::kCanAutomate, kMidiChannel);

	return result;
}

//------------------------------------------------------------------------
Steinberg::IPlugView* PLUGIN_API EuclidController::createView (Steinberg::FIDString name)
{
	if (strcmp (name, Steinberg::Vst::ViewType::kEditor) == 0)
		return new VSTGUI::VST3Editor (this, "view", "euclid.uidesc");
	return nullptr;
}

//------------------------------------------------------------------------
tresult PLUGIN_API EuclidController::getParamStringByValue (Steinberg::Vst::ParamID id,
                                                             Steinberg::Vst::ParamValue valueNormalized,
                                                             Steinberg::Vst::String128 string)
{
	if (!getParameterObject (id))
		return EditController::getParamStringByValue (id, valueNormalized, string);
	char buf[32];
	if (!paramToString (id, valueNormalized, buf, sizeof (buf)))
		return EditController::getParamStringByValue (id, valueNormalized, string);
	UString (string, str16BufferSize (Steinberg::Vst::String128)).assign (buf);
	return kResultTrue;
}

tresult PLUGIN_API EuclidController::getParamValueByString (Steinberg::Vst::ParamID id,
                                                             Steinberg::Vst::TChar* string,
                                                             Steinberg::Vst::ParamValue& valueNormalized)
{
	if (!getParameterObject (id))
		return EditController::getParamValueByString (id, string, valueNormalized);
	char buf[64];
	UString (const_cast<Steinberg::Vst::TChar*> (string), tstrlen (string)).toAscii (buf, sizeof (buf));
	return paramFromString (id, buf, valueNormalized) ? kResultTrue : kResultFalse;
}

Steinberg::Vst::ParamValue PLUGIN_API EuclidController::normalizedParamToPlain (Steinberg::Vst::ParamID id,
                                                                                Steinberg::Vst::ParamValue valueNormalized)
{
	if (!getParameterObject (id))
		return EditController::normalizedParamToPlain (id, valueNormalized);
	return plainFromNorm (id, valueNormalized);
}

Steinberg::Vst::ParamValue PLUGIN_API EuclidController::plainParamToNormalized (Steinberg::Vst::ParamID id,
                                                                                Steinberg::Vst::ParamValue plainValue)
{
	if (!getParameterObject (id))
		return EditController::plainParamToNormalized (id, plainValue);
	return normFromPlain (id, plainValue);
}

}
