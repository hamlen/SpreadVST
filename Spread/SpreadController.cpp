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

static inline ParamValue normalize(int32 value, int32 num_values)
{
	return ((ParamValue)value + 0.5) / (ParamValue)num_values;
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
	ocParam->getInfo().defaultNormalizedValue = normalize(4, 17);
	parameters.addParameter(ocParam);

	StringListParameter* sParam = new StringListParameter(STR16("Strategy"), kStrategy);
	for (int32 i = 0; i < kNumStrategies; ++i)
		sParam->appendString(strategy_name[i]);
	sParam->getInfo().defaultNormalizedValue = normalize(kMinLoad, kNumStrategies);
	parameters.addParameter(sParam);

	parameters.addParameter(STR16("Sustain"), nullptr, 1, 0., ParameterInfo::kCanAutomate, kSustain);
	parameters.addParameter(STR16("Sostenuto"), nullptr, 1, 0., ParameterInfo::kCanAutomate, kSostenuto);
	parameters.addParameter(STR16("Mute All"), nullptr, 1, 0., ParameterInfo::kCanAutomate, kMuteAll);
	parameters.addParameter(STR16("Release All"), nullptr, 1, 0., ParameterInfo::kCanAutomate, kReleaseAll);
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
	unsigned char val;
	if (!streamer.readUChar8(val))
	{
		LOG("SpreadController::setComponentState failed due to streamer error.\n");
		return kResultFalse;
	}
	if (val > 16) val = 16;
	setParamNormalized(kOutChannels, normalize(val, 16));

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
		case kCtrlResetAllCtrlers:
			tag = kReleaseAll;
			LOG("SpreadController::getMidiControllerAssignment registered kCtrlResetAllCtrlers.\n");
			return kResultTrue;
		}
	}
	LOG("SpreadController:getMidiControllerAssignment exited with failure.\n");
	return kResultFalse;
}
