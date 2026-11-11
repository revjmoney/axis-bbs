**What this changes and why**

**Tested on**
- [ ] host tests pass (`cmake --build build && ./build/axis_tests`)
- [ ] firmware builds (`pio run -e cyd`)
- [ ] real hardware (which board?)

**Flash and RAM** (flash % from `pio run -e cyd`; free heap from `@STATUS`, if it matters)

**Checklist**
- [ ] No secrets in the change (WiFi passwords, cluster keys, tokens, `.env` files, real user data)
- [ ] No AI attribution lines in the commits (no `Co-Authored-By` for an AI tool, no "Generated with")
- [ ] My work can be released under the project's GPL-3.0 licence.
