#include "public.sdk/source/vst/vstaudioprocessoralgo.h"

#include "pluginterfaces/vst/ivstevents.h"
#include "pluginterfaces/vst/ivstmidicontrollers.h"
#include "pluginterfaces/vst/ivstparameterchanges.h"
#include "pluginterfaces/vst/ivstprocesscontext.h"
#include "pluginterfaces/base/ibstream.h"
#include "base/source/fstreamer.h"

#include "Spread.h"
#include "SpreadController.h"

Spread::Spread(void)
{
	LOG("Spread constructor called.\n");
	setControllerClass(FUID(SpreadControllerUID));
	processSetup.maxSamplesPerBlock = kMaxInt32;
	LOG("Spread constructor exited.\n");
}

Spread::~Spread(void)
{
	LOG("Spread destructor called.\n");
	if (note_pool)
	{
		free(note_pool);
		note_pool = nullptr;
		pool_size = 0;
	}
	LOG("Spread destructor exited.\n");
}

tresult PLUGIN_API Spread::initialize(FUnknown* context)
{
	LOG("Spread::initialize called.\n");
	tresult result = AudioEffect::initialize(context);

	if (result != kResultOk)
	{
		LOG("Spread::initialize failed with code %d.\n", result);
		return result;
	}

	addEventInput(STR16("Event In"));
	addEventOutput(STR16("Event Out"));
	counter = 0;

	LOG("Spread::initialize exited normally.\n");
	return kResultOk;
}

tresult PLUGIN_API Spread::terminate()
{
	LOG("Spread::terminate called.\n");
	tresult result = AudioEffect::terminate();
	LOG("Spread::terminate exited with code %d.\n", result);
	return result;
}

tresult PLUGIN_API Spread::setActive(TBool state)
{
	LOG("Spread::setActive called.\n");
	tresult result = AudioEffect::setActive(state);
	if (state)
		counter = 0;
	LOG("Spread::setActive exited with code %d.\n", result);
	return result;
}

tresult PLUGIN_API Spread::setIoMode(IoMode mode)
{
	LOG("Spread::setIoMode called and exited.\n");
	return kResultOk;
}

tresult PLUGIN_API Spread::setProcessing(TBool state)
{
	if (state)
	{
		counter = 0;
		srand(0);
	}
	initial_points_sent = false;
	LOG("Spread::setProcessing called and exited.\n");
	return kResultOk;
}

tresult PLUGIN_API Spread::setState(IBStream* s)
{
	LOG("Spread::setState called.\n");

	IBStreamer streamer(s, kLittleEndian);
	unsigned char loaded_oc;
	int32 loaded_strat;

	if (!streamer.readUChar8(loaded_oc))
	{
		loaded_oc = 4;
		loaded_strat = kMinLoad;
	}
	else if (!streamer.readInt32(loaded_strat))
		loaded_strat = kMinLoad;

	if ((loaded_oc > 16) || (loaded_strat < 0) || (loaded_strat >= kNumStrategies))
		return kResultFalse;

	out_channels = loaded_oc;
	strategy = loaded_strat;
	counter = 0;
	srand(0);

	LOG("Spread::setState exited successfully.\n");
	return kResultOk;
}

tresult PLUGIN_API Spread::getState(IBStream* s)
{
	LOG("Spread::getState called.\n");

	IBStreamer streamer(s, kLittleEndian);
	if (!streamer.writeUChar8(out_channels) || !streamer.writeInt32(strategy))
	{
		LOG("Spread::getState failed due to streamer error.\n");
		return kResultFalse;
	}

	LOG("Spread::getState exited successfully.\n");
	return kResultOk;
}

tresult PLUGIN_API Spread::setupProcessing(ProcessSetup& newSetup)
{
	LOG("Spread::setupProcessing called.\n");
	processContextRequirements.flags = kNeedProjectTimeMusic | kNeedTempo;
	tresult result = AudioEffect::setupProcessing(newSetup);
	LOG("Spread::setupProcessing exited with code %d.\n", result);
	return result;
}

