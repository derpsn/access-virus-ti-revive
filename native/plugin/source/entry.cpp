// entry.cpp - VST3 factory of the native Virus TI plug-in.
#include "public.sdk/source/main/pluginfactory.h"
#include "virusplugin.h"

#ifdef __APPLE__
#include <dlfcn.h>

// The binary stays loaded until the host quits (issue #16, E336). The Audio Unit wrapper unloads the inner plug-in
// when its last instance goes, while things the plug-in started may still use its code: the thread that waits for
// the broker it started (waitpid returns into this binary when the broker exits, 30 s later), a view the host
// releases after deleting the unit. Hosts that keep plug-ins loaded (Ableton Live, GarageBand) behave as before.
namespace {
struct KeepLoaded {
    KeepLoaded()
    {
        Dl_info info;
        if (dladdr((const void *)&anchor, &info) && info.dli_fname) // this binary's own path
            dlopen(info.dli_fname, RTLD_LAZY | RTLD_NOLOAD | RTLD_NODELETE); // one more reference, never closed
    }
    static void anchor() {}
} keep_loaded;
} // namespace
#endif

#ifndef VTI_NATIVE_VERSION
#define VTI_NATIVE_VERSION "0.0.0" // set by CMake (project version)
#endif
#ifdef VTI_NATIVE_DROPIN
#define VTI_NATIVE_NAME "Virus TI"
#else
#define VTI_NATIVE_NAME "TI Revive Virus Control"
#endif

BEGIN_FACTORY_DEF("TI Revive", "https://github.com/derpsn/access-virus-ti-revive", "")

DEF_CLASS2(INLINE_UID_FROM_FUID(vti::kVirusTIClassId), PClassInfo::kManyInstances, kVstAudioEffectClass, VTI_NATIVE_NAME,
           0, Steinberg::Vst::PlugType::kInstrumentSynth, VTI_NATIVE_VERSION, kVstVersionString, vti::VirusPlugin::create)
#ifndef VTI_NATIVE_DROPIN // (the drop-in build has the original's id already)
// the same plug-in under the original Access plug-in's id: projects made with the original open with it in hosts that
// ignore moduleinfo compatibility (Ableton Live 11, E221)
DEF_CLASS2(INLINE_UID_FROM_FUID(vti::kOriginalClassId), PClassInfo::kManyInstances, kVstAudioEffectClass,
           VTI_NATIVE_NAME " (original projects)", 0, Steinberg::Vst::PlugType::kInstrumentSynth, VTI_NATIVE_VERSION,
           kVstVersionString, vti::VirusPlugin::create)
// the Snow plug-in, like the original's own Snow plug-in: the Snow's 4-part editor and files (E361)
DEF_CLASS2(INLINE_UID_FROM_FUID(vti::kSnowClassId), PClassInfo::kManyInstances, kVstAudioEffectClass,
           VTI_NATIVE_NAME " (Snow)", 0, Steinberg::Vst::PlugType::kInstrumentSynth, VTI_NATIVE_VERSION,
           kVstVersionString, vti::VirusPlugin::create_snow)
#endif

END_FACTORY
