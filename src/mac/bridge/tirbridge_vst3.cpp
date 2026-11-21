// tirbridge_vst3.cpp - "Virus TI (TI Revive)": the VST3 front end of the native (arm64) bridge for macOS
// (findings/design-macos-bridge.md). Runs in the natively started DAW; everything else (helper under Rosetta, audio,
// parameters, state, editor) is in the shared core (tirbridge_core.cpp). Single-component VST3 like the original
// (component = controller), built on the VST3 pluginterfaces only (MIT). Bundle layout: scripts/mac-build-bridge.sh.
#include <CoreFoundation/CoreFoundation.h>
#include <algorithm>
#include <atomic>
#include <cstring>
#include <string>
#include <vector>

#include "pluginterfaces/base/ibstream.h"
#include "pluginterfaces/base/ipluginbase.h"
#include "pluginterfaces/vst/ivstaudioprocessor.h"
#include "pluginterfaces/vst/ivstcomponent.h"
#include "pluginterfaces/vst/ivsteditcontroller.h"
#include "pluginterfaces/vst/ivstevents.h"
#include "pluginterfaces/vst/ivstmidicontrollers.h"
#include "pluginterfaces/vst/ivstparameterchanges.h"
#include "pluginterfaces/vst/ivstprocesscontext.h"
#include "tirbridge_core.h"

using namespace Steinberg;
using namespace Steinberg::Vst;

namespace Steinberg {
namespace Vst {
DEF_CLASS_IID(IComponent)
DEF_CLASS_IID(IAudioProcessor)
DEF_CLASS_IID(IEditController)
DEF_CLASS_IID(IMidiMapping)
DEF_CLASS_IID(IComponentHandler)
DEF_CLASS_IID(IEventList)
DEF_CLASS_IID(IParameterChanges)
DEF_CLASS_IID(IParamValueQueue)
} // namespace Vst
} // namespace Steinberg

#ifndef TIR_VERSION
#define TIR_VERSION "0.0.0" // set by scripts/mac-build-bridge.sh
#endif

// our class id (random, fixed forever: DAW projects refer to it)
static const TUID kBridgeCid = INLINE_UID(0x7A1E5C3B, 0x4D2F4E61, 0x9B0C8A17, 0x2F64D5E9);

static void from_utf8(const char *s, TChar *out, int n) // ASCII + 2/3-byte UTF-8 -> UTF-16
{
    int k = 0;
    for (const uint8_t *p = (const uint8_t *)s; *p && k < n - 1;) {
        uint32_t c = *p++;
        if (c >= 0xe0 && p[0] && p[1])
            c = (c & 0x0f) << 12 | (p[0] & 0x3f) << 6 | (p[1] & 0x3f), p += 2;
        else if (c >= 0xc0 && p[0])
            c = (c & 0x1f) << 6 | (p[0] & 0x3f), p += 1;
        out[k++] = (TChar)c;
    }
    out[k] = 0;
}

class Bridge : public IComponent, public IAudioProcessor, public IEditController, public IMidiMapping, public BridgeCore {
  public:
    std::atomic<uint32> refs{1};
    IComponentHandler *handler = nullptr;

    // FUnknown
    tresult PLUGIN_API queryInterface(const TUID iid, void **obj) override
    {
#define QI(I, P)                                                                                                       \
    if (FUnknownPrivate::iidEqual(iid, I::iid)) {                                                                      \
        *obj = static_cast<I *>(P);                                                                                    \
        addRef();                                                                                                      \
        return kResultOk;                                                                                              \
    }
        QI(FUnknown, static_cast<IComponent *>(this))
        QI(IPluginBase, static_cast<IComponent *>(this))
        QI(IComponent, this)
        QI(IAudioProcessor, this)
        QI(IEditController, this)
        QI(IMidiMapping, this)
#undef QI
        *obj = nullptr;
        return kNoInterface;
    }
    uint32 PLUGIN_API addRef() override { return ++refs; }
    uint32 PLUGIN_API release() override
    {
        uint32 r = --refs;
        if (!r)
            delete this;
        return r;
    }
    void hold() override { addRef(); }
    void unhold() override { release(); }

    // IPluginBase
    tresult PLUGIN_API initialize(FUnknown *) override { return init() ? kResultOk : kResultFalse; }
    tresult PLUGIN_API terminate() override
    {
        term();
        return kResultOk;
    }