tresult PLUGIN_API Spread::canProcessSampleSize(int32 symbolicSampleSize)
{
	LOG("Spread::canProcessSapmleSize called with arg %d and exited.\n", symbolicSampleSize);
	return (symbolicSampleSize == kSample32 || symbolicSampleSize == kSample64) ? kResultTrue : kResultFalse;
}

tresult PLUGIN_API Spread::getRoutingInfo(RoutingInfo& inInfo, RoutingInfo& outInfo)
{
	LOG("Spread::getRoutingInfo called.\n");
	if (inInfo.mediaType == kEvent && inInfo.busIndex == 0)
	{
		outInfo = inInfo;
		LOG("Spread::getRoutingInfo exited with success.\n");
		return kResultOk;
	}
	else
	{
		LOG("Spread::getRoutingInfo exited with failure.\n");
		return kResultFalse;
	}
}

static inline ParamValue normalize(int32 value, int32 max_value)
{
	return (value <= 0) ? 0.0 : (value >= max_value) ? 1.0 : (((ParamValue)value + 0.5) / (ParamValue)(max_value + 1));
}

static inline int32 discretize(ParamValue value, int32 max_value)
{
	const int32 discrete = (int32)(value * (ParamValue)(max_value + 1));
	return (discrete <= 0) ? 0 : (discrete >= max_value) ? max_value : discrete;
}

inline note_pool_index Spread::get_next(pitch_or_index poi)
{
	return PoI_IS_PITCH(poi) ? (held_notes[PITCH_OF_PoI(poi)] - 1) : (poi + 1 + note_pool[poi].next);
}

inline void Spread::set_next(pitch_or_index poi, note_pool_index j)
{
	if (PoI_IS_PITCH(poi))
		held_notes[PITCH_OF_PoI(poi)] = j + 1;
	else
		note_pool[poi].next = j - (poi + 1);
}

int16 Spread::delete_next(int32 pitch, pitch_or_index poi, int32* noteId)
{
	note_pool_index j = get_next(poi);
	if ((j < 0) || (j >= pool_size))
		return -1;

	int16 out_channel = note_pool[j].io_channels & 0xF;
	if (cstate[out_channel].load > 0)
		--cstate[out_channel].load;
	if ((sustain_pedal_down || (soslocked[pitch / 64] & (1ULL << pitch))) && (out_channel < out_channels))
		++cstate[out_channel].susload;

	if (noteId)
		*noteId = note_pool[j].noteId;

	set_next(poi, get_next(j));
	set_next(j, free_list);
	free_list = j;
		
	return out_channel;
}

int16 Spread::outchannel_of_note(bool delete_it, int16 pitch, int32 noteId, int16 in_channel)
{
	pitch_or_index prev = PITCH_TO_PoI(pitch);
	for (note_pool_index i = get_next(prev); (0 <= i) && (i < pool_size); i = get_next((prev = i)))
	{
		if ((note_pool[i].noteId == noteId) && ((note_pool[i].io_channels >> 4) == in_channel))
			return delete_it ? delete_next(pitch, prev, nullptr) : (note_pool[i].io_channels & 0xF);
	}

	return -1;
}

tresult Spread::emergency_evict(IEventList* events_out, const Event& note_on_event)
{
	if (events_out)
	{
		int16 held_pitch = -1;
		for (int16 pitch = 0; pitch < 128; ++pitch)
		{
			const note_pool_index i = get_next(PITCH_TO_PoI(pitch));
			if (i >= 0)
			{
				if (get_next(i) >= 0)
				{
					held_pitch = pitch;
					break;
				}
				else if (held_pitch < 0)
					held_pitch = pitch;
			}
		}
		if (held_pitch < 0)
			return kResultFalse;

		const int32 id = note_pool[get_next(PITCH_TO_PoI(held_pitch))].noteId;
		const int16 out_channel = delete_next(held_pitch, PITCH_TO_PoI(held_pitch), nullptr);
		if (out_channel < 0)
			return kResultFalse;
		else
		{
			Event evt = {};
			evt.sampleOffset = note_on_event.sampleOffset;
			evt.ppqPosition = note_on_event.ppqPosition;
			evt.type = Event::kNoteOffEvent;
			evt.noteOff.channel = out_channel;
			evt.noteOff.noteId = id;
			evt.noteOff.pitch = held_pitch;
			evt.noteOff.velocity = 1.F;
			events_out->addEvent(evt);
		}

		return kResultOk;
	}
	else
		return kResultFalse;
}

