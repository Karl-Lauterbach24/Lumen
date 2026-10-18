-- Lumen's controls in the player window, in the look of Lumen TV: the title and the clock at the top; at the foot the
-- time bar (chapters as gaps, what is buffered, a bubble with the time and the chapter under the mouse) and a row of
-- large buttons. Shown when the mouse moves, on a key, while paused; gone again after a moment.
--
-- What mpv does not know comes from Lumen as JSON in user-data/lumen/osc (MpvController::publishOsc): the time line of a
-- disc played with its menus, whether a menu is up (then the controls stay away), the tracks as Lumen names them.
-- What the controls do goes back as "script-message lumen-osc <what> [value]" (MpvController::handleClientMessage).
local mp = require 'mp'
local assdraw = require 'mp.assdraw'
local utils = require 'mp.utils'

local HIDE = 2.6                 -- seconds without a mouse move or a key
local ACCENT, ACCENT2 = '#6b82ff', '#9aaaff'
local TEXT, DIM = '#eceef3', '#b4bac8'

local DEFAULTS = {
    play = 'Wiedergabe', pause = 'Pause', back10 = '10 Sekunden zurück', fwd10 = '10 Sekunden vor',
    chprev = 'Voriges Kapitel', chnext = 'Nächstes Kapitel', audio = 'Tonspur', subs = 'Untertitel', off = 'Aus',
    fullscreen = 'Vollbild', chapter = 'Kapitel %1', chapterOf = 'Kapitel %1 von %2', endsAt = 'Ende %1', stop = 'Stopp',
}
local lumen, text = {}, {}
local visible, hideAt, flashUntil = false, 0, 0
local mouse = { x = -1, y = -1, inside = false }
local hover, pressed = nil, false    -- pressed: the element under the mouse when its button went down
local elements = {}              -- what can be clicked, from the last drawing: {name, x1, y1, x2, y2}
local dragging, lastDragSeek = false, 0
local panel = nil                -- the open track list: 'audio' or 'subs'
local idle = true
local W, H = 1920, 1080
local barX1, barX2 = 96, 1824
local ov = mp.create_osd_overlay('ass-events')
local inputOn = false

local function now() return mp.get_time() end
local function T(k, a, b)
    local s = text[k] or DEFAULTS[k] or k
    if a ~= nil then s = s:gsub('%%1', function() return tostring(a) end) end
    if b ~= nil then s = s:gsub('%%2', function() return tostring(b) end) end
    return s
end
local function clock(t)
    if not t or t < 0 or t ~= t then return '–:––' end
    t = math.floor(t)
    local h, m, s = math.floor(t / 3600), math.floor(t / 60) % 60, t % 60
    if h > 0 then return string.format('%d:%02d:%02d', h, m, s) end
    return string.format('%d:%02d', m, s)
end
local function ulen(s) return select(2, s:gsub('[^\128-\191]', '')) end
local function esc(s) return (tostring(s):gsub('\\', '\\\\'):gsub('{', '\\{'):gsub('}', '\\}'):gsub('\n', ' ')) end
local function act(what, value) mp.commandv('script-message', 'lumen-osc', what, value ~= nil and tostring(value) or '') end

-- ---- what plays ----
local function nav() return lumen.nav == true end
local function pos() if nav() then return lumen.position or 0 end return mp.get_property_number('time-pos') or 0 end
local function dur() if nav() then return lumen.duration or 0 end return mp.get_property_number('duration') or 0 end
local function chapters() if nav() then return lumen.chapters or {} end return mp.get_property_native('chapter-list') or {} end
local function chapterAt(t, list)
    local n = 0
    for i, c in ipairs(list) do if (c.time or 0) <= t + 0.25 then n = i end end
    return n
end
local function bufferedEnd()
    if nav() then return 0 end
    local s = mp.get_property_native('demuxer-cache-state')
    return s and s['cache-end'] or 0
end
local function selected(list)
    for _, t in ipairs(list or {}) do if t.selected then return t end end
end
-- (without Lumen's list: mpv's own)
local CODECS = { truehd = 'Dolby TrueHD', eac3 = 'Dolby Digital Plus', ac3 = 'Dolby Digital', dts = 'DTS', aac = 'AAC',
                 flac = 'FLAC', opus = 'Opus', mp3 = 'MP3', pcm_bluray = 'PCM', pcm_s16le = 'PCM', pcm_s24le = 'PCM' }
