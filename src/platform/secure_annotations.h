#pragma once

// Compatibility annotations for legacy modules that survived the retirement of
// the standalone security framework.  They preserve the intended inlining
// boundary without reintroducing the removed anti-tamper dependency.
#ifndef OMNIGHOST_SECURE_FUNC
#if defined(_MSC_VER)
#define OMNIGHOST_SECURE_FUNC __declspec(noinline)
#define OMNIGHOST_SECURE_INLINE __forceinline
#else
#define OMNIGHOST_SECURE_FUNC
#define OMNIGHOST_SECURE_INLINE inline
#endif
#endif
