#include "polyriffcontroller.h"
#include "params.h"

#include "pluginterfaces/base/ustring.h"

#include <chrono>
#include <cstdio>
#include <cstring>

using namespace Steinberg;
using namespace Steinberg::Vst;

namespace Polyriff {

namespace {
// Two random_device draws plus the clock: distinct across sessions and across
// consecutive presses even on platforms where random_device is deterministic.
std::mt19937 makeRng ()
{
	std::random_device rd;
	const auto now = static_cast<std::uint64_t> (
	    std::chrono::steady_clock::now ().time_since_epoch ().count ());
	std::seed_seq seq {static_cast<std::uint32_t> (rd ()), static_cast<std::uint32_t> (rd ()),
	                   static_cast<std::uint32_t> (now), static_cast<std::uint32_t> (now >> 32)};
	return std::mt19937 {seq};
}
}

//------------------------------------------------------------------------
Steinberg::tresult PLUGIN_API PolyriffController::initialize (FUnknown* context)
{
	tresult result = EditController::initialize (context);
	if (result != kResultOk)
		return result;

	static const char* titles[kNumParams] = {"Seed",        "Riff Length", "Hyper Bars", "Density",
	                                         "Group Spread","Pitch Low",   "Pitch Range","Pitch Density",
	                                         "Kick Mode",   "Accent Velocity", "Gate",  "Slide",
	                                         "Riff Channel","Kick Channel", "Randomize"};

	for (int i = 0; i < kNumParams; ++i)
	{
		const ParamID id = static_cast<ParamID> (i);
		TChar wide[64];
		size_t k = 0;
		for (; titles[i][k] && k + 1 < 64; ++k)
			wide[k] = static_cast<TChar> (titles[i][k]);
		wide[k] = 0;
		// kRandomize is momentary: keep it out of automation lanes.
		const int32 flags = id == kRandomize ? 0 : ParameterInfo::kCanAutomate;
		parameters.addParameter (wide, nullptr, paramStepCount (id), normFromPlain (id, defaultPlain (id)),
		                         flags, id);
	}
	return result;
}

//------------------------------------------------------------------------
tresult PLUGIN_API PolyriffController::getParamStringByValue (Steinberg::Vst::ParamID id, ParamValue valueNormalized,
                                                              String128 string)
{
	if (!getParameterObject (id))
		return EditController::getParamStringByValue (id, valueNormalized, string);
	char buf[32];
	if (!paramToString (id, valueNormalized, buf, sizeof (buf)))
		return EditController::getParamStringByValue (id, valueNormalized, string);
	UString (string, str16BufferSize (String128)).assign (buf);
	return kResultTrue;
}

tresult PLUGIN_API PolyriffController::getParamValueByString (Steinberg::Vst::ParamID id, TChar* string,
                                                              ParamValue& valueNormalized)
{
	if (!getParameterObject (id))
		return EditController::getParamValueByString (id, string, valueNormalized);
	char buf[64];
	UString (const_cast<TChar*> (string), tstrlen (string)).toAscii (buf, sizeof (buf));
	return paramFromString (id, buf, valueNormalized) ? kResultTrue : kResultFalse;
}

ParamValue PLUGIN_API PolyriffController::normalizedParamToPlain (Steinberg::Vst::ParamID id, ParamValue valueNormalized)
{
	if (!getParameterObject (id))
		return EditController::normalizedParamToPlain (id, valueNormalized);
	return plainFromNorm (id, valueNormalized);
}

ParamValue PLUGIN_API PolyriffController::plainParamToNormalized (Steinberg::Vst::ParamID id, ParamValue plainValue)
{
	if (!getParameterObject (id))
		return EditController::plainParamToNormalized (id, plainValue);
	return normFromPlain (id, plainValue);
}

//------------------------------------------------------------------------
tresult PLUGIN_API PolyriffController::setParamNormalized (Steinberg::Vst::ParamID id, ParamValue valueNormalized)
{
	if (id == kRandomize)
	{
		// Rising edge across received values -> one new seed per press/gesture (a drag
		// may cross the edge several times; the last kSeed edit wins, which is fine).
		// The trigger itself always displays 0.
		const bool fire = valueNormalized >= 0.5 && lastRandomize < 0.5;
		lastRandomize = fire ? 0. : valueNormalized;
		if (fire)
		{
			const double plain = static_cast<double> (rng () & 0xffffffffu);
			const double ns = normFromPlain (kSeed, plain);
			EditController::setParamNormalized (kSeed, ns);
			performEdit (kSeed, ns); // -> host -> processor (single source of the seed)
			performEdit (kRandomize, 0.);
		}
		return EditController::setParamNormalized (kRandomize, 0.);
	}
	return EditController::setParamNormalized (id, valueNormalized);
}

}