    // IComponent
    tresult PLUGIN_API getControllerClassId(TUID cid) override
    {
        memcpy(cid, kBridgeCid, sizeof(TUID)); // single component
        return kResultOk;
    }
    tresult PLUGIN_API setIoMode(IoMode) override { return kResultOk; }
    int32 PLUGIN_API getBusCount(MediaType type, BusDirection dir) override
    {
        if (type == kAudio)
            return dir == kOutput ? (int32)layout.num_outputs : 0;
        return dir == kInput && layout.event_inputs ? 1 : 0;
    }
    tresult PLUGIN_API getBusInfo(MediaType type, BusDirection dir, int32 index, BusInfo &bus) override
    {
        if (type == kAudio && dir == kOutput && index >= 0 && index < (int32)layout.num_outputs) {
            bus.mediaType = kAudio;
            bus.direction = kOutput;
            bus.channelCount = 2;
            from_utf8(layout.out_names[index], bus.name, 128);
            bus.busType = index == 0 ? kMain : kAux;
            bus.flags = index == 0 ? BusInfo::kDefaultActive : 0;
            return kResultOk;
        }
        if (type == kEvent && dir == kInput && index == 0 && layout.event_inputs) {
            bus.mediaType = kEvent;
            bus.direction = kInput;
            bus.channelCount = (int32)layout.event_channels;
            from_utf8("Midi In", bus.name, 128);
            bus.busType = kMain;
            bus.flags = BusInfo::kDefaultActive;
            return kResultOk;
        }
        return kInvalidArgument;
    }
    tresult PLUGIN_API getRoutingInfo(RoutingInfo &, RoutingInfo &) override { return kNotImplemented; }
    tresult PLUGIN_API activateBus(MediaType, BusDirection, int32, TBool) override { return kResultOk; }
    tresult PLUGIN_API setActive(TBool state) override
    {
        set_active(state);
        return kResultOk;
    }
    tresult PLUGIN_API setState(IBStream *s) override
    {
        std::vector<uint8_t> data;
        char buf[65536];
        int32 got = 0;
        while (s && s->read(buf, sizeof buf, &got) == kResultOk && got > 0)
            data.insert(data.end(), buf, buf + got);
        return set_state(data.data(), data.size()) ? kResultOk : kResultFalse;
    }
    tresult PLUGIN_API getState(IBStream *s) override
    {
        std::vector<uint8_t> r;
        if (!get_state(r))
            return kResultFalse;
        int32 wrote = 0;
        return s->write(r.data(), (int32)r.size(), &wrote);
    }

    // IAudioProcessor
    tresult PLUGIN_API setBusArrangements(SpeakerArrangement *, int32 nin, SpeakerArrangement *out, int32 nout) override
    {
        if (nin != 0 || nout != (int32)layout.num_outputs)
            return kResultFalse;
        for (int32 i = 0; i < nout; i++)
            if (out[i] != SpeakerArr::kStereo)
                return kResultFalse;
        return kResultOk;
    }
    tresult PLUGIN_API getBusArrangement(BusDirection dir, int32 index, SpeakerArrangement &arr) override
    {
        if (dir != kOutput || index < 0 || index >= (int32)layout.num_outputs)
            return kInvalidArgument;
        arr = SpeakerArr::kStereo;
        return kResultOk;
    }
    tresult PLUGIN_API canProcessSampleSize(int32 s) override { return s == kSample32 ? kResultOk : kResultFalse; }
    uint32 PLUGIN_API getLatencySamples() override { return layout.latency; }
    tresult PLUGIN_API setupProcessing(ProcessSetup &su) override
    {
        setup(su.sampleRate, su.maxSamplesPerBlock, su.processMode);
        return kResultOk;
    }
    tresult PLUGIN_API setProcessing(TBool) override { return kResultOk; }
    uint32 PLUGIN_API getTailSamples() override { return kInfiniteTail; }

