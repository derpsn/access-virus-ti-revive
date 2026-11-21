-- scripts/reaper-projcompat/make.lua - REAPER ReaScript, step 1 of a project test (E556): run as
--   reaper -nosplash -new make.lua   (Xvfb works); writes make.txt + rename-test.RPP next to this script. Rule 2: only
--   what a DAW does when it adds the plug-in and saves; parameters are read, never set.
-- step 1: a project with the installed plug-in (0.8.42), saved by REAPER itself
local dir = ({reaper.get_action_context()})[2]:match("(.*/)")
local out = io.open(dir .. "make.txt", "w")
reaper.InsertTrackAtIndex(0, true)
local tr = reaper.GetTrack(0, 0)
local fx = reaper.TrackFX_AddByName(tr, "TI Revive Virus Control (TI Revive)", false, -1)
out:write("fx index " .. fx .. "\n"); out:flush()
local t0 = reaper.time_precise()
local function finish()
  if reaper.time_precise() - t0 < 15 then reaper.defer(finish) return end
  local _, name = reaper.TrackFX_GetFXName(tr, 0, "")
  out:write("name " .. name .. "\nparams " .. reaper.TrackFX_GetNumParams(tr, 0) .. "\n")
  for i = 0, 39 do out:write(string.format("p%d %.6f\n", i, reaper.TrackFX_GetParamNormalized(tr, 0, i))) end
  reaper.Main_SaveProjectEx(0, dir .. "rename-test.RPP", 0)
  out:write("saved\n"); out:close()
  reaper.Main_OnCommand(40004, 0) -- quit
end
reaper.defer(finish)
