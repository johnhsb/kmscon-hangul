/*
 * Kmscon - Input Method Abstraction Layer
 *
 * Copyright (c) 2024 kmscon contributors
 *
 * Permission is hereby granted, free of charge, to any person obtaining
 * a copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sublicense, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice shall be included
 * in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
 * OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
 * IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY
 * CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
 * TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
 * SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

/*
 * Input Method Abstraction Layer
 *
 * Provides a backend-independent interface for in-process character
 * composition.  Backends plug in via kmscon_im_ops and are selected at
 * runtime by engine name.
 *
 * Phase 1 (implemented): Korean via libhangul — single-char preedit,
 *   no candidate list.
 *
 * Phase 2 (extension points marked below): Chinese (librime) and
 *   Japanese (libskk/anthy) — multi-char preedit string, candidate list.
 *   To extend: widen kmscon_im_result and add a candidate-UI render path
 *   in kmscon_terminal.c without touching any backend.
 */

#ifndef KMSCON_IM_H
#define KMSCON_IM_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

struct kmscon_im;

/*
 * Modifier flags passed to kmscon_im_process_key().
 * Bit positions intentionally mirror input_modifier in input/input.h so
 * ev->mods can be forwarded verbatim.
 */
#define KMSCON_IM_MOD_SHIFT (1u << 0)
#define KMSCON_IM_MOD_LOCK (1u << 1)
#define KMSCON_IM_MOD_CTRL (1u << 2)
#define KMSCON_IM_MOD_ALT (1u << 3)
#define KMSCON_IM_MOD_LOGO (1u << 4)

/*
 * Result of a single IM operation.
 *
 * Fields are owned by the kmscon_im object and remain valid until the next
 * call into the same object.
 *
 * Phase 2 extension: replace preedit with a NULL-terminated UCS4 string
 * (multi-char romanisation for Pinyin/Kana) and add a candidate list here.
 * The terminal render path in kmscon_terminal.c already isolates preedit
 * drawing in draw_im_preedit() — that function is the sole change point.
 */
struct kmscon_im_result {
	/* NULL-terminated UCS4 string to write to the PTY, or NULL */
	const uint32_t *commit;

	/*
	 * UCS4 character drawn at the cursor position to visualise the
	 * composition in progress (underlined).  0 means nothing to show.
	 *
	 * Phase 2: extend to const uint32_t *preedit_str for multi-char
	 * preedit (Pinyin romanisation, Kana string, …).
	 */
	uint32_t preedit;
};

/*
 * Backend operations table.
 *
 * Each engine provides one statically-initialised instance of this struct
 * and registers it via the backends[] table in kmscon_im.c.
 *
 * Convention: all ops receive the engine-private pointer via im->data.
 */
struct kmscon_im_ops {
	/* Engine identifier used with --im-engine=<name> */
	const char *name;

	/*
	 * Allocate and initialise engine state.  On success, store a pointer
	 * to the engine context in im->data and return 0.
	 * params is engine-specific (e.g. keyboard layout id for hangul).
	 */
	int (*init)(struct kmscon_im *im, const char *params);

	/* Release all engine resources stored in im->data. */
	void (*destroy)(struct kmscon_im *im);

	/*
	 * Feed one key press to the engine.
	 *
	 * keysym : XKB keysym
	 * mods   : active modifiers (KMSCON_IM_MOD_* bitmask)
	 * out    : filled with any commit/preedit arising from this key
	 *
	 * Returns true if the key was consumed by the IM and must NOT be
	 * forwarded to tsm_vte_handle_keyboard().
	 */
	bool (*process_key)(struct kmscon_im *im, uint32_t keysym, unsigned int mods,
			    struct kmscon_im_result *out);

	/*
	 * Force-complete any pending composition (e.g. on IM-mode exit or
	 * session deactivation).  out->commit carries the flushed text;
	 * out->preedit is always 0 after a flush.
	 */
	void (*flush)(struct kmscon_im *im, struct kmscon_im_result *out);

	/* True when there is no character being composed. */
	bool (*is_empty)(const struct kmscon_im *im);
};

/* Opaque IM context; allocated by kmscon_im_new(). */
struct kmscon_im {
	const struct kmscon_im_ops *ops;
	void *data; /* engine-private context */
};

/* ---- Public API (used exclusively by kmscon_terminal.c) ---- */

/*
 * Create an IM context for the named engine.
 * Returns 0 on success, -ENOENT if the engine is unknown or not compiled in,
 * -ENOMEM on allocation failure.
 */
int kmscon_im_new(struct kmscon_im **out, const char *engine, const char *params);

void kmscon_im_destroy(struct kmscon_im *im);

bool kmscon_im_process_key(struct kmscon_im *im, uint32_t keysym, unsigned int mods,
			   struct kmscon_im_result *out);

void kmscon_im_flush(struct kmscon_im *im, struct kmscon_im_result *out);

bool kmscon_im_is_empty(const struct kmscon_im *im);

#endif /* KMSCON_IM_H */
