#include "pluginterfaces/base/ibstream.h"
#include "base/source/fstreamer.h"
#include <pluginterfaces/vst/ivstmidicontrollers.h>

#include "Spread.h"
#include "SpreadController.h"

SpreadController::SpreadController(void)
{
	LOG("SpreadController constructor called and exited.\n");
}

SpreadController::~SpreadController(void)
{
	LOG("SpreadController destructor called and exited.\n");
}

tresult PLUGIN_API SpreadController::queryInterface(const char* iid, void** obj)
{
	QUERY_INTERFACE(iid, obj, IMidiMapping::iid, IMidiMapping)
	return EditController::queryInterface(iid, obj);
}

static inline ParamValue normalize(int32 value, int32 max_value)
{
	return (value <= 0) ? 0.0 : (value >= max_value) ? 1.0 : (((ParamValue)value + 0.5) / (ParamValue)(max_value + 1));
}

static void uint32_to_str16(TChar* p, uint32 n)
{
	if (n == 0)
	{
		*p++ = u'0';
		*p = 0;
	}
	else
	{
		uint32 width = 0;
		for (uint32 i = n; i; i /= 10)
			++width;

		*(p + width) = 0;
		for (uint32 i = n; width > 0; i /= 10)
			*(p + --width) = STR16("0123456789")[i % 10];
	}
}

tresult PLUGIN_API SpreadController::initialize(FUnknown* context)
{
	LOG("SpreadController::initialize called.\n");
	tresult result = EditController::initialize(context);

	if (result != kResultOk)
	{
		LOG("SpreadController::initialize exited prematurely with code %d.\n", result);
		return result;
	}

	TChar ocString[3];
	StringListParameter* ocParam = new StringListParameter(STR16("OutChannels"), kOutChannels);
	ocParam->appendString(STR16("Preserve"));
	for (int32 i = 1; i <= 16; ++i)
	{
		uint32_to_str16(ocString, i);
		ocParam->appendString(ocString);
	}
	ocParam->getInfo().defaultNormalizedValue = normalize(4, 16);
	parameters.addParameter(ocParam);

	StringListParameter* sParam = new StringListParameter(STR16("Strategy"), kStrategy);
	for (int32 i = 0; i < kNumStrategies; ++i)
		sParam->appendString(strategy_name[i]);
	sParam->getInfo().defaultNormalizedValue = normalize(kMinLoad, kNumStrategies - 1);
	parameters.addParameter(sParam);

	parameters.addParameter(STR16("Sustain"), nullptr, 1, 0., ParameterInfo::kCanAutomate, kSustain);
	parameters.addParameter(STR16("Sostenuto"), nullptr, 1, 0., ParameterInfo::kCanAutomate, kSostenuto);

	StringListParameter* muteAllParam = new StringListParameter(STR16("Mute All"), kMuteAll);
	muteAllParam->appendString(STR16("Triggered"));
	muteAllParam->appendString(STR16("--"));
	muteAllParam->getInfo().defaultNormalizedValue = 1.;
	parameters.addParameter(muteAllParam);

	StringListParameter* releaseAllParam = new StringListParameter(STR16("Release All"), kReleaseAll);
	releaseAllParam->appendString(STR16("Triggered"));
	releaseAllParam->appendString(STR16("--"));
	releaseAllParam->getInfo().defaultNormalizedValue = 1.;
	parameters.addParameter(releaseAllParam);

	parameters.addParameter(STR16("Bypass"), nullptr, 1, 0., ParameterInfo::kIsBypass, kBypass);

	LOG("SpreadController::initialize exited normally with code %d.\n", result);
	return result;
}

tresult PLUGIN_API SpreadController::terminate()
{
	LOG("SpreadController::terminate called.\n");
	tresult result = EditController::terminate();
	LOG("SpreadController::terminate exited with code %d.\n", result);
	return result;
}

tresult PLUGIN_API SpreadController::setComponentState(IBStream* state)
{
	LOG("SpreadController::setComponentState called.\n");
	if (!state)
	{
		LOG("SpreadController::setComponentState failed because no state argument provided.\n");
		return kResultFalse;
	}

	IBStreamer streamer(state, kLittleEndian);

	unsigned char loaded_oc;
	if (!streamer.readUChar8(loaded_oc))
		loaded_oc = 4;
	else if (loaded_oc > 16)
		return kResultFalse;

	int32 loaded_strat;
	if (!streamer.readInt32(loaded_strat))
		loaded_strat = kMinLoad;
	else if ((loaded_strat < 0) || (loaded_strat >= kNumStrategies))
		return kResultFalse;

	setParamNormalized(kOutChannels, normalize(loaded_oc, 16));
	setParamNormalized(kStrategy, normalize(loaded_strat, kNumStrategies - 1));

	LOG("SpreadController::setComponentState exited normally.\n");
	return kResultOk;
}

tresult PLUGIN_API SpreadController::getMidiControllerAssignment(int32 busIndex, int16 midiChannel, CtrlNumber midiControllerNumber, ParamID& tag)
{
	LOG("SpreadController::getMidiControllerAssignment called.\n");
	if (busIndex == 0)
	{
		switch (midiControllerNumber)
		{
		case kCtrlSustainOnOff:
			tag = kSustain;
			LOG("SpreadController::getMidiControllerAssignment registered kCtrlSustainOnOff.\n");
			return kResultTrue;
		case kCtrlSustenutoOnOff:
			tag = kSostenuto;
			LOG("SpreadController::getMidiControllerAssignment registered kCtrlSustenutoOnOff.\n");
			return kResultTrue;
		case kCtrlAllSoundsOff:
			tag = kMuteAll;
			LOG("SpreadController::getMidiControllerAssignment registered kCtrlAllSoundsOff.\n");
			return kResultTrue;
		case kCtrlAllNotesOff:
			tag = kReleaseAll;
			LOG("SpreadController::getMidiControllerAssignment registered kCtrlResetAllCtrlers.\n");
			return kResultTrue;
		}
	}
	LOG("SpreadController:getMidiControllerAssignment exited with failure.\n");
	return kResultFalse;
}
