# drm-kmod
The DRM drivers ported from Linux to FreeBSD using LinuxKPI

This is work in progress. This merged drm-intel commit 830b3c68c1fb1e9176028d02ef86f3cf76aa2476 (drm-intel-fixes)
into drm-kmod, but focused only on i915.

amd, radeon and virtual gpus are all excluded from this build.

For the moment:
* i915 driver code is updated
* previous 5.12 patches were ported forward
* focus is on 14.0 SNAPSHOT; my work on backporting to 13.1 caused way too much pain, so I paused that.
  see freebsd13 branch if you want to know more details.
* everything is based on freebsd/drm-kmod and freedesktop/drm-intel
* I've flagged questionable or controversial code parts with FIXME BSD, intentionally differnet from BSDFIXME;
  this way, we can revisit ported issue and newly introduced issues separately
* there are some sections, I've flagged with FIXME LINUXKPI - particularly in some non-public branches
* it compiles, finally (as of December 27, 2022)
* it might work for your GPU, on my 12th gen Intel it unfortunately does not work completely yet;
  as of January 1, 2023 I seem to have identified the culprit: linuxkpi is handling xarrays with
  sleep mutexes, while linux' drm apparently expects spin mutexes to lock out interrupt handling.
  For the moment, the driver module can be loaded and unloaded but the screen appears to not work
  right yet. Need to check, whether this has to do with missing privacy screen functions?
* there is a lock reversal, identified by witness; might be originating from xarrays again,
  though it does involve the Giant lock (?)
* firmware needs to be loaded as a kernel module; I've got one working already and checked into a
  private branch. Since I'm not clear about the licensing terms of the binary blobs, I'll attempt
  to set up a sample Makefile with directions instead of directly putting them into the repo.
* backlight controls are confirmed working (use /usr/local/bin/backlight to adjust)

### Open/Known Issues

* framebuffer is not being set up correctly; the screen gets updated, if you switch to another tty
  and back; this tells me, there's still something broken with the basics
* the aforementioned witness lock reversion might be a false positive, because it's actually caused
  by vt(4).
* loading Xorg leads to a page fault, which confirms that there's more issues beneath the surface.
  I suspect it might have to do with xarray handling in linuxkpi, but it's only a hunch at the moment
* another supect part is the use of "atomic" instead of "local" page locks due to linuxkpi missing
  equivalent functionality

### Why I'm doing this

This is one attempt to give back to FreeBSD. You're invited to join in the effort, if you have time.

I'll save you the sermon on why I think it's the best OS choice, because if you're reading this, you're likely already converted anyways.

### FreeBSD source code
Folders `linuxkpi`

Code style and rules same as FreeBSD kernel.
No new code should be added there, all new linuxkpi functions should be
added in FreeBSD base.

(Note: this is a tough one; from what I can tell, there might be stuff we need to add to get this working. Never thought I'd be writing "kernel"
code any time soon. Yet, here we are...)
