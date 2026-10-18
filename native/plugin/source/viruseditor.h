// viruseditor.h - the plug-in editor: the original layout (editor.xml + PNGs from the user's Access installation)
// drawn by the shared EditorRenderer into a VSTGUI view; knobs/sliders by vertical drag, radio buttons and buttons by
// click. Menus, text edits and the custom displays follow (R4).
#pragma once
#include "editor/editorrender.h"
#include "pluginterfaces/gui/iplugviewcontentscalesupport.h"
#include "public.sdk/source/vst/vstguieditor.h"
#include "vstgui/lib/cview.h"
#include <map>
#include <memory>

namespace vti {

class VirusPlugin;

// Size: the original layout (907 x 513) times the display scale the host reports (Windows/Linux: e.g. 1.5 at
// 150 %; macOS: 1, it scales itself) times the user's choice: the right-click menu's steps (75..200 %) or any size
// the window is dragged to (50..300 %, in the layout's proportions; editor/editorsize.h), kept per user.
class VirusEditor : public Steinberg::Vst::VSTGUIEditor, public Steinberg::IPlugViewContentScaleSupport {
public:
    explicit VirusEditor(VirusPlugin *plugin);
    bool PLUGIN_API open(void *parent, const VSTGUI::PlatformType &platformType) override;
    void PLUGIN_API close() override;

    // IPlugViewContentScaleSupport: the host's display scale
    Steinberg::tresult PLUGIN_API setContentScaleFactor(ScaleFactor factor) override;
    // the host may let the user drag the window's edges; checkSizeConstraint keeps the proportions and the limits
    Steinberg::tresult PLUGIN_API canResize() override { return Steinberg::kResultTrue; }
    Steinberg::tresult PLUGIN_API checkSizeConstraint(Steinberg::ViewRect *rect) override;
    Steinberg::tresult PLUGIN_API onSize(Steinberg::ViewRect *newSize) override;
    bool beforeSizeChange(const VSTGUI::CRect &newSize, const VSTGUI::CRect &oldSize) override;

    double user_zoom() const { return user_zoom_; }
    void set_user_zoom(double z); // from the editor's menu; saved for the next editor
    double content_scale() const { return content_scale_; } // the host's display scale

    DELEGATE_REFCOUNT(Steinberg::Vst::VSTGUIEditor)
    Steinberg::tresult PLUGIN_API queryInterface(const Steinberg::TUID iid, void **obj) override;

private:
    VirusPlugin *plugin_;
    double user_zoom_ = 1.0, content_scale_ = 1.0;
    bool resizing_ = false;   // inside our own resize request
    bool zoom_dirty_ = false; // dragged to a new size: saved when the editor closes, not at every step of the drag
    double zoom() const { return user_zoom_ * content_scale_; }
    void apply_zoom();
    void save_zoom();
};

} // namespace vti
