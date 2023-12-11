#include "Spread.h"
#include "SpreadController.h"

#include "public.sdk/source/main/pluginfactory.h"

#define PluginCategory "Fx"
#define PluginName "Spread"

#define PLUGINVERSION "1.1.0"

bool InitModule()
{
	LOG("InitModule called and exited.\n");
	return true;
}

bool DeinitModule()
{
	LOG("DeinitModule called and exited.\n");
	return true;
}

BEGIN_FACTORY_DEF("Kevin Hamlen",
	"https://github.com/hamlen/SpreadVST",
	"no contact")

	LOG("GetPluginFactory called.\n");

	DEF_CLASS2(INLINE_UID_FROM_FUID(SpreadProcessorUID),
		PClassInfo::kManyInstances,
		kVstAudioEffectClass,
		PluginName,
		Vst::kDistributable,
		PluginCategory,
		PLUGINVERSION,
		kVstVersionString,
		Spread::createInstance)

	DEF_CLASS2(INLINE_UID_FROM_FUID(SpreadControllerUID),
		PClassInfo::kManyInstances,
		kVstComponentControllerClass,
		PluginName "Controller",
		0, // unused
		"", //unused
		PLUGINVERSION,
		kVstVersionString,
		SpreadController::createInstance)

END_FACTORY