tresult Spread::add_note(const Event& note_on_event, int16 out_channel, IEventList* events_out)
{
	if (free_list >= pool_size)
	{
		if (pool_size >= max_held_notes)
		{
			if (emergency_evict(events_out, note_on_event) != kResultOk)
				return kResultFalse;
		}
		else
		{
			free_list = pool_size;
			pool_size = (pool_size <= 0) ? initial_note_pool_size : (pool_size * 2);
			if (pool_size > max_held_notes)
				pool_size = max_held_notes;
			note_in_record* const new_pool = (note_in_record*)realloc(note_pool, pool_size * sizeof(*note_pool));
			if (new_pool)
			{
				note_pool = new_pool;
				memset(note_pool + free_list, 0, ((size_t)pool_size - (size_t)free_list) * sizeof(*note_pool));
			}
			else
			{
				pool_size = free_list;
				if (emergency_evict(events_out, note_on_event) != kResultOk)
					return kResultFalse;
			}
		}
	}

	const note_pool_index slot = free_list;
	if (slot >= pool_size)
		return kResultFalse;
	free_list = get_next(free_list);
	note_pool[slot].noteId = note_on_event.noteOn.noteId;
	note_pool[slot].io_channels = (note_on_event.noteOn.channel << 4) | out_channel;
	set_next(slot, -1);

	for (pitch_or_index poi = PITCH_TO_PoI(note_on_event.noteOn.pitch), next = get_next(poi); ; next = get_next((poi = next)))
	{
		if (next < 0)
		{
			set_next(poi, slot);
			break;
		}
	}

	++cstate[out_channel].load;

	return kResultOk;
}

void Spread::set_outchannels(IEventList* events_out, int16 new_oc, int32 offset)
{
	if (sustain_pedal_down)
	{
		// clear sustain-load counters for channels dropped from the output spread
		for (int16 c = new_oc; c < out_channels; ++c)
			cstate[c].susload = 0;

		if (events_out)
		{
			Event e = {};
			e.type = Event::EventTypes::kLegacyMIDICCOutEvent;
			e.sampleOffset = offset;
			e.midiCCOut.controlNumber = kCtrlSustainOnOff;

			// send pedal-off to channels dropped from the output spread
			for (int16 c = new_oc; c < out_channels; ++c)
			{
				e.midiCCOut.channel = c;
				events_out->addEvent(e);
			}

			// send pedal-on to channels added to the output spread
			e.midiCCOut.value = 127;
			for (int16 c = out_channels; c < new_oc; ++c)
			{
				e.midiCCOut.channel = c;
				events_out->addEvent(e);
			}
		}
	}
	out_channels = new_oc;
}

void Spread::broadcast_event(IEventList* events_out, uint8 cc, uint8 value, int32 offset)
{
	if (events_out)
	{
		Event e = {};
		e.type = Event::EventTypes::kLegacyMIDICCOutEvent;
		e.sampleOffset = offset;
		e.midiCCOut.controlNumber = cc;
		e.midiCCOut.value = value;
		for (int16 c = 0; c < out_channels; ++c)
		{
			e.midiCCOut.channel = c;
			events_out->addEvent(e);
		}
	}
}

void Spread::press_sustain_pedal(IEventList* events_out, int32 offset)
{
	if (!sustain_pedal_down)
	{
		sustain_pedal_down = true;
		broadcast_event(events_out, kCtrlSustainOnOff, 127, offset);
	}
}

void Spread::release_sustain_pedal(IEventList* events_out, int32 offset)
{
	if (sustain_pedal_down)
	{
		sustain_pedal_down = false;
		for (int16 c = 0; c < out_channels; ++c)
			cstate[c].susload = 0;
		broadcast_event(events_out, kCtrlSustainOnOff, 0, offset);
	}
}

