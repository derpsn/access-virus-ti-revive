// helptext.cpp - see helptext.h. Keys are parameter paths; a key without digits ("Assign Slot/Amount") serves all
// numbered copies ("Assign Slot 3/Amount 2") unless a key of its own exists. Written for TI Revive from the documented
// behaviour of the Virus TI (community notes, may simplify); keep it short and practical.
#include "editor/helptext.h"
#include <algorithm>
#include <cctype>
#include <cstring>
#include <unordered_map>

namespace vti {

namespace {

struct Entry {
    const char *key, *title, *text; // title nullptr: made from the path
};

const Entry ENTRIES[] = {
    // --- editor, pages, browser --------------------------------------------------------------------------------
    {"VirusControl/Help Menu", "Help menu",
     "Opens TI Revive's menu: Explain the Controls (this help mode: point at a control to read about it, click to "
     "keep the note on screen, click ? again or press Esc to leave; clicks change no value meanwhile), the website, "
     "Report a Problem, Save Logs for a Bug Report, the detailed log and Panic (all notes off)."},
    // the displays share their parameter with the knob around them: their own notes (help mode looks them up by type)
    {"VirusControl/Filter Display", "Filter display",
     "Shows how the filter shapes the sound: the point is the cutoff (left / right) and the resonance (up / down). "
     "Drag the point or the line to change them; in band stop mode the point moves only the cutoff."},
    {"VirusControl/Envelope Display", "Envelope display",
     "Shows the envelope's shape over time. Drag a point or a line to change its times and levels."},
    {"VirusControl/SelectedPage", "Pages",
     "Switches between the editor's pages (Easy, Oscillators, Filters, LFOs, Matrix, Arpeggiator, Effects ...). "
     "Page tabs keep working in help mode, so you can read every page."},
    {"VirusControl/SelectedUtilityPage", "Utility pages",
     "Switches between the pages of the Patch Utility section (browser, preferences, remote templates)."},
    {"VirusControl/Selected Section", "Patch / utility section",
     "Switches the main area between editing the patch and the Patch Utility section (browser, preferences)."},
    {"Multi Parameters/Panel Part", "Part strip",
     "Selects the part (1 to 16) that the editor shows and edits. Each part plays on its own MIDI channel; "
     "drag a patch name onto another strip to copy it there."},
    {"Multi Part Bits/Part Enable", "Part on/off",
     "Switches this part on or off. A part that is off does not play, even if notes arrive on its channel."},
    {"Internal MIDI Standard Parameters/Volume", "Part volume and activity",
     "The part's channel volume (MIDI CC 7). The bar behind the handle lights up while the part receives notes."},
    {"Single Patch Name/Name", "Patch name",
     "The name of the part's current patch. Double-click to rename it; drag it onto another part strip to copy "
     "the whole patch there."},
    {"Browser/Selected Bank", "Patch browser",
     "The banks of the Virus (RAM and ROM) and your patch libraries. Click a patch to select it, double-click to "
     "load it into the current part, or drag it onto a part strip."},
    {"Repository Actions/Search", "Search",
     "Type to filter the browser by patch name; only matching patches stay bright."},
    {"Repository Actions/Category Filter", "Category filter",
     "Shows only patches of one category (Lead, Pad, Bass ...) in the browser."},
    {"Repository Actions/Current Patch", "Patch menu",
     "Actions for the current patch: load the next or previous patch, initialise, and similar."},
    {"Repository Actions/Current Patch Save Action", "Save patch",
     "Saves the current patch into one of your patch libraries on the computer (it does not write to the Virus)."},
    {"Repository Actions/Load Template File", nullptr, "Loads a remote template file (knob assignments for MIDI "
                                                      "controllers)."},
    {"Repository Actions/Save Template File", nullptr, "Saves the remote templates into a file."},
    {"Categories/Category", "Category",
     "One of two categories of this patch (Lead, Pad, Bass, Arp ...). Used by the browser's category filter."},
    {"Global USB/Virus Control USB IO Configuration", "Input and output configuration",
     "How the Virus' USB audio is used, kept per computer. 2 Outs: the plug-in's Out 1 and 2. 3 Outs: a part "
     "routed to USB 3 comes out of Out 3. 3 Outs / 1 Input: also sends the plug-in's Sidechain input to the "
     "Virus, where a part's Input settings (FX page) can process it."},
    {"VirusControl Links/Filter Link", "Filter link",
     "Links the two filters' cutoff knobs, so one knob moves both."},
    {"VirusControl Links/LFO1 Mod Link", "LFO 1 link",
     "Links LFO 1's oscillator amounts so the Osc 1 and Osc 2 knobs move together."},
    {"VirusControl Links/LFO2 Mod Link", "LFO 2 link",
     "Links LFO 2's cutoff amounts so the Cutoff 1 and Cutoff 2 knobs move together."},
    {"VirusControl Links/Vocoder Link", "Vocoder link",
     "Links the vocoder's carrier and modulator spread and Q, so one knob moves both."},
    {"Locks/*", "Section lock",
     "Double-click the header to lock this section of the part (red), again to unlock it. A locked section keeps its "
     "settings when you load another patch, in the browser or on the Virus. Right-click for more: all sections, all "
     "parts and presets per page. Locks are not saved with the project, like in the original."},
    {"LockView/Part Lock Indicator", "Lock indicator", "This part has locked sections."},
    {"VirusControl/Live Mode", nullptr, "A switch of the original Virus Control. Not used by TI Revive yet."},
    {"VirusControl/Direct Monitoring", nullptr, "A switch of the original Virus Control. Not used by TI Revive yet."},
    {"VirusControl/Access Website", nullptr, "A link to the manufacturer's website (not used by TI Revive)."},
    {"VirusControl/Display Parameter", "Display", "The display shows the parameter you touched last, its value and the part. It changes when you move a knob here or on the Virus."},
    {"VirusControl/Display Value", "Display", "The display shows the parameter you touched last, its value and the part. It changes when you move a knob here or on the Virus."},
    {"VirusControl/Display Part", "Display", "The display shows the parameter you touched last, its value and the part. It changes when you move a knob here or on the Virus."},
    {"VirusControl/Display Original Value", "Display", "The display shows the parameter you touched last, its value and the part. It changes when you move a knob here or on the Virus."},
    {"Envelope/Select", "Envelope select",
     "Chooses which envelope the panel shows and edits: the filter envelope, envelope 3 or envelope 4."},

    // --- oscillators -------------------------------------------------------------------------------------------
    {"Oscillator/Mode", "Oscillator type",
     "Classic (analog-style waves), HyperSaw (up to 9 stacked saws), Wavetable, Wave PWM, and the Grain and "
     "Formant table types, which reshape wavetables. Simple versions use less DSP than Complex ones."},
    {"Oscillator/Shape", "Shape",
     "Blends the waveform: left of centre a spectral wave (chosen with Wave), centre a pure sawtooth, right a "
     "pulse wave. Sweeping it, or modulating it, changes the tone a lot."},
    {"Oscillator/WaveSelect", "Wave",
     "Chooses one of 64 spectral waves (sine, triangle, waves 3 to 64) used while Shape is left of centre."},
    {"Oscillator/Pulsewidth", "Pulse width",
     "Width of the pulse wave while Shape is right of centre: 50 % is a square, higher values sound thinner."},
    {"Oscillator/Semitone", "Semitone",
     "Pitch of the oscillator in semitones (four octaves up or down). Try -24 to hear wavetables in detail."},
    {"Oscillator/Keyfollow", "Key follow",
     "How much the pitch follows the keys. Normal is one semitone per key; other values stretch or fix the pitch, "
     "useful for drones and effects."},
    {"Oscillator/Balance", "Osc balance",
     "Mix between oscillator 1 (left) and oscillator 2 (right, including its FM)."},
    {"Oscillator/Sync", "Sync",
     "Classic: restarts oscillator 2 with every cycle of oscillator 1 - sweep oscillator 2's pitch for the typical "
     "sync lead. HyperSaw: switches its own sync on, tuned with the sync frequency."},
    {"Oscillator/Detune", "Detune",
     "Detunes the oscillator slightly against the others for a wider, beating sound."},
    {"Oscillator/FM Mode", "FM mode",
     "What modulates oscillator 2's frequency: oscillator 1 (triangle or its wave), noise, or the Virus' audio "
     "inputs. FM Amount sets how much."},
    {"Oscillator/FM Amount", "FM amount",
     "Strength of the frequency modulation chosen with FM Mode: small amounts add bite, large ones metallic tones."},
    {"Oscillator/FilterEnv > Pitch", "Filter env > pitch",
     "Lets the filter envelope bend oscillator 2's pitch; with Sync on this gives the classic sync sweep."},
    {"Oscillator/FilterEnv > FM", "Filter env > FM",
     "Lets the filter envelope change the FM amount, for FM sounds that start bright and settle."},
    {"Oscillator HyperSaw/Density", "Density",
     "How many sawtooth waves the HyperSaw stacks (1 to 9), crossfaded smoothly."},
    {"Oscillator HyperSaw/DetuneSpread", "Spread",
     "How far the stacked saws are detuned against each other: low for a thick unison, high for a big supersaw."},
    {"Oscillator HyperSaw/CrossOscillatorSyncFrequency", "Sync frequency",
     "The frequency offset of the HyperSaw's internal sync oscillator. Sweep it for sync-style timbres."},
    {"Oscillator HyperSaw/FilterEnv > Pitch", "Filter env > pitch",
     "Lets the filter envelope bend the oscillator's pitch."},
    {"Oscillator HyperSaw/FilterEnv > SyncFrequency", "Filter env > sync",
     "Lets the filter envelope sweep the sync frequency of the HyperSaw oscillators."},
    {"Oscillator Wavetable/WaveTableSelect", "Wavetable",
     "Chooses the wavetable: a set of waves the oscillator moves through with Index."},
    {"Oscillator Wavetable/WaveTableIndex", "Index",
     "Position inside the wavetable. Moving it with an LFO or an envelope is what makes wavetable sounds come alive."},
    {"Oscillator Wavetable/Interpolation", "Interpolation",
     "How smoothly the waves of the table blend while Index moves: 0 morphs smoothly, high values step from wave "
     "to wave."},
    {"Oscillator Wavetable/FormantShift", "Formant shift",
     "Grain and Formant types: shifts the formants of the table up or down, a bit like a sync sweep."},
    {"Oscillator Wavetable/Sync", "Formant spread",
     "Complex Grain and Formant types: spreads the formants apart; the effect depends on Formant Shift."},
    {"Oscillator Wavetable/Internal Detune", "Local detune / detune",
     "Wave PWM: the built-in detune of the two table copies. Complex types: a second layer an octave apart, "
     "detuned against the first."},
    {"Oscillator Wavetable/FM Amount", "FM amount", "Strength of the frequency modulation of the wavetable "
                                                    "oscillator."},
    {"Oscillator Wavetable/FM Mode WaveTable", "FM mode",
     "Frequency modulation or phase modulation (the digital FM style) for oscillator 2 in wavetable modes."},
    {"Oscillator Wavetable/FilterEnv > Pitch", "Filter env > pitch",
     "Lets the filter envelope bend oscillator 2's pitch."},
    {"Oscillator Wavetable/FilterEnv > FM", "Filter env > FM", "Lets the filter envelope change the FM amount."},
    {"Oscillator 3/Mode", "Oscillator 3",
     "Off, Slave (follows oscillator 2 including its shape and modulation), or a fixed wave of its own."},
    {"Oscillator 3/Semitone", "Semitone", "Pitch of oscillator 3 (ignored in Slave mode)."},
    {"Oscillator 3/Detune", "Detune", "Detunes oscillator 3 downwards (ignored in Slave mode)."},
    {"Oscillator 3/Volume", "Volume", "Level of oscillator 3."},
    {"Oscillators/KeyMode", "Key mode",
     "Poly plays chords. Mono 1-4 play one note at a time (multi or single trigger, portamento always or only "
     "when legato). Hold keeps notes until all keys are up and a new one is played."},
    {"Oscillators/Phase Init", "Initial phase",
     "Off lets the oscillators start anywhere, like a free-running analog synth. Other values start every note "
     "the same way - good for punchy, repeatable attacks."},
    {"Oscillators/Volume+Saturation", "Osc volume / saturation",
     "Level of the oscillators going into the filters. The centre is full level; turning further drives the "
     "saturation chosen in the filter section."},
    {"Suboscillator/Volume", "Sub oscillator",
     "Level of the sub oscillator, one octave below oscillator 1. With HyperSaw it crossfades to the HyperSub."},
    {"Suboscillator/Shape", "Sub shape", "Square or triangle wave for the sub oscillator."},
    {"Noise Oscillator/Volume", "Noise", "Level of the noise generator."},
    {"Noise Oscillator/Color", "Noise colour",
     "Tone of the noise: darker to the left, brighter to the right, white noise in the centre."},
    {"Ring Modulator/Volume", "Ring modulator",
     "Level of oscillator 1 multiplied with oscillator 2: bell-like and metallic tones."},
    {"Patch Common/Portamento", "Portamento",
     "Glide time between notes. How it behaves depends on Key Mode."},
    {"Patch Common/Punch Intensity", "Punch",
     "Adds snap to the start of notes with fast amplifier attacks."},

    // --- filters -----------------------------------------------------------------------------------------------
    {"Filter/Cutoff", "Cutoff",
     "Where the filter starts to cut. The most important tone knob: lower is darker (low pass), and the envelope "
     "and LFOs move it from here."},
    {"Filter/Resonance", "Resonance",
     "Boosts the frequencies around the cutoff. High values whistle and can self-oscillate - mind your speakers."},
    {"Filter/Envelope Amount", "Env amount", "How much the filter envelope moves the cutoff."},
    {"Filter/Keyfollow", "Key follow",
     "How much the cutoff follows the keys: higher notes open the filter more, so the tone stays even."},
    {"Filter/Mode", "Filter mode",
     "Low pass (cuts highs), high pass (cuts lows), band pass (keeps a band) or band stop (removes a band). "
     "Filter 1 also has analog ladder models with 1 to 4 poles and their own saturation."},
    {"Filter/Polarity", "Env polarity",
     "Inverts the envelope's effect on this filter: the cutoff moves down instead of up."},
    {"Filter 2/Offset for FilterLink", "Cutoff 2 offset",
     "With the filter link on, filter 2 follows filter 1's cutoff and this knob sets the distance between them."},
    {"Filters/Balance", "Filter balance",
     "Mix of the two filters' outputs: filter 1 only on the left, both in the centre, filter 2 only on the right."},
    {"Filters/Routing", "Filter routing",
     "Serial (one filter after the other, 4 or 6 poles), parallel, or split (oscillator 1 and sub to filter 1, "
     "the rest to filter 2, left and right)."},
    {"Filters/Cutoff Link", "Cutoff link",
     "Lets the cutoff knob move both filters; the second cutoff knob then sets an offset."},
    {"Filters/Keyfollow Base", "Key follow base",
     "The note around which key follow works: notes above open the filter, notes below close it."},
    {"Voice Saturation/Type", "Saturation",
     "Distortion per voice before the filters (soft to hard, digital, wave shaper, bit and rate reducers ...). "
     "The oscillator volume above centre sets how hard it drives."},
    {"Surround/Balance", nullptr, "Balance of the part's surround channels."},
    {"Global Surround/Output Control", "Second / third output",
     "Where the part's extra output goes (an analog or USB output)."},

    // --- envelopes ---------------------------------------------------------------------------------------------
    {"Filter Envelope/Attack", "Attack", "Time the filter envelope needs to rise to its peak."},
    {"Filter Envelope/Decay", "Decay", "Time from the peak down to the sustain level."},
    {"Filter Envelope/Sustain", "Sustain", "Level held while the key is down."},
    {"Filter Envelope/Sustain Time", "Sustain slope",
     "Lets the sustain level fall (left) or rise (right) while the key is held, instead of staying flat."},
    {"Filter Envelope/Release", "Release", "Time to fade out after the key is released."},
    {"Amp Envelope/Attack", "Attack", "How fast the sound fades in: short for plucks, long for pads."},
    {"Amp Envelope/Decay", "Decay", "Time from the peak down to the sustain level."},
    {"Amp Envelope/Sustain", "Sustain", "Volume held while the key is down."},
    {"Amp Envelope/Sustain Time", "Sustain slope",
     "Lets the volume fall (left) or rise (right) while the key is held."},
    {"Amp Envelope/Release", "Release", "How long the sound rings out after the key is released."},
    {"Envelope/Attack", "Attack", "Envelope 3 and 4 are extra envelopes for the modulation matrix. Time to rise."},
    {"Envelope/Decay", "Decay", "Time from the peak down to the sustain level."},
    {"Envelope/Sustain", "Sustain", "Level held while the key is down."},
    {"Envelope/Sustain Time", "Sustain slope", "Lets the sustain level fall or rise while the key is held."},
    {"Envelope/Release", "Release", "Time to fall back after the key is released."},

    // --- LFOs --------------------------------------------------------------------------------------------------
    {"LFO/Rate", "Rate", "Speed of the LFO when it is not synced to the clock."},
    {"LFO/Shape", "LFO shape",
     "Waveform of the LFO: sine, triangle, saw, square, sample & hold, sample & glide or one of the spectral waves."},
    {"LFO/Contour", "Contour", "Bends the LFO wave, e.g. from sine towards triangle or square."},
    {"LFO/Clock", "Clock", "Syncs the LFO to the tempo (note values) instead of the free rate."},
    {"LFO Clock/Switch", "Clock sync", "Switches between a free rate and a tempo-synced rate."},
    {"LFO Clock/Divider", "Clock value", "The note value of the synced LFO."},
    {"LFO/Keyfollow", "Key follow", "Higher notes make the LFO faster."},
    {"LFO/Trigger Phase", "Trigger phase",
     "Off: the LFO runs freely. Other values restart it at this point of the wave with every note."},
    {"LFO/Envelope Mode", "Envelope mode",
     "The LFO runs through one cycle per note and stops - an extra simple envelope."},
    {"LFO/Poly Mode", "Poly / mono",
     "Poly: every voice has its own, slightly detuned LFO. Mono: all voices share one LFO."},
    {"LFO/Fade In", "Fade in", "LFO 3 fades in after each note starts - the classic delayed vibrato."},
    {"LFO/Dest Assign Target", "Assign target", "A free destination for this LFO (any modulation target)."},
    {"LFO/Dest Assign Amount", "Assign amount", "How much the LFO moves the assign target."},
    {"LFO 1/Dest Osc1", "Osc 1 pitch", "How much LFO 1 moves oscillator 1's pitch (vibrato)."},
    {"LFO 1/Dest Osc2", "Osc 2 pitch", "How much LFO 1 moves oscillator 2's (and 3's) pitch."},
    {"LFO 1/Dest Pulsewidth", "Pulse width", "How much LFO 1 moves the pulse width of both oscillators (PWM)."},
    {"LFO 1/Dest Reso1+2", "Resonance", "How much LFO 1 moves the resonance of both filters."},
    {"LFO 1/Dest Filtergain", "Filter gain", "How much LFO 1 moves the level going into the filters (tremolo-like)."},
    {"LFO 2/Dest Cutoff1", "Cutoff 1", "How much LFO 2 moves filter 1's cutoff (wah-like movement)."},
    {"LFO 2/Dest Cutoff2", "Cutoff 2", "How much LFO 2 moves filter 2's cutoff."},
    {"LFO 2/Dest Shape1+2", "Shape", "How much LFO 2 moves the shape of oscillators 1 and 2."},
    {"LFO 2/Dest FM Amount", "FM amount", "How much LFO 2 moves the FM amount."},
    {"LFO 2/Dest Panorama", "Panorama", "How much LFO 2 moves the sound between left and right (auto-pan)."},

    // --- modulation matrix -------------------------------------------------------------------------------------
    {"Assign Slot/Source", "Matrix source",
     "What modulates: an envelope, LFO, velocity, mod wheel, aftertouch, a MIDI controller and more. Each slot "
     "sends one source to up to three destinations."},
    {"Assign Slot/Destination", "Matrix destination", "Which parameter this source moves."},
    {"Assign Slot/Amount", "Matrix amount",
     "How much and in which direction the source moves the destination (negative values invert it)."},

    // --- arpeggiator -------------------------------------------------------------------------------------------
    {"Arpeggiator/Mode", "Arpeggiator mode",
     "Off, up, down, up and down, as played, random, chord - or Arp to Matrix, where the pattern only drives the "
     "modulation matrix."},
    {"Arpeggiator/Pattern", "Pattern",
     "The rhythm pattern; User is the pattern you draw below."},
    {"Arpeggiator/OctaveRange", "Octaves", "Over how many octaves the arpeggio climbs before it starts again."},
    {"Arpeggiator/Clock", "Resolution", "Note value of each arpeggio step (1/16 is the usual)."},
    {"Arpeggiator/NoteLength", "Note length", "Shortens or lengthens the arpeggiated notes."},
    {"Arpeggiator/Swing", "Swing", "Delays every other step for a shuffle feel; about 66 % is a full triplet swing."},
    {"Arpeggiator/HoldEnable", "Hold", "Keeps the arpeggio running after you let go of the keys."},
    {"Arpeggiator/UserPatternLength", "Pattern length", "Number of steps of the user pattern."},
    {"Arpeggiator User Pattern/Step Velocity", "Step",
     "User pattern: drag a bar up or down for the step's velocity; drag its right edge for the length."},
    {"Arpeggiator User Pattern/Step Length", "Step length", "User pattern: the length of this step."},
    {"Arpeggiator User Pattern/Step Bitfield", "Step on/off", "User pattern: whether this step plays."},
    {"VirusControl/ExecuteARPShiftLeft", "Shift left", "Moves the user pattern one step to the left."},
    {"VirusControl/ExecuteARPShiftRight", "Shift right", "Moves the user pattern one step to the right."},
    {"VirusControl/ExecuteARPRandomVelo", "Random velocity", "Gives the user pattern's steps random velocities."},
    {"VirusControl/ExecuteARPRandomLength", "Random length", "Gives the user pattern's steps random lengths."},
    {"VirusControl/ExecuteARPRandomNote", "Random steps", "Switches the user pattern's steps on and off at random."},

    // --- common ------------------------------------------------------------------------------------------------
    {"Patch Common/Volume", "Patch volume",
     "Overall level of the patch; around 100 leaves headroom when many notes play at once."},
    {"Patch Common/Panorama", "Panorama", "Position of the patch between left and right."},
    {"Patch Common/Transpose", "Transpose", "Shifts the whole patch in semitones."},
    {"Patch Common/Bender Up", "Bend up", "How far the pitch bender bends up, in semitones."},
    {"Patch Common/Bender Down", "Bend down", "How far the pitch bender bends down, in semitones."},
    {"Patch Common/Bender Scale", "Bender scale",
     "Linear, or exponential for finer control near the bender's centre."},
    {"Patch Common/Smooth Mode", "Smooth mode",
     "How parameter changes are smoothed: off, on, automatic, per note, or quantised to note values for "
     "rhythmic stepping."},
    {"Unison/Mode", "Unison voices",
     "How many voices play each note (Twin to 16): fatter and wider, but fewer notes at once."},
    {"Unison/Detune", "Unison detune", "How much the stacked voices are detuned against each other."},
    {"Unison/Pan Spread", "Pan spread", "How wide the stacked (or split) voices spread in the stereo field."},
    {"Unison/LFO Phase", "LFO phase", "Offsets the LFOs of the stacked voices for an even more animated sound."},
    {"Velocity Response/Volume", "Velocity > volume", "How much playing harder makes the sound louder."},
    {"Velocity Response/Panorama", "Velocity > panorama", "How much velocity moves the sound left or right."},
    {"Velocity Response/FM Amount", "Velocity > FM", "How much velocity changes the FM amount (or HyperSaw sync)."},
    {"Velocity Response/Osc Shape", "Velocity > shape",
     "How much velocity changes this oscillator's shape (density for HyperSaw, index for wavetables)."},
    {"Velocity Response/Pulse Width", "Velocity > pulse width", "How much velocity changes the pulse width."},
    {"Velocity Response/Filter Envelope Amount", "Velocity > env amount",
     "How much velocity changes this filter's envelope amount - harder playing opens the filter more."},
    {"Velocity Response/Filter Resonance", "Velocity > resonance", "How much velocity changes this filter's "
                                                                   "resonance."},
    {"Softknobs/Softknob", "Soft knob",
     "A performance knob: it controls whatever its destination below is set to (also on the Virus' panel)."},
    {"Soft Knobs/Destination", "Soft knob destination", "The parameter this soft knob controls."},
    {"Soft Knobs/Name", "Soft knob name", "The word shown above the soft knob."},
    {"Multi Part Parameter/Detune", "Part detune", "Fine-tunes the whole part."},
    {"Multi Part Parameter/Output Select Control", "Part output",
     "Which output the part plays to: USB 1 (Out 1), USB 2 (Out 2), USB 3 (Out 3, with the 3-output "
     "configuration) or the analog outputs."},

    // --- inputs ------------------------------------------------------------------------------------------------
    {"Input/Mode", "Input mode",
     "Feeds an input through this part's filters and effects instead of (or with) the oscillators. Dynamic: "
     "plays with notes and envelopes. Static: always on, no notes needed."},
    {"Input/Select", "Input select",
     "Which input channel: left, both or right. With TI Revive's 3 Outs / 1 Input configuration this is the "
     "plug-in's Sidechain input."},
    {"Input/Atomizer", "Atomizer",
     "A live looper for the input signal, played from the keyboard. Switch it on to use the Virus' Atomizer keys."},
    {"Input Follower/Select", "Input follower",
     "Uses the level of an input as an envelope that replaces the filter envelope (great for ducking or "
     "auto-wah effects driven by another track)."},
    {"Input Follower/Attack", "Follower attack", "How fast the follower reacts to rising input level."},
    {"Input Follower/Release", "Follower release", "How fast the follower falls back when the input gets quieter."},
    {"Input Follower/Level", "Follower sensitivity", "How strongly the follower responds to the input."},

    // --- effects -----------------------------------------------------------------------------------------------
    {"FX/Delay Switch", "Delay on/off", "Switches the delay effect on or off."},
    {"FX/Reverb Switch", "Reverb on/off", "Switches the reverb effect on or off."},
    {"Delay/Type", "Delay type",
     "Classic delay, or tape delays: clocked, free, or Doppler (pitch bends when you change the time, like real tape)."},
    {"Delay/Mode", "Delay mode",
     "Simple delay, ping-pong (left and right in a ratio) or rhythmic patterns synced to the tempo."},
    {"Delay/Send", "Delay send", "How much of the sound goes into the delay; at the top only the echo remains."},
    {"Delay/Time", "Delay time", "Delay time in milliseconds (used when the clock is off)."},
    {"Delay/Clock", "Delay clock", "Syncs the delay time to the tempo as a note value."},
    {"Delay/Feedback", "Feedback", "How many repeats you hear."},
    {"Delay/Color", "Colour", "Makes the repeats darker (left) or brighter (right) each time around."},
    {"Delay/LFO Rate", "Mod rate", "Speed of the delay's own LFO that wobbles the delay time."},
    {"Delay/LFO Depth", "Mod depth", "How much the delay's LFO moves the time - chorus-like or tape-like wobble."},
    {"Delay/LFO Shape", "Mod shape", "Waveform of the delay's LFO; S&G gives a tape-echo character."},
    {"Delay/Tape Delay Time", "Tape time", "Delay time of the tape delay."},
    {"Delay/Tape Delay Left Clock", "Left clock", "Note value of the left tape delay channel."},
    {"Delay/Tape Delay Right Clock", "Right clock", "Note value of the right tape delay channel."},
    {"Delay/Tape Delay Ratio", "Ratio", "Left/right time ratio of the free tape delay."},
    {"Delay/Tape Delay Feedback", "Tape feedback",
     "Repeats of the tape delay: at the centre they go on forever, above that they saturate without getting louder."},
    {"Delay/Tape Delay Center Frequency", "Tape frequency",
     "Which frequency band the repeats keep - like the tone of an old tape machine."},
    {"Delay/Tape Delay Bandwidth", "Tape bandwidth", "Width of that band: higher values make the repeats thinner."},
    {"Delay/Tape Delay Modulation", "Tape modulation", "Wow and flutter of the tape."},
    {"Reverb/Mode", "Reverb mode", "Reverb, or two feedback modes that repeat the reverb tail."},
    {"Reverb/Type", "Room type", "Size of the room: ambience, small room, large room, hall."},
    {"Reverb/Send", "Reverb send", "How much reverb; at the top only the reverb remains."},
    {"Reverb/Time", "Reverb time", "Length of the reverb tail."},
    {"Reverb/Damping", "Damping", "Removes highs from the tail, like a room with soft surfaces."},
    {"Reverb/Color", "Colour", "Darker or brighter reverb."},
    {"Reverb/Predelay", "Predelay", "Gap before the reverb starts - keeps the dry sound clear."},
    {"Reverb/Clock", "Predelay clock", "Syncs the predelay to the tempo."},
    {"Reverb/Feedback", "Feedback", "Feedback modes: how often the reverb repeats."},
    {"EQ/Low Frequency", "Low EQ frequency", "Corner frequency of the low shelf."},
    {"EQ/Low Gain", "Low EQ gain", "Boost or cut of the lows."},
    {"EQ/Mid Frequency", "Mid EQ frequency", "Centre frequency of the mid band (sweep it with a high Q for wah)."},
    {"EQ/Mid Gain", "Mid EQ gain", "Boost or cut of the mid band."},
    {"EQ/Mid Q", "Mid EQ Q", "Width of the mid band: low is wide, high is narrow."},
    {"EQ/High Frequency", "High EQ frequency", "Corner frequency of the high shelf."},
    {"EQ/High Gain", "High EQ gain", "Boost or cut of the highs."},
    {"Patch Distortion/Type", "Distortion type",
     "Distortion for the whole part (unlike the per-voice saturation): soft to hard curves, folds, bounces, "
     "bit and rate reducers and several overdrive models."},
    {"Patch Distortion/Intensity", "Intensity", "How hard the distortion is driven."},
    {"Patch Distortion/Mix", "Mix", "Blend of the clean and the distorted sound."},
    {"Patch Distortion/Quality", "Quality", "Character setting of some distortion types."},
    {"Patch Distortion/Tone127", "Tone", "Tone control of the overdrive types (darker or brighter)."},
    {"Patch Distortion/Tone64", "Tone", "Tone control of the overdrive types (a low pass: turn down for a softer sound)."},
    {"Patch Distortion/Treble Booster", "Treble booster",
     "Pushes the mids and highs before the distortion for a livelier, guitar-like drive."},
    {"Patch Distortion/High Cut", "High cut", "Removes harsh highs after the distortion."},
    {"Analog Boost/Type", "Character",
     "Character effects: analog boost, vintage models, pad opener, lead and bass enhancers, stereo widener, "
     "speaker cabinet."},
    {"Analog Boost/Intensity", "Intensity", "How strong the character effect is."},
    {"Analog Boost/Tune", "Tune", "Frequency or tone setting of the character effect."},
    {"Chorus/Type", "Chorus type",
     "Classic, vintage (bucket-brigade style), hyper chorus (huge, simple), air chorus (subtle stereo), vibrato or "
     "rotary speaker."},
    {"Chorus/Mix", "Mix", "Dry/wet mix (classic chorus: centre for the strongest effect)."},
    {"Chorus/Mix2", "Mix", "Dry/wet mix of the chorus."},
    {"Chorus/Delay", "Delay", "Base delay of the classic chorus."},
    {"Chorus/Feedback", "Feedback", "Feeds the chorus back into itself - flanger-like sounds."},
    {"Chorus/LFO Rate", "Rate", "Speed of the chorus modulation."},
    {"Chorus/LFO Depth", "Depth", "Amount of chorus modulation."},
    {"Chorus/LFO Shape", "Shape", "Waveform of the chorus LFO."},
    {"Chorus/Amount", "Amount", "Hyper chorus: how many delay lines per side (1 to 3)."},
    {"Chorus/X Over", "X-over", "Keeps the lows out of the chorus for a solid bass."},
    {"Chorus/Speed", "Rotor speed", "Rotary speaker: slow or fast; the rotors speed up and slow down gradually."},
    {"Chorus/Mic Angle", "Mic angle", "Rotary speaker: stereo width from the angle of the microphones."},
    {"Chorus/Distance", "Mic distance", "Rotary speaker: closer microphones give more tremolo."},
    {"Chorus/LowHigh Bal", "Low/high balance", "Rotary speaker: balance between the bass rotor and the horn."},
    {"Phaser/Mix", "Mix", "Dry/wet mix of the phaser."},
    {"Phaser/Frequency", "Frequency", "Centre frequency of the phaser notches."},
    {"Phaser/Feedback", "Feedback", "Resonance of the phaser."},
    {"Phaser/Rate", "Rate", "Speed of the phaser sweep."},
    {"Phaser/Depth", "Depth", "Depth of the phaser sweep."},
    {"Phaser/Spread", "Spread", "Spacing of the notches."},
    {"Phaser/Stages", "Stages", "Number of filter stages (1 to 6): more stages, more complex sweep."},
    {"Filter Bank/Type", "Filterbank type",
     "Ring modulator, frequency shifter, vowel filter, comb filter, crossfade filters (low to band to high pass) "
     "or variable-slope filters - all applied to the whole part."},
    {"Filter Bank/Mix", "Mix", "Dry/wet mix (ring modulator: the centre gives amplitude modulation)."},
    {"Filter Bank/Frequency", "Frequency",
     "Ring modulator / frequency shifter: frequency of the modulating sine; near the centre gives slow tremolo-like "
     "effects."},
    {"Filter Bank/Filter Frequency", "Cutoff", "Cutoff of the crossfade and variable-slope filters."},
    {"Filter Bank/Comb Frequency", "Comb pitch", "The note the comb filter resonates at."},
    {"Filter Bank/Vowel Frequency", "Vowel", "Sweeps through the vowels (u, o, a, e, i ...) - a talk-box effect."},
    {"Filter Bank/Resonance", "Resonance", "Resonance of the filterbank."},
    {"Filter Bank/Filter Type", "Filter type", "Crossfade filters: from low pass through band pass to high pass."},
    {"Filter Bank/Poles", "Poles", "Variable-slope filters: 2 to 6 poles (steeper with more)."},
    {"Filter Bank/Slope", "Slope", "Variable-slope filters: makes each pole gentler."},
    {"Filter Bank/Stereo Phase", "Stereo phase", "Offset between left and right for a wider sound."},
    {"Filter Bank/Shape L", "Shape left",
     "Frequency shifter: up-shifted frequencies (right) or down-shifted (left) on the left channel."},
    {"Filter Bank/Shape R", "Shape right", "The same for the right channel."},
    {"Vocoder/Mode", "Vocoder",
     "Switches the vocoder on: the carrier (oscillators, noise or an input) is shaped by the modulator (usually "
     "an input with a voice). It replaces the normal filters."},
    {"Vocoder/Bands", "Bands", "Number of filter bands: more for clear speech, fewer for robot voices."},
    {"Vocoder/Balance", "Balance", "Mix of carrier and modulator; the centre gives the pure vocoder sound."},
    {"Vocoder/Spectral Balance", "Spectral balance", "Tilts the vocoder output towards lows or highs."},
    {"Vocoder/Attack", "Attack", "How fast the bands follow the modulator."},
    {"Vocoder/Release", "Release", "How fast the bands fall back."},
    {"Vocoder/Carrier Center Frequency", "Carrier frequency", "Centre of the carrier filter bank."},
    {"Vocoder/Carrier Frequency Spread", "Carrier spread", "Spacing of the carrier bands (negative inverts them)."},
    {"Vocoder/Carrier Q Factor", "Carrier Q", "Steepness of the carrier bands."},
    {"Vocoder/Modulator Frequency Offset", "Modulator offset", "Shifts the modulator bands against the carrier."},
    {"Vocoder/Modulator Frequency Spread", "Modulator spread", "Spacing of the modulator bands."},
    {"Vocoder/Modulator Q Factor", "Modulator Q", "Steepness of the modulator bands."},

    // --- remote templates --------------------------------------------------------------------------------------
    {"Remote Templates/Selected Template", "Remote templates",
     "Templates that map a MIDI controller's knobs to Virus parameters."},
    {"Remote Templates/Selected Knob", nullptr, "The knob of the template you are editing."},
    {"Remote Templates/Knob Template", nullptr, "The parameter this knob controls."},
    {"Remote Templates/Knob Name", nullptr, "The knob's name."},
    {"Remote Templates/Template Name", nullptr, "The template's name."},
    {"Remote Templates/MIDI String", nullptr, "The MIDI message the knob sends."},
    {"Remote Templates/MIDI String Info", nullptr, "What the MIDI message means."},
    {"Remote Templates/MIDI Channel Behaviour", nullptr, "Which MIDI channel the template uses."},
};

// "Assign Slot 3/Amount 2" -> "Assign Slot/Amount": digits (and a space before them) removed
std::string strip_numbers(const std::string &s)
{
    std::string out;
    for (size_t i = 0; i < s.size(); i++) {
        if (isdigit((unsigned char)s[i])) {
            if (!out.empty() && out.back() == ' ')
                out.pop_back();
            while (i + 1 < s.size() && isdigit((unsigned char)s[i + 1]))
                i++;
            if (i + 1 < s.size() && s[i + 1] == ' ' && (out.empty() || out.back() == '/'))
                i++; // "07 Volume" -> "Volume"
            continue;
        }
        out += s[i];
    }
    return out;
}

const std::unordered_map<std::string, const Entry *> &index()
{
    static const std::unordered_map<std::string, const Entry *> m = [] {
        std::unordered_map<std::string, const Entry *> r;
        for (auto &e : ENTRIES)
            r[e.key] = &e;
        return r;
    }();
    return m;
}

} // namespace

bool help_for(const std::string &param, HelpText &out)
{
    const auto &idx = index();
    auto it = idx.find(param);
    if (it == idx.end())
        it = idx.find(strip_numbers(param));
    if (it == idx.end() && param.rfind("Locks/", 0) == 0)
        it = idx.find("Locks/*");
    if (it == idx.end())
        return false;
    const Entry &e = *it->second;
    out.text = e.text;
    // title: the entry's own, else the parameter's name; the section (with its number) in brackets
    size_t slash = param.find('/');
    std::string section = slash == std::string::npos ? std::string() : param.substr(0, slash);
    std::string name = e.title ? e.title : param.substr(slash == std::string::npos ? 0 : slash + 1);
    auto squash = [](const std::string &v) { // "Sub oscillator" ~ "Suboscillator"
        std::string r;
        for (char ch : v)
            if (ch != ' ')
                r += (char)tolower((unsigned char)ch);
        return r;
    };
    const std::string st = squash(name), ss = squash(section);
    bool plain = section.empty() || section == "VirusControl" || section == "Repository Actions" ||
                 section == "Internal MIDI Standard Parameters" || section == "Multi Parameters" ||
                 st.find(ss) != std::string::npos || ss.find(st) != std::string::npos;
    out.title = plain ? name : name + " (" + section + ")";
    return true;
}

std::string inactive_reason(const std::string &condition)
{
    std::string r = condition;
    for (const char *prefix : {"VirusControl/Display/", "Parameter Active Condition/", "VirusControl/"})
        if (r.rfind(prefix, 0) == 0) {
            r = r.substr(strlen(prefix));
            break;
        }
    for (auto &ch : r)
        if (ch == '/')
            ch = ' ';
    auto lower = [&](const std::string &from, const std::string &to) {
        for (size_t p; (p = r.find(from)) != std::string::npos;)
            r.replace(p, from.size(), to);
    };
    lower(" Is NOT ", " is not ");
    lower(" Is ", " is ");
    return r;
}

std::string depends_reason(const std::vector<std::string> &params)
{
    std::string r;
    for (auto &p : params) {
        std::string n = p;
        std::replace(n.begin(), n.end(), '/', ' ');
        r += (r.empty() ? "depends on " : ", ") + n;
    }
    return r;
}

int help_count() { return (int)index().size(); }

} // namespace vti
