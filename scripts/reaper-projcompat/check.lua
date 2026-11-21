-- scripts/reaper-projcompat/check.lua - step 2 (E556): after installing the new build, run as
--   reaper -nosplash -new check.lua; writes check.txt (fx count, name, offline, params, the same 40 values).
-- step 3: open the saved project with the renamed build and compare
local dir = ({reaper.get_action_context()})[2]:match("(.*/)")
local out = io.open(dir .. "check.txt", "w")
reaper.Main_openProject("noprompt:" .. dir .. "rename-test.RPP")
local t0 = reaper.time_precise()
local function finish()
  if reaper.time_precise() - t0 < 15 then reaper.defer(finish) return end
  local tr = reaper.GetTrack(0, 0)
  out:write("fx count " .. reaper.TrackFX_GetCount(tr) .. "\n")
  local _, name = reaper.TrackFX_GetFXName(tr, 0, "")
  out:write("name " .. name .. "\noffline " .. tostring(reaper.TrackFX_GetOffline(tr, 0)) .. "\nparams " .. reaper.TrackFX_GetNumParams(tr, 0) .. "\n")
  for i = 0, 39 do out:write(string.format("p%d %.6f\n", i, reaper.TrackFX_GetParamNormalized(tr, 0, i))) end
  out:close()
  reaper.Main_OnCommand(40004, 0)
end
reaper.defer(finish)