void Spread::press_sostenuto_pedal(IEventList* events_out, int32 offset)
{
	if (!sostenuto_pedal_down)
	{
		sostenuto_pedal_down = true;
		broadcast_event(events_out, kCtrlSustenutoOnOff, 127, offset);
	}

	for (int32 pitch = 0; pitch < 128; ++pitch)
	{
		if (held_notes[pitch])
			soslocked[pitch / 64] |= 1ULL << (pitch % 64);
	}
}

void Spread::release_sostenuto_pedal(IEventList* events_out, int32 offset)
{
	if (sostenuto_pedal_down)
	{
		sostenuto_pedal_down = false;
		broadcast_event(events_out, kCtrlSustenutoOnOff, 0, offset);
	}

	soslocked[0] = soslocked[1] = 0;
}

tresult Spread::note_on(IEventList* events_out, Event& evt)
{
	const int16 in_channel = evt.noteOn.channel;
	const int16 pitch = evt.noteOn.pitch;
	if ((0 <= in_channel) && (in_channel < 16) && (0 <= pitch) && (pitch < 128))
	{
		int16 out_channel = in_channel;
		if (!bypass && (out_channels > 0))
		{
			switch (strategy)
			{
				case kMinLoad:
				{
					uint32 lowest_load = UINT32_MAX;
					uint32 tally = 0;
					++counter;
					for (int16 i = 0; i < out_channels; ++i)
					{
						uint32 this_load = cstate[i].load + cstate[i].susload;
						if (this_load < lowest_load)
						{
							out_channel = i;
							lowest_load = this_load;
							tally = 1;
						}
						else if (this_load == lowest_load)
						{
							if (counter % ++tally == 0)
								out_channel = i;
						}
					}
				}
				break;

				case kRoundRobin:
				{
					if (roundrobin_channel >= out_channels)
						roundrobin_channel = 0;
					out_channel = roundrobin_channel;
					++roundrobin_channel;
				}
				break;

				case kRandom:
				{
					const int max = RAND_MAX - RAND_MAX % out_channels;
					int r;
					do
					{
						r = rand();
					} while (r >= max);
					out_channel = r % out_channels;
				}
				break;
			}
		}

		if (add_note(evt, out_channel, events_out) != kResultOk)
			return kResultFalse;

		evt.noteOn.channel = out_channel;
		if (events_out)
			events_out->addEvent(evt);
	}
	return kResultOk;
}

void Spread::note_off(IEventList* events_out, Event& evt)
{
	const int16 in_channel = evt.noteOff.channel;
	const int16 pitch = evt.noteOff.pitch;
	if ((0 <= in_channel) && (in_channel < 16) && (0 <= pitch) && (pitch < 128))
	{
		const int16 out_channel = outchannel_of_note(true, pitch, evt.noteOff.noteId, in_channel);
		if (out_channel >= 0)
		{
			evt.noteOff.channel = out_channel;
			if (events_out)
				events_out->addEvent(evt);
		}
		// Note-off without preceding note-on is ignored.
	}
}

void Spread::polypressure(IEventList* events_out, Event& evt)
{
	int16 in_channel = evt.polyPressure.channel;
	int16 pitch = evt.polyPressure.pitch;
	if (events_out && (0 <= in_channel) && (in_channel < 16) && (0 <= pitch) && (pitch < 128))
	{
		int16 out_channel = outchannel_of_note(false, pitch, evt.polyPressure.noteId, in_channel);
		if (out_channel >= 0)
		{
			evt.polyPressure.channel = out_channel;
			events_out->addEvent(evt);
		}
		// Poly-pressure without preceding note-on is ignored.
	}
}

