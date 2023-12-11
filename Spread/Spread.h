#pragma once

#undef LOGGING

#include "public.sdk/source/vst/vsteditcontroller.h"
#include "public.sdk/source/vst/vstaudioeffect.h"
#include "pluginterfaces/vst/ivstevents.h"
#include "base/source/fstring.h"
#include "pluginterfaces/base/funknown.h"

using namespace Steinberg;
using namespace Steinberg::Vst;

constexpr uint32 max_held_notes = 512;
constexpr uint32 initial_note_pool_size = 64;

// Parameter enumeration
enum SpreadParams : ParamID
{
	kOutChannels = 0,
	kStrategy = 1,
	kSustain = 2,
	kSostenuto = 3,
	kMuteAll = 4,
	kReleaseAll = 5,
	kBypass = 6,
	kNumParams = 7
};

enum Strategy : int32
{
	kMinLoad = 0,
	kRoundRobin = 1,
	kRandom = 2,
	kNumStrategies = 3
};

constexpr const TChar* strategy_name[kNumStrategies] = {
	STR16("Min-Load"),
	STR16("Round Robin"),
	STR16("Random")
};

// Plugin processor GUID - must be unique
static const FUID SpreadProcessorUID(0x152C7B8D, 0x71604051, 0x8FD3A939, 0x17EB5368);

// -1 = none
typedef int16 note_pool_index;

// non-negative = note_pool array index
// negative = -pitch - 1
typedef int16 pitch_or_index;
#define PoI_IS_PITCH(poi) ((poi) < 0)
#define PITCH_TO_PoI(pitch) (-(pitch) - 1)
#define PITCH_OF_PoI(poi) (-(poi) - 1)

typedef struct {
	int32 noteId;
	note_pool_index next; // relative offset from current index + 1
	uint8 io_channels;
} note_in_record;

typedef struct {
	uint32 load, susload;
} out_channel_state;

class Spread : public AudioEffect
{
public:
	Spread(void);

	static FUnknown* createInstance(void* context)
	{
		return (IAudioProcessor*) new Spread();
	}

	tresult PLUGIN_API initialize(FUnknown* context);
	tresult PLUGIN_API terminate();
	tresult PLUGIN_API setupProcessing(ProcessSetup& newSetup);
	tresult PLUGIN_API setActive(TBool state);
	tresult PLUGIN_API setProcessing(TBool state);
	tresult PLUGIN_API process(ProcessData& data);
	tresult PLUGIN_API getRoutingInfo(RoutingInfo& inInfo, RoutingInfo& outInfo);
	tresult PLUGIN_API setIoMode(IoMode mode);
	tresult PLUGIN_API setState(IBStream* state);
	tresult PLUGIN_API getState(IBStream* state);
	tresult PLUGIN_API canProcessSampleSize(int32 symbolicSampleSize);
	~Spread(void);

protected:
	inline note_pool_index get_next(pitch_or_index poi);
	inline void set_next(pitch_or_index poi, note_pool_index j);
	int16 delete_next(int32 pitch, pitch_or_index poi, int32* noteId); // returns out_channel of deleted note
	tresult add_note(const Event& note_on_event, int16 out_channel, IEventList* events_out);
	int16 outchannel_of_note(bool delete_it, int16 pitch, int32 noteId, int16 in_channel);
	tresult emergency_evict(IEventList* events_out, const Event& note_on_event);

	void set_outchannels(IEventList* events_out, int16 new_oc, int32 offset);
	void broadcast_event(IEventList* events_out, uint8 cc, uint8 value, int32 offset);
	void press_sustain_pedal(IEventList* events_out, int32 offset);
	void release_sustain_pedal(IEventList* events_out, int32 offset);
	void press_sostenuto_pedal(IEventList* events_out, int32 offset);
	void release_sostenuto_pedal(IEventList* events_out, int32 offset);
	tresult note_on(IEventList* events_out, Event& evt);
	void note_off(IEventList* events_out, Event& evt);
	void polypressure(IEventList* events_out, Event& evt);
	void release_all(IEventList* events_out, int32 offset, TQuarterNotes pos, uint8 cc);

	out_channel_state cstate[16] = {};
	note_pool_index held_notes[128] = {};
	note_in_record* note_pool = nullptr;
	note_pool_index free_list = 0; // if free_list == pool_size then no free slots left in held_notes
	note_pool_index pool_size = 0;
	uint64 soslocked[2] = {};
	uint32 counter = 0; // for generating a uniform distribution of values non-randomly
	int32 strategy = kMinLoad;
	int16 out_channels = 4;
	int16 roundrobin_channel = 0;
	bool sustain_pedal_down = false;
	bool sostenuto_pedal_down = false;
	bool bypass = false;
	bool initial_points_sent = false;
};

#ifdef LOGGING
	void log(const char* format, ...);
#	define LOG(format, ...) log((format), __VA_ARGS__)
#else
#	define LOG(format, ...) 0
#endif
