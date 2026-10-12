// vtihelper_editor.h - the original plug-in's editor inside the helper, mirrored into the bridge
// (findings/design-macos-bridge.md, "Editor (mirror)"). All functions run on the helper's main thread.
#pragma once
#include "pluginterfaces/vst/ivsteditcontroller.h"
#include "tirbridge.h"

struct HelperEditor;
typedef void (*editor_resize_cb)(void *ctx, const tirb_editor_reply &r);

// Opens the original's editor in an invisible, click-through window whose top-left is at (x, y) in Cocoa screen
// coordinates, renders it at `scale` into a pixel shm named after shm_prefix. Returns nullptr if there is no editor.
HelperEditor *editor_open(Steinberg::Vst::IEditController *ctl, const char *shm_prefix, double x, double y,
                          double scale, tirb_editor_reply &reply, editor_resize_cb on_resize, void *ctx);
void editor_close(HelperEditor *e);
void editor_move(HelperEditor *e, double x, double y);
void editor_input(HelperEditor *e, const tirb_input &in);
