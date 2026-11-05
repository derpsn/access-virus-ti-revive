// tirevive_au.mm - the Audio Unit (AUv2) of "TI Revive Virus Control" for Logic Pro / GarageBand: Steinberg's AU
// wrapper (VST3 SDK, MIT; compiled as part of this file) around the VST3 plug-in, which the component carries as
// Contents/Resources/plugin.vst3.
//
// It replaces the classic TI Revive Audio Unit (the bridge that ran the original Access plug-in under Rosetta) and
// has its codes (aumu / VTIr / TIRv), so Logic and GarageBand projects made with "Virus TI (TI Revive)" open with it.
// Those projects stored the plug-in state under the AU preset key "data" (tirbridge_au.cpp); the wrapper expects
// "Processor State". RestoreState translates the old form; the content is the original's state format, which the
// plug-in reads unchanged.
#include "public.sdk/source/vst/auwrapper/auwrapper.mm"

#include "../src/device/dualsched.h"

#include <atomic>
#include <cstdio>
#include <string>
#include <sys/stat.h>

namespace Steinberg {
namespace Vst {

class TIReviveAU : public AUWrapper
{
public:
	TIReviveAU (ComponentInstanceRecord* ci) : AUWrapper (ci), offerDual (probeFile ("dualsched.on"))
	{
		probeLine (offerDual ? "audio unit opened, dual scheduling offered" : "audio unit opened, dual scheduling not offered");
	}
	~TIReviveAU () override { probeCounts ("audio unit closed"); }

	// Apple's dual scheduling mode (src/device/dualsched.h, E401): offering it lets the host tell us which notes are
	// played right now. The wrapper knows nothing of it, so the frame value is taken apart here, before it gets there.
	// Offered only with dualsched.on next to diag.on, for tests: Logic sets it and then its regions do not play (E405).
	OSStatus GetPropertyInfo (AudioUnitPropertyID inID, AudioUnitScope inScope, AudioUnitElement inElement,
	                          UInt32& outDataSize, bool& outWritable) override
	{
		if (inID == vti::DUAL_SCHEDULING_PROPERTY && offerDual)
		{
			if (inScope != kAudioUnitScope_Global)
				return kAudioUnitErr_InvalidScope;
			outDataSize = sizeof (UInt32);
			outWritable = true;
			return noErr;
		}
		return AUWrapper::GetPropertyInfo (inID, inScope, inElement, outDataSize, outWritable);
	}

	ComponentResult GetProperty (AudioUnitPropertyID inID, AudioUnitScope inScope, AudioUnitElement inElement,
	                             void* outData) override
	{
		if (inID == vti::DUAL_SCHEDULING_PROPERTY && offerDual)
		{
			if (inScope != kAudioUnitScope_Global)
				return kAudioUnitErr_InvalidScope;
			*static_cast<UInt32*> (outData) = dualMode.load (std::memory_order_relaxed) ? 1 : 0;
			return noErr;
		}
		return AUWrapper::GetProperty (inID, inScope, inElement, outData);
	}

	ComponentResult SetProperty (AudioUnitPropertyID inID, AudioUnitScope inScope, AudioUnitElement inElement,
	                             const void* inData, UInt32 inDataSize) override
	{
		probeSet (inID, inScope, inElement, inData, inDataSize);
		if (inID == vti::DUAL_SCHEDULING_PROPERTY && offerDual)
		{
			if (inScope != kAudioUnitScope_Global)
				return kAudioUnitErr_InvalidScope;
			if (inDataSize != sizeof (UInt32))
				return kAudioUnitErr_InvalidPropertyValue;
			const bool on = *static_cast<const UInt32*> (inData) != 0;
			dualMode.store (on, std::memory_order_relaxed);
			probeLine (on ? "the host set dual scheduling to 1" : "the host set dual scheduling to 0");
			return noErr;
		}
		return AUWrapper::SetProperty (inID, inScope, inElement, inData, inDataSize);
	}

	OSStatus MIDIEvent (UInt32 inStatus, UInt32 inData1, UInt32 inData2, UInt32 inOffsetSampleFrame) override
	{
		const auto d = take (inOffsetSampleFrame, midiEvents);
		const int32 before = eventList.getEventCount ();
		const OSStatus r = AUWrapper::MIDIEvent (inStatus, inData1, inData2, d.offset);
		markLive (before, d.live);
		return r;
	}

#if AUSDK_MIDI2_AVAILABLE
	OSStatus MIDIEventList (UInt32 inOffsetSampleFrame, const struct MIDIEventList* inEventList) override
	{
		const auto d = take (inOffsetSampleFrame, eventListCalls);
		const int32 before = eventList.getEventCount ();
		const OSStatus r = AUWrapper::MIDIEventList (d.offset, inEventList);
		markLive (before, d.live);
		return r;
	}
#endif

	ComponentResult StartNote (MusicDeviceInstrumentID inInstrument, MusicDeviceGroupID inGroupID,
	                           NoteInstanceID* outNoteInstanceID, UInt32 inOffsetSampleFrame,
	                           const MusicDeviceNoteParams& inParams) override
	{
		const auto d = take (inOffsetSampleFrame, startStopCalls);
		const int32 before = eventList.getEventCount ();
		const ComponentResult r =
		    AUWrapper::StartNote (inInstrument, inGroupID, outNoteInstanceID, d.offset, inParams);
		markLive (before, d.live);
		return r;
	}