void Spread::release_all(IEventList* events_out, int32 offset, TQuarterNotes pos, uint8 cc)
{
	if (events_out)
	{
		Event evt = {};
		evt.sampleOffset = offset;
		evt.ppqPosition = pos;
		evt.type = Event::kNoteOffEvent;
		evt.noteOff.velocity = 1.;

		for (int16 pitch = 0; pitch < 128; ++pitch)
		{
			evt.noteOff.pitch = pitch;
			for (;;)
			{
				evt.noteOff.channel = delete_next(pitch, PITCH_TO_PoI(pitch), &evt.noteOff.noteId);
				if (evt.noteOff.channel >= 0)
					events_out->addEvent(evt);
				else
					break;
			}
		}

		evt.type = Event::kLegacyMIDICCOutEvent;
		evt.midiCCOut.controlNumber = cc;
		evt.midiCCOut.value = evt.midiCCOut.value2 = 0;
		const int16 n = (out_channels <= 0) ? 16 : out_channels;
		for (evt.midiCCOut.channel = 0; evt.midiCCOut.channel < n; ++evt.midiCCOut.channel)
			events_out->addEvent(evt);
	}
}

static tresult set_parameter(IParameterChanges* params_out, IParamValueQueue*& queue, ParamID id, int32 offset, ParamValue value)
{
	if (params_out)
	{
		int32 dummy;
		if (!queue)
			queue = params_out->addParameterData(id, dummy);
		if (queue)
			return queue->addPoint(offset, value, dummy);
	}
	return kResultFalse;
}

