# drm-kmod
The DRM drivers ported from Linux to FreeBSD using LinuxKPI

This is work in progress. This merged drm-intel commit 830b3c68c1fb1e9176028d02ef86f3cf76aa2476 (drm-intel-fixes)
into drm-kmod, but focused only on i915.

I will probably take out amd et al code at a later point, if they no longer compile properly.

For the moment:
* i915 driver code is updated
* previous 5.12 patches were ported forward
* focus is on 14.0 SNAPSHOT; my work on backporting to 13.1 caused way too much pain, so I paused that.
  see freebsd13 branch if you want to know more details.
* everything is based on freebsd/drm-kmod and freedesktop/drm-intel
* I've flagged questionable or controversial code parts with FIXME BSD, intentionally differnet from BSDFIXME;
  this way, we can revisit ported issue and newly introduced issues separately

This is one attempt to give back to FreeBSD. You're invited to join in the effort, if you have time.

I'll save you the sermon on why I think it's the best OS choice, because if you're reading this, you're likely already converted anyways.

### FreeBSD source code
Folders `linuxkpi`

Code style and rules same as FreeBSD kernel.
No new code should be added there, all new linuxkpi functions should be
added in FreeBSD base.

(Note: this is a tough one; from what I can tell, there might be stuff we need to add to get this working. Never thought I'd be writing "kernel"
code any time soon. Yet, here we are...)
