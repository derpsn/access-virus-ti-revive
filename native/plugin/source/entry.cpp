// entry.cpp - VST3 factory of the native Virus TI plug-in.
#include "public.sdk/source/main/pluginfactory.h"
#include "virusplugin.h"
#include "device/factoryorder.h"
#include <string>

#ifdef __APPLE__
#include <dlfcn.h>
#include <sys/stat.h>

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

// The factory written out (what BEGIN_FACTORY_DEF / DEF_CLASS2 / END_FACTORY expand to), so the class order can
// depend on the bundle: the Snow Audio Unit's copy of this module lists the Snow class first (factoryorder.h, E374)
static bool snow_first()
{
#ifdef __APPLE__
    Dl_info info;
    struct stat st;
    if (dladdr((const void *)&snow_first, &info) && info.dli_fname) {
        const std::string marker = vti::snow_first_marker(info.dli_fname);
        return !marker.empty() && stat(marker.c_str(), &st) == 0;
    }
#endif
    return false;
}

using namespace Steinberg;

SMTG_EXPORT_SYMBOL IPluginFactory *PLUGIN_API GetPluginFactory()
{
    if (gPluginFactory) {
        gPluginFactory->addRef();
        return gPluginFactory;
    }
    static PFactoryInfo factoryInfo("TI Revive", "https://github.com/derpsn/access-virus-ti-revive", "",
                                    Vst::kDefaultFactoryFlags);
    gPluginFactory = new CPluginFactory(factoryInfo);
    TUID ti = INLINE_UID_FROM_FUID(vti::kVirusTIClassId);
    static PClassInfo2 tiClass(ti, PClassInfo::kManyInstances, kVstAudioEffectClass, VTI_NATIVE_NAME, 0,
                               Vst::PlugType::kInstrumentSynth, nullptr, VTI_NATIVE_VERSION, kVstVersionString);
#ifdef VTI_NATIVE_DROPIN // (the drop-in build has the original's id already, and no Snow class)
    gPluginFactory->registerClass(&tiClass, vti::VirusPlugin::create);
#else
    // the same plug-in under the original Access plug-in's id: projects made with the original open with it in hosts
    // that ignore moduleinfo compatibility (Ableton Live 11, E221)
    TUID orig = INLINE_UID_FROM_FUID(vti::kOriginalClassId);
    static PClassInfo2 origClass(orig, PClassInfo::kManyInstances, kVstAudioEffectClass,
                                 VTI_NATIVE_NAME " (original projects)", 0, Vst::PlugType::kInstrumentSynth, nullptr,
                                 VTI_NATIVE_VERSION, kVstVersionString);
    // the Snow plug-in, like the original's own Snow plug-in: the Snow's 4-part editor and files (E361)
    TUID snow = INLINE_UID_FROM_FUID(vti::kSnowClassId);
    static PClassInfo2 snowClass(snow, PClassInfo::kManyInstances, kVstAudioEffectClass, VTI_NATIVE_NAME " (Snow)", 0,
                                 Vst::PlugType::kInstrumentSynth, nullptr, VTI_NATIVE_VERSION, kVstVersionString);
    const struct {
        PClassInfo2 *info;
        FUnknown *(*create)(void *);
    } classes[3] = {{&tiClass, vti::VirusPlugin::create},
                    {&origClass, vti::VirusPlugin::create},
                    {&snowClass, vti::VirusPlugin::create_snow}};
    for (int i : vti::class_order(snow_first()))
        gPluginFactory->registerClass(classes[i].info, classes[i].create);
#endif
    return gPluginFactory;
}
