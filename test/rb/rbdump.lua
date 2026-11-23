-- rbdump.lua: run by test/rb/mamerun.sh in a headless MAME. Each frame (at
-- the VBLANK's start, where MAME's frame_done runs; frame n at n * 16,640
-- of the main CPU's cycles, rb_host's frame n) it applies the inputs
-- the script gives for that frame, then appends the board to the dump, the
-- records rb_host -dump writes: 'RBFR', the frame (u32), the RAM
-- ($0000-$BFFF, from the share: never the ROMs banked over it), the
-- palette (16), the CMOS (1K). RB_SCRIPT, RB_DUMP, RB_FRAMES and RB_EVERY
-- (a record every Nth frame) come from the environment. Script lines:
-- "frame :PORT hex", the port's value from that frame on (active high).
if not _G.rb_dump then
  _G.rb_dump = true
  local ports = manager.machine.ioport.ports
  local events = {}
  local script = os.getenv("RB_SCRIPT")
  if script and script ~= "" then
    for line in io.lines(script) do
      local f, port, v = line:match("^(%d+)%s+(%S+)%s+(%x+)")
      if f then
        f = tonumber(f)
        events[f] = events[f] or {}
        table.insert(events[f], { port, tonumber(v, 16) })
      end
    end
  end
  local frames = tonumber(os.getenv("RB_FRAMES") or "600")
  local every = tonumber(os.getenv("RB_EVERY") or "1")
  local dumpname = os.getenv("RB_DUMP")
  local out = dumpname and dumpname ~= "" and assert(io.open(dumpname, "wb"))
  local shares = manager.machine.memory.shares
  local vram, pal, cmos = shares[":videoram"], shares[":paletteram"], shares[":nvram"]
  local function bytes(s, n)
    local t = {}
    for k = 0, n - 1, 4 do t[#t + 1] = string.pack(">I4", s:read_u32(k)) end
    return table.concat(t)
  end
  -- the frame by the machine's time (n at n * 16.64 ms), not by counting
  -- callbacks: when the script starts isn't fixed
  local seen = {}
  _G.rb_sub = emu.register_frame_done(function()
    local n = math.floor(manager.machine.time:as_double() / 0.01664 + 0.5)
    if seen[n] then return end
    seen[n] = true
    for _, e in ipairs(events[n] or {}) do
      for _, f in pairs(ports[e[1]].fields) do f:set_value((e[2] & f.mask) ~= 0 and 1 or 0) end
    end
    if out and n % every == 0 then
      out:write("RBFR", string.pack("<I4", n), bytes(vram, 0xC000), bytes(pal, 16), bytes(cmos, 0x400))
    end
    if n >= frames then
      if out then out:close() end
      manager.machine:exit()
    end
  end)
end