    tresult PLUGIN_API process(ProcessData &d) override
    {
        // parameter-only calls (numSamples 0) still reach the helper so automation is applied
        tirb_shm *s = shm;
        uint32_t frames = (uint32_t)std::min<int32>(d.numSamples, TIRB_MAX_BLOCK);
        if (!s || !ok) {
            silence(d);
            return kResultOk;
        }
        uint32_t ne = 0;
        if (d.inputEvents)
            for (int32 i = 0, n = d.inputEvents->getEventCount(); i < n && ne < TIRB_MAX_EVENTS; i++) {
                Event e;
                if (d.inputEvents->getEvent(i, e) != kResultOk)
                    continue;
                if (e.type == Event::kDataEvent || e.type == Event::kNoteExpressionTextEvent ||
                    e.type == Event::kChordEvent || e.type == Event::kScaleEvent)
                    continue; // events with pointers: not bridged (SysEx from the DAW is not used by the Virus TI)
                tirb_event &te = s->events[ne++];
                te.bus = e.busIndex;
                te.sample_offset = e.sampleOffset;
                te.type = e.type;
                te.flags = e.flags;
                memcpy(te.data, &e.noteOn, sizeof te.data);
            }
        s->num_events = ne;
        uint32_t np = 0;
        if (d.inputParameterChanges)
            for (int32 i = 0, n = d.inputParameterChanges->getParameterCount(); i < n; i++) {
                IParamValueQueue *q = d.inputParameterChanges->getParameterData(i);
                if (!q)
                    continue;
                ParamID id = q->getParameterId();
                for (int32 k = 0, m = q->getPointCount(); k < m && np < TIRB_MAX_PCHANGES; k++) {
                    int32 off;
                    ParamValue v;
                    if (q->getPoint(k, off, v) == kResultOk) {
                        s->pchanges[np++] = {id, off, v};
                        set_value(id, v);
                    }
                }
            }
        s->num_pchanges = np;
        tirb_transport &t = s->transport;
        memset(&t, 0, sizeof t);
        if (const ProcessContext *c = d.processContext) {
            t.state = c->state;
            t.sample_rate = c->sampleRate;
            t.project_time_samples = c->projectTimeSamples;
            t.system_time = c->systemTime;
            t.continous_time_samples = c->continousTimeSamples;
            t.project_time_music = c->projectTimeMusic;
            t.bar_position_music = c->barPositionMusic;
            t.cycle_start_music = c->cycleStartMusic;
            t.cycle_end_music = c->cycleEndMusic;
            t.tempo = c->tempo;
            t.time_sig_num = c->timeSigNumerator;
            t.time_sig_den = c->timeSigDenominator;
        }
        if (!run(frames)) {
            silence(d);
            return kResultOk;
        }
        for (int32 b = 0; b < d.numOutputs && b < (int32)layout.num_outputs; b++) {
            AudioBusBuffers &o = d.outputs[b];
            for (int32 c = 0; c < o.numChannels && c < 2; c++)
                if (o.channelBuffers32 && o.channelBuffers32[c])
                    memcpy(o.channelBuffers32[c], s->out[b * 2 + c], frames * sizeof(float));
            o.silenceFlags = 0;
        }
        const float *main_out[2] = {s->out[0], s->out[1]};
        note_output_level(main_out, 2, frames);
        if (d.outputParameterChanges)
            for (uint32_t i = 0; i < s->num_out_pchanges && i < 64; i++) {
                int32 idx;
                if (IParamValueQueue *q = d.outputParameterChanges->addParameterData(s->out_pchanges[i].id, idx))
                    q->addPoint(s->out_pchanges[i].sample_offset, s->out_pchanges[i].value, idx);
            }
        return kResultOk;
    }
    static void silence(ProcessData &d)
    {
        for (int32 b = 0; b < d.numOutputs; b++) {
            AudioBusBuffers &o = d.outputs[b];
            for (int32 c = 0; c < o.numChannels; c++)
                if (o.channelBuffers32 && o.channelBuffers32[c])
                    memset(o.channelBuffers32[c], 0, d.numSamples * sizeof(float));
            o.silenceFlags = ~0ull;
        }
    }

    // IEditController
    tresult PLUGIN_API setComponentState(IBStream *) override { return kResultOk; } // single component
    int32 PLUGIN_API getParameterCount() override { return (int32)params.size(); }
    tresult PLUGIN_API getParameterInfo(int32 index, ParameterInfo &info) override
    {
        if (index < 0 || index >= (int32)params.size())
            return kInvalidArgument;
        const tirb_param &p = params[index];
        info.id = p.id;
        from_utf8(p.title, info.title, 128);
        from_utf8(p.short_title, info.shortTitle, 128);
        from_utf8(p.units, info.units, 128);
        info.stepCount = p.step_count;
        info.defaultNormalizedValue = p.default_value;
        info.unitId = p.unit_id;
        info.flags = p.flags;
        return kResultOk;
    }
    tresult PLUGIN_API getParamStringByValue(ParamID id, ParamValue v, String128 out) override
    {
        std::string s;
        if (!param_string(id, v, s))
            return kResultFalse;
        from_utf8(s.c_str(), out, 128);
        return kResultOk;
    }
    tresult PLUGIN_API getParamValueByString(ParamID, TChar *, ParamValue &) override { return kNotImplemented; }
    ParamValue PLUGIN_API normalizedParamToPlain(ParamID, ParamValue v) override { return v; }
    ParamValue PLUGIN_API plainParamToNormalized(ParamID, ParamValue v) override { return v; }
    ParamValue PLUGIN_API getParamNormalized(ParamID id) override { return value(id); }
    tresult PLUGIN_API setParamNormalized(ParamID id, ParamValue v) override
    {
        set_param_from_host(id, v);
        return kResultOk;
    }
    tresult PLUGIN_API setComponentHandler(IComponentHandler *h) override
    {
        if (h)
            h->addRef();
        if (handler)
            handler->release();
        handler = h;
        return kResultOk;
    }
    IPlugView *PLUGIN_API createView(FIDString name) override
    {
        if (!ok || (name && strcmp(name, ViewType::kEditor)) || view)
            return nullptr;
        view = bridge_create_view(this);
        return view;
    }