local function trackLabel(t)
    if not t then return nil end
    local parts = {}
    local lang = t.lang and ((text.langs or {})[t.lang] or t.lang:upper())
    if lang then parts[#parts + 1] = lang end
    if t.type == 'audio' then
        local c = CODECS[t.codec or ''] or (t.codec or ''):upper()
        if (t['codec-profile'] or ''):find('MA') then c = 'DTS-HD MA' end
        if c ~= '' then parts[#parts + 1] = c end
        local ch = t['demux-channel-count']
        if ch then parts[#parts + 1] = ({ 'Mono', 'Stereo', nil, nil, nil, '5.1', '6.1', '7.1' })[ch] or (ch .. ' ch') end
    end
    if t.title and t.title ~= '' then parts[#parts + 1] = t.title end
    return table.concat(parts, ' · ')
end
local function audioLabel()
    local s = selected(lumen.audio)
    if s then return s.label end
    return trackLabel(mp.get_property_native('current-tracks/audio'))
end
local function subsLabel()
    if lumen.subs then local s = selected(lumen.subs); return s and s.label or T('off') end
    return trackLabel(mp.get_property_native('current-tracks/sub')) or T('off')
end

-- ---- drawing (in units of a screen 1080 high) ----
local function color(hex) return string.format('&H%s%s%s&', hex:sub(6, 7), hex:sub(4, 5), hex:sub(2, 3)) end
local function style(col, alpha) return string.format('{\\an7\\pos(0,0)\\bord0\\shad0\\1c%s\\1a&H%02X&}', color(col), math.floor(alpha or 0)) end
local function box(a, x1, y1, x2, y2, col, alpha, r)
    a:new_event(); a:append(style(col, alpha)); a:draw_start()
    if r and r > 0 then a:round_rect_cw(x1, y1, x2, y2, r) else a:rect_cw(x1, y1, x2, y2) end
    a:draw_stop()
end
-- a soft shade across the picture: a dark band, blurred so it fades out (libass has no gradients)
local function shade(a, y1, y2, alpha, blur)
    a:new_event()
    a:append(string.format('{\\an7\\pos(0,0)\\bord0\\shad0\\blur%d\\1c&H000000&\\1a&H%02X&}', blur, alpha))
    a:draw_start(); a:rect_cw(-400, y1, W + 400, y2); a:draw_stop()
end
local function label(a, x, y, an, size, col, s, bold, alpha)
    a:new_event()
    a:append(string.format('{\\an%d\\pos(%d,%d)\\fs%d\\b%d\\bord0\\shad1.2\\4c&H000000&\\4a&H90&\\1c%s\\1a&H%02X&\\q2}',
        an, x, y, size, bold and 1 or 0, color(col), alpha or 0))
    a:append(esc(s))
end
-- a shape of the 24-unit icon box, drawn at (cx, cy) in size s
local function shape(a, col, alpha, paths, cx, cy, s)
    local k = s / 24
    a:new_event(); a:append(style(col, alpha)); a:draw_start()
    for _, p in ipairs(paths) do
        for i = 1, #p, 2 do
            local x, y = cx + (p[i] - 12) * k, cy + (p[i + 1] - 12) * k
            if i == 1 then a:move_to(x, y) else a:line_to(x, y) end
        end
    end
    a:draw_stop()
end
-- an arc of a ring, as one closed path (icon units)
local function arc(cx, cy, r1, r2, a0, a1, n)
    local p = {}
    for i = 0, n do local t = a0 + (a1 - a0) * i / n; p[#p + 1] = cx + r2 * math.cos(t); p[#p + 1] = cy + r2 * math.sin(t) end
    for i = n, 0, -1 do local t = a0 + (a1 - a0) * i / n; p[#p + 1] = cx + r1 * math.cos(t); p[#p + 1] = cy + r1 * math.sin(t) end
    return p
end
local PI = math.pi
local ICONS = {
    play = { { 8, 5, 19.5, 12, 8, 19 } },
    pause = { { 6, 5, 10.2, 5, 10.2, 19, 6, 19 }, { 13.8, 5, 18, 5, 18, 19, 13.8, 19 } },
    chprev = { { 5, 5, 7.6, 5, 7.6, 19, 5, 19 }, { 19.5, 5.5, 19.5, 18.5, 9.5, 12 } },
    chnext = { { 16.4, 5, 19, 5, 19, 19, 16.4, 19 }, { 4.5, 5.5, 4.5, 18.5, 14.5, 12 } },
    back10 = { arc(12, 13, 7.1, 8.9, -PI / 2, PI, 24), { 12.6, 1.6, 12.6, 8.6, 8.2, 5.1 } },
    fwd10 = { arc(12, 13, 7.1, 8.9, 0, 3 * PI / 2, 24), { 11.4, 1.6, 11.4, 8.6, 15.8, 5.1 } },
    audio = { { 3, 9, 7, 9, 12, 4.5, 12, 19.5, 7, 15, 3, 15 }, arc(13, 12, 2.8, 4.4, -0.85, 0.85, 8), arc(13, 12, 6, 7.6, -0.9, 0.9, 10) },
    subs = { { 2.8, 5, 21.2, 5, 21.2, 19, 2.8, 19 }, { 4.6, 6.8, 4.6, 17.2, 19.4, 17.2, 19.4, 6.8 },
             { 6.5, 10.8, 10.5, 10.8, 10.5, 12.6, 6.5, 12.6 }, { 12.5, 10.8, 17.5, 10.8, 17.5, 12.6, 12.5, 12.6 },
             { 6.5, 14, 13.5, 14, 13.5, 15.8, 6.5, 15.8 } },
    fullscreen = { { 4, 4, 10, 4, 10, 6, 6, 6, 6, 10, 4, 10 }, { 20, 4, 20, 10, 18, 10, 18, 6, 14, 6, 14, 4 },
                   { 4, 20, 4, 14, 6, 14, 6, 18, 10, 18, 10, 20 }, { 20, 20, 14, 20, 14, 18, 18, 18, 18, 14, 20, 14 } },
    check = { { 4, 12.5, 6.2, 10.3, 9.6, 13.7, 17.8, 5.5, 20, 7.7, 9.6, 18.1 } },
}

local function button(a, name, icon, cx, cy, size, big)
    local on = hover == name
    local r = size / 2
    box(a, cx - r, cy - r, cx + r, cy + r, on and ACCENT or '#ffffff', on and 0 or (big and 0xD2 or 0xE4), r)
    shape(a, '#ffffff', 0, ICONS[icon], cx, cy, size * (big and 0.5 or 0.5))
    if icon == 'back10' or icon == 'fwd10' then
        label(a, cx + (icon == 'back10' and 1 or -1) * size * 0.015, cy + size * 0.045, 5, math.floor(size * 0.21), '#ffffff', '10', true)
    end
    elements[#elements + 1] = { name, cx - r - 4, cy - r - 4, cx + r + 4, cy + r + 4 }
end

local function render()
    local ow, oh = mp.get_osd_size()
    if not ow or ow <= 0 or oh <= 0 then return end
    H = 1080
    W = math.floor(1080 * ow / oh + 0.5)
    ov.res_x, ov.res_y = W, H
    elements = {}
    if not visible then ov.data = ''; ov:update(); return end
    local a = assdraw.ass_new()
    -- the shades
    shade(a, -300, 150, 0x50, 110)
    shade(a, H - 250, H + 300, 0x2C, 150)

    local d, t = dur(), pos()
    local known = d and d > 0
    local list = chapters()
    local n = chapterAt(t, list)
    -- the top: title, chapter, clock and when the film ends
    local title = (mp.get_property('media-title') or ''):gsub('%.[mM][kKpP][vV4]$', ''):gsub('%.[mM]2[tT][sS]$', ''):gsub('%.[tT][sS]$', ''):gsub('%.[aA][vV][iI]$', ''):gsub('%.[wW][eE][bB][mM]$', '')
    label(a, 96, 62, 7, 46, TEXT, title, true)
    local ch = list[n]
    if ch and ch.title and ch.title ~= '' then label(a, 96, 124, 7, 26, DIM, ch.title) end
    local tm = os.date('*t')
    label(a, W - 96, 60, 9, 44, TEXT, string.format('%02d:%02d', tm.hour, tm.min))
    if known then
        local speed = mp.get_property_number('speed') or 1
        local ends = os.date('*t', os.time() + math.floor((d - t) / math.max(0.1, speed)))
        label(a, W - 96, 116, 9, 24, DIM, T('endsAt', string.format('%02d:%02d', ends.hour, ends.min)))
    end

    -- the line above the bar: chapter, picture, sound, subtitles
    local meta = {}
    if #list > 1 and n > 0 then meta[#meta + 1] = T('chapterOf', n, #list) end
    if lumen.video and lumen.video ~= '' then meta[#meta + 1] = lumen.video end
    local al = audioLabel()
    if al and al ~= '' then meta[#meta + 1] = al end
    meta[#meta + 1] = T('subs') .. ': ' .. subsLabel()
    label(a, 96, H - 268, 1, 24, DIM, table.concat(meta, '    ·    '))

    -- the time bar
    barX1, barX2 = 96, W - 96
    local bw = barX2 - barX1
    local barHot = hover == 'bar' or dragging
    local by, bh = H - 226, barHot and 12 or 8
    local function px(x) return barX1 + bw * math.max(0, math.min(1, known and x / d or 0)) end
    box(a, barX1, by - bh / 2, barX2, by + bh / 2, '#ffffff', 0xC4, bh / 2)
    local buf = bufferedEnd()
    if known and buf > t then box(a, px(t), by - bh / 2, px(buf), by + bh / 2, '#ffffff', 0xA8, bh / 2) end
    if known and t > 0 then box(a, barX1, by - bh / 2, math.max(barX1 + bh, px(t)), by + bh / 2, ACCENT, 0, bh / 2) end
    if known then
        for i, c in ipairs(list) do
            if i > 1 and (c.time or 0) > 0.5 then box(a, px(c.time) - 2, by - bh / 2 - 1, px(c.time) + 2, by + bh / 2 + 1, '#05060a', 0x10) end
        end
    end
    elements[#elements + 1] = { 'bar', barX1 - 10, by - 22, barX2 + 10, by + 22 }
    local at = t
    if barHot and known and mouse.x >= 0 then at = math.max(0, math.min(1, (mouse.x - barX1) / bw)) * d end
    if known and (barHot or now() < flashUntil) then
        local kx = px(at)
        box(a, kx - 14, by - 14, kx + 14, by + 14, '#ffffff', 0, 14)
        -- the bubble: the time there, and its chapter
        local cn = chapterAt(at, list)
        local sub = ''
        if #list > 1 and cn > 0 then sub = (list[cn].title and list[cn].title ~= '') and list[cn].title or T('chapter', cn) end
        local wbox = math.max(110, ulen(clock(at)) * 21 + 40, ulen(sub) * 12 + 40)
        local hbox = sub ~= '' and 78 or 54
        local bx = math.max(barX1, math.min(barX2 - wbox, kx - wbox / 2))
        box(a, bx, by - 34 - hbox, bx + wbox, by - 34, '#0e101a', 0x10, 14)
        label(a, bx + wbox / 2, by - 34 - hbox + 8, 8, 34, TEXT, clock(at), true)
        if sub ~= '' then label(a, bx + wbox / 2, by - 34 - 32, 8, 20, ACCENT2, sub) end
    end
    -- the times under the bar
    label(a, barX1, H - 172, 1, 26, TEXT, clock(at))
    if known then label(a, barX2, H - 172, 3, 26, DIM, '−' .. clock(math.max(0, d - at))) end

    -- the buttons: in the middle the film, on the left its tracks, on the right the window
    local cy = H - 104
    local row = {}
    local hasCh = #list > 1
    if hasCh then row[#row + 1] = 'chprev' end
    row[#row + 1] = 'back10'
    row[#row + 1] = 'play'
    row[#row + 1] = 'fwd10'
    if hasCh then row[#row + 1] = 'chnext' end
    local size, big, gap = 72, 92, 24
    local total = 0
    for _, b in ipairs(row) do total = total + (b == 'play' and big or size) + gap end
    local x = W / 2 - (total - gap) / 2
    local paused = mp.get_property_bool('pause')
    for _, b in ipairs(row) do
        local s = b == 'play' and big or size
        button(a, b, b == 'play' and (paused and 'play' or 'pause') or b, x + s / 2, cy, s, b == 'play')
        x = x + s + gap
    end
    local lx = 96 + size / 2
    button(a, 'audio', 'audio', lx, cy, size); lx = lx + size + gap
    button(a, 'subs', 'subs', lx, cy, size)
    if not lumen.kiosk then button(a, 'fullscreen', 'fullscreen', W - 96 - size / 2, cy, size) end
    -- the name of the button under the mouse
    local names = { play = paused and T('play') or T('pause'), back10 = T('back10'), fwd10 = T('fwd10'), chprev = T('chprev'),
                    chnext = T('chnext'), audio = T('audio'), subs = T('subs'), fullscreen = T('fullscreen') }
    if hover and names[hover] then label(a, W / 2, H - 22, 2, 23, TEXT, names[hover]) end

    -- the track list of the audio or subtitle button
    if panel then
        local rows = {}
        local src = panel == 'audio' and (lumen.audio or {}) or (lumen.subs or {})
        if panel == 'subs' then rows[#rows + 1] = { id = 0, label = T('off'), selected = selected(src) == nil } end
        for _, tr in ipairs(src) do rows[#rows + 1] = tr end
        local pw, rh = 620, 54
        local ph = 70 + #rows * rh + 16
        local px1 = panel == 'audio' and 96 or 96 + size + gap
        local py2 = cy - size / 2 - 26
        local py1 = math.max(60, py2 - ph)
        box(a, px1, py1, px1 + pw, py2, '#0e101a', 0x0C, 22)
        label(a, px1 + 30, py1 + 22, 7, 21, ACCENT2, (panel == 'audio' and T('audio') or T('subs')):upper(), true)
        elements[#elements + 1] = { 'panel', px1, py1, px1 + pw, py2 }      -- (under its rows: the last found first)
        local y = py1 + 64
        for _, r in ipairs(rows) do
            if y + rh > py2 then break end
            local name = 'track:' .. panel .. ':' .. tostring(r.id)
            if hover == name then box(a, px1 + 12, y, px1 + pw - 12, y + rh - 4, ACCENT, 0, 14) end
            if r.selected then shape(a, '#ffffff', 0, ICONS.check, px1 + 40, y + rh / 2 - 2, 26) end
            label(a, px1 + 70, y + rh / 2 - 2, 4, 25, TEXT, r.label)
            elements[#elements + 1] = { name, px1 + 12, y, px1 + pw - 12, y + rh - 4 }
            y = y + rh
        end
    end

    ov.data = a.text
    ov:update()
end

-- ---- showing and hiding ----
local function setInput(on)
    if on == inputOn then return end
    inputOn = on
    if on then mp.enable_key_bindings('lumen-osc-input') else mp.disable_key_bindings('lumen-osc-input') end
end
local function show(seconds)
    if idle or lumen.menu then return end
    visible = true
    hideAt = math.max(hideAt, now() + (seconds or HIDE))
end
local function hide()
    visible, panel, hover, dragging = false, nil, nil, false
    setInput(false)
    render()
end
local function hit(x, y)
    for i = #elements, 1, -1 do
        local e = elements[i]
        if x >= e[2] and x <= e[4] and y >= e[3] and y <= e[5] then return e[1] end
    end
end
local function updateHover()
    local h = visible and mouse.inside and hit(mouse.x, mouse.y) or nil
    if h ~= hover then hover = h; render() end
    setInput(visible and (hover ~= nil or panel ~= nil or dragging))
end

local function seekAtMouse(final)
    local d = dur()
    if not d or d <= 0 then return end
    local t = math.max(0, math.min(1, (mouse.x - barX1) / (barX2 - barX1))) * d
    if final or now() - lastDragSeek > 0.12 then
        lastDragSeek = now()
        act('seek', string.format('%.2f', t))
    end
end

local function activate(name)
    if name == 'play' then mp.command('cycle pause')
    elseif name == 'back10' then act('skip', -10)
    elseif name == 'fwd10' then act('skip', 10)
    elseif name == 'chprev' then act('chapter', -1)
    elseif name == 'chnext' then act('chapter', 1)
    elseif name == 'fullscreen' then act('fullscreen')
    elseif name == 'audio' or name == 'subs' then panel = panel ~= name and name or nil
    elseif name and name:sub(1, 6) == 'track:' then
        local kind, id = name:match('^track:(%a+):(%-?%d+)$')
        act(kind == 'audio' and 'aid' or 'sid', id)
        panel = nil
    end
    show(HIDE)
    render()
    updateHover()
end

mp.set_key_bindings({
    { 'mbtn_left',
      function()                    -- up (or a click as one event, without a down before: some inputs send it so)
          if dragging then dragging = false; seekAtMouse(true); render(); updateHover(); pressed = false; return end
          local h = hover
          if pressed == false or pressed == h then
              if h == 'bar' then flashUntil = now() + 1; seekAtMouse(true); render()
              elseif h then activate(h) end
          end
          if panel and h ~= 'panel' and not (h or ''):find('^track:') and h ~= 'audio' and h ~= 'subs' then panel = nil; render(); updateHover() end
          pressed = false
      end,
      function()                    -- down
          pressed = hover or ''
          if hover == 'bar' then dragging = true; flashUntil = now() + 1; seekAtMouse(true); render() end
      end },
    { 'mbtn_left_dbl', function() end },
}, 'lumen-osc-input', 'force')

mp.observe_property('mouse-pos', 'native', function(_, m)
    if not m then return end
    local ow, oh = mp.get_osd_size()
    if not ow or ow <= 0 or oh <= 0 then return end
    local moved = math.abs(m.x * W / ow - mouse.x) > 0.5 or math.abs(m.y * H / oh - mouse.y) > 0.5
    mouse.x, mouse.y, mouse.inside = m.x * W / ow, m.y * H / oh, m.hover
    if not moved then return end
    if mouse.inside and not lumen.menu and not idle then
        local was = visible
        show(HIDE)
        if not was then render() end
    end
    if dragging then seekAtMouse(false); render() end
    updateHover()
end)

mp.add_periodic_timer(0.25, function()
    if not visible then return end
    if lumen.menu or idle then hide(); return end
    local paused = mp.get_property_bool('pause')
    if not paused and not dragging and not panel and not hover and now() > hideAt then hide(); return end
    render()
end)

-- a key (arrows, OK, the remote of LumenOS): the controls show what happens
mp.register_script_message('lumen-key', function() show(HIDE); render() end)
mp.register_event('seek', function()
    if idle or lumen.menu then return end
    show(2); flashUntil = now() + 1.4; render()
end)
mp.observe_property('pause', 'bool', function(_, p)
    if p then show(HIDE) else hideAt = now() + 1.2 end
    render()
end)
mp.observe_property('chapter', 'number', function()
    if visible then flashUntil = now() + 1.4; render() end
end)
mp.observe_property('idle-active', 'bool', function(_, v) idle = v ~= false; if idle then hide() end end)
mp.observe_property('osd-dimensions', 'native', function() render() end)
-- (read as "native": the string itself; as "string" mpv hands it over quoted)
local function json(v) return type(v) == 'string' and utils.parse_json(v) or nil end
mp.observe_property('user-data/lumen/osc', 'native', function(_, v)
    local was = lumen.menu
    lumen = json(v) or {}
    if lumen.menu and visible then hide() elseif visible then render() end
    if was and not lumen.menu then hideAt = 0 end
end)
mp.observe_property('user-data/lumen/osc-text', 'native', function(_, v)
    text = json(v) or {}
end)

-- the controls show the jumps and chapters themselves: mpv's own bar only for the volume
mp.set_property('osd-on-seek', 'no')

-- for pictures of the controls (tests, the README): LUMEN_OSC_PIN=1 keeps them up; =bar:0.4 as with the mouse over the
-- time bar at 40 %; =audio / =subs with that track list open; =<button> with the mouse over it
local pin = os.getenv('LUMEN_OSC_PIN')
if pin and pin ~= '' then
    HIDE = 1e9
    mp.add_periodic_timer(0.25, function()
        if idle then return end
        if not visible then show(HIDE) end
        local what, at = pin:match('^(%a+):?([%d%.]*)$')
        if what == 'bar' then mouse.inside = true; mouse.x = barX1 + (barX2 - barX1) * (tonumber(at) or 0.5); mouse.y = H - 226; hover = 'bar'
        elseif what == 'audio' or what == 'subs' then panel = what
        elseif what and what ~= '1' then hover = what end
        render()
    end)
end