tresult PLUGIN_API Spread::process(ProcessData& data)
{
	// We shouldn't be asked for audio output, but process it anyway (emit silence) to accommodate uncompliant hosts.
	bool is32bit = (data.symbolicSampleSize == kSample32);
	if (is32bit || (data.symbolicSampleSize == kSample64))
	{
		for (int32 i = 0; i < data.numOutputs; ++i)
		{
			for (int32 j = 0; j < data.outputs[i].numChannels; ++j)
			{
				void* buffer = is32bit ? (void*)data.outputs[i].channelBuffers32[j] : (void*)data.outputs[i].channelBuffers64[j];
				if (buffer)
					memset(buffer, 0, data.numSamples * (is32bit ? sizeof(*data.outputs[i].channelBuffers32[j]) : sizeof(*data.outputs[i].channelBuffers64[j])));
			}
		}
	}
	for (int32 i = 0; i < data.numOutputs; ++i)
		data.outputs[i].silenceFlags = (1ULL << data.outputs[i].numChannels) - 1;

	IParameterChanges* params_in = data.inputParameterChanges;
	IParameterChanges* params_out = data.outputParameterChanges;
	IEventList* events_in = data.inputEvents;
	IEventList* events_out = data.outputEvents;

	int32 numEvents = events_in ? events_in->getEventCount() : 0;
	int32 numParamChanges[kNumParams] = {};
	IParamValueQueue* paramQueue[kNumParams] = {};
	if (params_in)
	{
		int32 numParamsChanged = params_in->getParameterCount();

		for (int32 i = 0; i < numParamsChanged; ++i)
		{
			IParamValueQueue* q = params_in->getParameterData(i);
			ParamID id = q->getParameterId();
			if (id < kNumParams)
			{
				paramQueue[id] = q;
				numParamChanges[id] = q->getPointCount();
			}
		}
	}

	IParamValueQueue* out_queue[kNumParams] = {};
	if (params_out)
	{
		int32 numOutQueues = params_out->getParameterCount();

		for (int32 i = 0; i < numOutQueues; ++i)
		{
			IParamValueQueue* q = params_out->getParameterData(i);
			ParamID id = q->getParameterId();
			if (id < kNumParams)
				out_queue[id] = q;
		}
	}

	int32 pindex[kNumParams] = {};
	int32 eindex = 0;
	for (;;)
	{
		int32 nextSampleOffset = kMaxInt32;
		int32 nextId = -1;

		Event evt;
		if (eindex < numEvents)
		{
			if (events_in->getEvent(eindex, evt) == kResultOk)
			{
				nextSampleOffset = evt.sampleOffset;
				nextId = kNumParams + 1;
			}
		}

		ParamValue value;
		for (int32 i = 0; i < kNumParams; ++i)
		{
			if (pindex[i] < numParamChanges[i])
			{
				ParamValue v;
				int32 o;
				if (paramQueue[i]->getPoint(pindex[i], o, v) == kResultOk)
				{
					if (o < nextSampleOffset)
					{
						nextSampleOffset = o;
						value = v;
						nextId = i;
					}
				}
			}
		}
		if (value < 0.) value = 0.; else if (value > 1.) value = 1.;

		if (nextId < 0)
		{
			// no more note events or parameter changes
			break;
		}
		else if (nextId < kNumParams)
		{
			switch (nextId)
			{
			case kOutChannels: // number of output channels changed
				set_outchannels(events_out, discretize(value, 16), nextSampleOffset);
				break;

			case kStrategy: // note distribution strategy changed
				strategy = discretize(value, kNumStrategies - 1);
				break;

			case kSustain: // sustain pedal changed
				if (value > 0.)
					press_sustain_pedal(events_out, nextSampleOffset);
				else
					release_sustain_pedal(events_out, nextSampleOffset);
				break;

			case kSostenuto: // sostenuto pedal changed
				if (value > 0.)
					press_sostenuto_pedal(events_out, nextSampleOffset);
				else
					release_sostenuto_pedal(events_out, nextSampleOffset);
				break;

			case kMuteAll: // release and un-sustain all notes
				if (value < 0.5)
				{
					release_all(events_out, nextSampleOffset, evt.ppqPosition, kCtrlAllSoundsOff);
					counter = 0;
					srand(0);
					for (int16 channel = 0; channel < 16; ++channel)
						cstate[channel].susload = 0;
					const int32 o = (nextSampleOffset + 1 < data.numSamples) ? (nextSampleOffset + 1) : (data.numSamples - 1);
					set_parameter(params_out, out_queue[kMuteAll], kMuteAll, o, 1.);
				}
				break;

			case kReleaseAll: // release all notes without un-sustaining
				if (value < 0.5)
				{
					release_all(events_out, nextSampleOffset, evt.ppqPosition, kCtrlAllNotesOff);
					counter = 0;
					srand(0);
					const int32 o = (nextSampleOffset + 1 < data.numSamples) ? (nextSampleOffset + 1) : (data.numSamples - 1);
					set_parameter(params_out, out_queue[kReleaseAll], kReleaseAll, o, 1.);
				}
				break;

			case kBypass: // bypass mode preserves channel without remapping
				bypass = (value >= 0.5);
				break;
			}
			++pindex[nextId];
		}
		else
		{
			// MIDI event
			switch (evt.type)
			{
			case Event::kNoteOnEvent:
				if (note_on(events_out, evt) != kResultOk)
					return kResultFalse;
				break;
			case Event::kNoteOffEvent:
				note_off(events_out, evt);
				break;
			case Event::kPolyPressureEvent:
				polypressure(events_out, evt);
				break;
			case Event::kNoteExpressionValueEvent:
			case Event::kNoteExpressionTextEvent:
				// Note expression events have no channel, so just re-broadcast them.
				if (events_out)
					events_out->addEvent(evt);
				break;
			}
			++eindex;
		}
	}

	// Send initial parameter values to help hosts sync them with the VST
	if (!initial_points_sent && params_out)
	{
		initial_points_sent = true;
		const ParamValue default_values[kNumParams] = {
			normalize(out_channels, 16),			// kOutChannels
			normalize(strategy, kNumStrategies - 1),// kStrategy
			sustain_pedal_down ? 1. : 0.,			// kSustainPedal
			sostenuto_pedal_down ? 1. : 0.,			// kSostenutoPedal
			1.,										// kMuteAll (0=on, 1=off as per MIDI standard)
			1.,										// kReleaseAll (0=on, 1=off as per MIDI standard)
			0.,										// kBypass
		};
		for (ParamID i = 0; i < kNumParams; ++i)
		{
			if (!out_queue[i] || out_queue[i]->getPointCount() <= 0)
			{
				if (set_parameter(params_out, out_queue[i], i, 0, default_values[i]) != kResultTrue)
					initial_points_sent = false;
			}
		}
	}

	return kResultOk;
}