    // IMidiMapping
    tresult PLUGIN_API getMidiControllerAssignment(int32 bus, int16 channel, CtrlNumber cc, ParamID &id) override
    {
        if (bus != 0)
            return kResultFalse;
        auto it = midimap.find((uint32_t)channel << 16 | (uint16_t)cc);
        if (it == midimap.end())
            return kResultFalse;
        id = it->second;
        return kResultOk;
    }

    // core notifications (main thread)
    void on_param_edit(const tirb_param_edit &e) override
    {
        if (!handler)
            return;
        if (e.what == 0)
            handler->beginEdit(e.id);
        else if (e.what == 1)
            handler->performEdit(e.id, e.value);
        else
            handler->endEdit(e.id);
    }
    void on_restart(int32_t flags) override
    {
        if (handler)
            handler->restartComponent(flags);
    }
};

// ------------------------------------------------------------------------------- factory and module entry
class Factory : public IPluginFactory2 {
  public:
    tresult PLUGIN_API queryInterface(const TUID iid, void **obj) override
    {
        if (FUnknownPrivate::iidEqual(iid, IPluginFactory2::iid) || FUnknownPrivate::iidEqual(iid, IPluginFactory::iid) ||
            FUnknownPrivate::iidEqual(iid, FUnknown::iid)) {
            *obj = this;
            return kResultOk;
        }
        *obj = nullptr;
        return kNoInterface;
    }
    uint32 PLUGIN_API addRef() override { return 1; }
    uint32 PLUGIN_API release() override { return 1; }
    tresult PLUGIN_API getFactoryInfo(PFactoryInfo *info) override
    {
        *info = PFactoryInfo();
        strcpy(info->vendor, "TI Revive");
        strcpy(info->url, "https://github.com/derpsn/ti-revive");
        info->flags = PFactoryInfo::kUnicode;
        return kResultOk;
    }
    int32 PLUGIN_API countClasses() override { return 1; }
    tresult PLUGIN_API getClassInfo(int32 i, PClassInfo *info) override
    {
        if (i != 0)
            return kInvalidArgument;
        *info = PClassInfo();
        memcpy(info->cid, kBridgeCid, sizeof(TUID));
        info->cardinality = PClassInfo::kManyInstances;
        strcpy(info->category, kVstAudioEffectClass);
        strcpy(info->name, "Virus TI (TI Revive)");
        return kResultOk;
    }
    tresult PLUGIN_API getClassInfo2(int32 i, PClassInfo2 *info) override
    {
        if (i != 0)
            return kInvalidArgument;
        *info = PClassInfo2();
        memcpy(info->cid, kBridgeCid, sizeof(TUID));
        info->cardinality = PClassInfo::kManyInstances;
        strcpy(info->category, kVstAudioEffectClass);
        strcpy(info->name, "Virus TI (TI Revive)");
        strcpy(info->subCategories, "Instrument|Synth");
        strcpy(info->vendor, "TI Revive");
        strcpy(info->version, TIR_VERSION);
        strcpy(info->sdkVersion, kVstVersionString);
        return kResultOk;
    }
    tresult PLUGIN_API createInstance(FIDString cid, FIDString iid, void **obj) override
    {
        if (!FUnknownPrivate::iidEqual(cid, kBridgeCid)) {
            *obj = nullptr;
            return kInvalidArgument;
        }
        Bridge *b = new Bridge;
        tresult r = b->queryInterface(iid, obj);
        b->release();
        return r;
    }
};

extern "C" {
__attribute__((visibility("default"))) bool bundleEntry(CFBundleRef) { return true; }
__attribute__((visibility("default"))) bool bundleExit() { return true; }
__attribute__((visibility("default"))) IPluginFactory *GetPluginFactory()
{
    static Factory f;
    return &f;
}
}
