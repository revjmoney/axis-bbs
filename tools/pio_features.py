# PlatformIO extra script: build switches from the environment.
#   AXIS_OFF=lord,scripts pio run -e cyd     switch those features off
#   AXIS_PRESET=no-doors pio run -e cyd      switch a preset's list off
# With neither set this does nothing. A bad name or a broken dependency stops the
# build with the reason; it never builds something else.
Import("env")

import os
import sys

tools = os.path.join(env.subst("$PROJECT_DIR"), "tools")
if tools not in sys.path:
    sys.path.insert(0, tools)
import buildopts

off_text = os.environ.get("AXIS_OFF", "").strip()
preset_text = os.environ.get("AXIS_PRESET", "").strip()

if off_text or preset_text:
    try:
        features, presets = buildopts.load(env.subst("$PROJECT_DIR"))
        if off_text and preset_text:
            raise ValueError("set AXIS_OFF or AXIS_PRESET, not both")
        if preset_text:
            match = [p for p in presets if p["id"] == preset_text]
            if not match:
                raise ValueError("unknown preset: %r" % preset_text)
            off = set(match[0]["off"])
        else:
            off = {s.strip() for s in off_text.split(",") if s.strip()}
        off = buildopts.resolve(off, features)
        flags = buildopts.build_flags(off, features)
    except ValueError as e:
        sys.stderr.write("AXIS build switches refused: %s\n" % e)
        env.Exit(1)
    else:
        print("AXIS build switches: %s" % (" ".join(flags) or "none"))
        env.Append(CCFLAGS=flags)
