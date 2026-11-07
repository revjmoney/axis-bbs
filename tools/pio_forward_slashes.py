# PlatformIO on Windows expands ${platformio.packages_dir} with backslashes.
# The ESP-IDF builder then treats those as escapes, so the LittleFS include
# and library paths never resolve. Forward slashes survive. Linux is unchanged.
Import("env")

flags = env.get("BUILD_FLAGS", [])
fixed = [f.replace("\\", "/") if isinstance(f, str) else f for f in flags]
env.Replace(BUILD_FLAGS=fixed)
