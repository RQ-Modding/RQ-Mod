# RQ MOD changes to Lua

This folder is Lua 5.4.7, built into the engine (`src/meson.build`, `onelua.c`). It is Lua's own code
with the changes below. Each one is marked `RQ MOD` in the source. Carry them over when Lua is
updated, and run Lua's own test suite on the result.

## 1. A tail call into another chunk is a plain call (loader 2.24.1)

`lvm.c`, `OP_TAILCALL`. When the callee is a Lua function from another chunk (its `Proto`'s `source`
is not the caller's), the VM calls it as `OP_CALL` does, keeping every result, and goes on with the
`OP_RETURN` that the compiler always puts after `OP_TAILCALL`. That `OP_RETURN` returns the call's
results, closes the upvalues and fixes the varargs, as it does for `return x, f()`.

A tail call within one chunk stays a tail call. So does one into a C function, which never took the
caller's frame in Lua 5.4.

Why: the loader names the calling mod by the nearest stack frame that runs a file in a mod's folder
(`rq.mod.id`, the sandbox's paths, the `rq.log` tag). A tail call such as
`return rq.ui.confirm(...)` used to replace the mod's frame with the loader's Lua, so the call ran as
if outside any mod.

What it costs:

- A chain of tail calls that crosses chunks grows the stack, as plain recursion does. It stops with
  "stack overflow" at about 499,000 levels.
- `debug.traceback` shows those frames where it showed `(...tail calls...)`.

Lua's test suite (`lua-5.4.7-tests` from lua.org, every file, run with `_port=true`) passes on the
changed VM as on the unchanged one (2026-11-10). On Windows, `os.tmpname` names a file at the root
of the drive, so `verybig.lua` and `files.lua` need it pointed at a folder you can write to:
`lua -e "_port=true; local n=0; os.tmpname=function() n=n+1; return [[C:\some\dir\t]]..n end" files.lua`.