	ComponentResult StopNote (MusicDeviceGroupID inGroupID, NoteInstanceID inNoteInstanceID,
	                          UInt32 inOffsetSampleFrame) override
	{
		const auto d = take (inOffsetSampleFrame, startStopCalls);
		const int32 before = eventList.getEventCount ();
		const ComponentResult r = AUWrapper::StopNote (inGroupID, inNoteInstanceID, d.offset);
		markLive (before, d.live);
		return r;
	}

	ComponentResult SetParameter (AudioUnitParameterID inID, AudioUnitScope inScope, AudioUnitElement inElement,
	                              AudioUnitParameterValue inValue, UInt32 inBufferOffsetInFrames) override
	{
		// Apple's list has this one too; without the mask a scheduled change would get an offset of 16 million frames
		const auto d = vti::dual_schedule (dualMode.load (std::memory_order_relaxed), inBufferOffsetInFrames);
		return AUWrapper::SetParameter (inID, inScope, inElement, inValue, d.offset);
	}

	ComponentResult RestoreState (CFPropertyListRef inData) override
	{
		if (inData && CFGetTypeID (inData) == CFDictionaryGetTypeID ())
		{
			CFDictionaryRef dict = static_cast<CFDictionaryRef> (inData);
			CFStringRef processor = CFSTR ("Processor State");
			CFTypeRef old = CFDictionaryGetValue (dict, CFSTR (kAUPresetDataKey));
			if (!CFDictionaryContainsKey (dict, processor) && old && CFGetTypeID (old) == CFDataGetTypeID ())
			{
				CFMutableDictionaryRef m = CFDictionaryCreateMutableCopy (nullptr, 0, dict);
				CFDictionarySetValue (m, processor, old); // no "Controller State": single component plug-in
				ComponentResult r = AUWrapper::RestoreState (m);
				CFRelease (m);
				return r;
			}
		}
		return AUWrapper::RestoreState (inData);
	}

private:
	const bool offerDual;
	int probeSets = 0;
	std::atomic<bool> dualMode {false};
	std::atomic<uint32> midiEvents {0}, eventListCalls {0}, startStopCalls {0}, liveEvents {0}, scheduledEvents {0};

	// on the render thread: atomics only
	vti::DualSchedule take (UInt32 frame, std::atomic<uint32>& entry)
	{
		const bool on = dualMode.load (std::memory_order_relaxed);
		const vti::DualSchedule d = vti::dual_schedule (on, frame);
		entry.fetch_add (1, std::memory_order_relaxed);
		if (on)
			(d.live ? liveEvents : scheduledEvents).fetch_add (1, std::memory_order_relaxed);
		return d;
	}

	// the wrapper builds the events with no flags: mark what it has just added
	void markLive (int32 from, bool live)
	{
		if (!live)
			return;
		for (int32 i = from; i < eventList.getEventCount (); i++)
			if (Event* e = eventList.getEventByIndex (i))
				e->flags |= Event::kIsLive;
	}

	// the field diagnostic of the plug-in (noteprobe.on next to diag.on, native/src/device/noteprobe.h): same log. Called
	// from the host's control calls and open/close, never from the render thread.
	static std::string probeDir ()
	{
		const char* home = getenv ("HOME");
		return home ? std::string (home) + "/Library/Application Support/virus-ti-native" : std::string ();
	}

	static bool probeFile (const char* name)
	{
		struct stat st;
		const std::string dir = probeDir ();
		return !dir.empty () && stat ((dir + "/" + name).c_str (), &st) == 0;
	}

	static bool probeOn () { return probeFile ("diag.on") && probeFile ("noteprobe.on"); }

	// what the host sets on us, to see how it treats the unit (control calls only; the first 60 of an instance)
	void probeSet (AudioUnitPropertyID id, AudioUnitScope scope, AudioUnitElement element, const void* data, UInt32 size)
	{
		if (probeSets >= 60 || !probeOn ())
			return;
		probeSets++;
		char b[120];
		if (data && size == sizeof (UInt32))
			snprintf (b, sizeof b, "set property %u scope %u element %u = %u", (unsigned)id, (unsigned)scope, (unsigned)element,
			          (unsigned)*static_cast<const UInt32*> (data));
		else if (data && size == sizeof (Float64))
			snprintf (b, sizeof b, "set property %u scope %u element %u = %.3f", (unsigned)id, (unsigned)scope, (unsigned)element,
			          *static_cast<const Float64*> (data));
		else
			snprintf (b, sizeof b, "set property %u scope %u element %u, %u bytes", (unsigned)id, (unsigned)scope, (unsigned)element,
			          (unsigned)size);
		probeLine (b);
	}

	static void probeLine (const char* text)
	{
		if (!probeOn ())
			return;
		if (FILE* f = fopen ((probeDir () + "/diag.log").c_str (), "a"))
		{
			fprintf (f, "noteprobe: AU: %s\n", text);
			fclose (f);
		}
	}

	void probeCounts (const char* text)
	{
		char b[200];
		snprintf (b, sizeof b, "%s, dual scheduling %d, MIDI events %u, event lists %u, start/stop %u, live %u, scheduled %u",
		          text, dualMode.load () ? 1 : 0, (unsigned)midiEvents.load (), (unsigned)eventListCalls.load (),
		          (unsigned)startStopCalls.load (), (unsigned)liveEvents.load (), (unsigned)scheduledEvents.load ());
		probeLine (b);
	}
};

} // namespace Vst
} // namespace Steinberg

extern "C" {
__attribute__ ((visibility ("default"))) void* TIReviveVirusControlFactory (const AudioComponentDescription* inDesc)
{
	return Steinberg::Vst::AUFactory<Steinberg::Vst::TIReviveAU>::Factory (inDesc);
}
}
