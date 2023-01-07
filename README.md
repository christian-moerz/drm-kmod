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

Update 2023/01/07: dumbbell@ is working on drm-kmod as well; I'll attempt to merge my results into
his so we are all staying on the official track.

For the moment, I believe this is close but something is not yet working right in regards to double
buffering - at least, that's my interpretation. I probably won't be able to fix this on my own for
the moment. I've got a few possible avenues left, which I first want to discuss with others.

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

### Resources

I collected a bunch of links, I'd like to recommend reading up; they were pretty valuable to me during this
work. Unfortunately, git swallowed some of those original links, so this is an attempt at reproducing my
original list. I'll write up my curses towards git in an internal branch...

* https://bwidawsk.net/blog/2014/6/the-global-gtt-part-1/
* https://docs.freebsd.org/en/books/developers-handbook/kerneldebug/
* https://docs.freebsd.org/doc/3.1-RELEASE/usr/share/doc/zh/FAQ/FAQ245.html
* https://docs.kernel.org/core-api/xarray.html
* https://www.kernel.org/doc/gorman/html/understand/understand013.html
* https://docs.kernel.org/driver-api/firmware/request_firmware.html
* https://papers.freebsd.org/2008/BSDCan/baldwin-Introduction_to_Debugging_the_FreeBSD_Kernel.files/slides.pdf
* https://blog.ffwll.ch/2013/01/i915gem-crashcourse-overview.html
* https://lwn.net/Articles/263343/
