# drm-kmod
The DRM drivers ported from Linux to FreeBSD using LinuxKPI

This is an old drm-kmod driver, patched to work for frame.work Intel 12th gen
notebooks. This work is based on dumbbell@'s work.

I was having screen freezes and random reboots with kernel messages

```
 drmn0: [drm] ERROR Fault errors on pipe A
 drmn0: [drm] ERROR Timed out waiting for DSB workload completion.
```

This appears to stick around on 15, unfortunately.
See https://github.com/freebsd/drm-kmod/issues/284

So far, this version has been working great on 13.3-RELEASE and I'm now working
on 14.2 quite nicely.

