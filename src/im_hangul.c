/*
 * Kmscon - Hangul (Korean) Input Method Backend
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
 * Hangul input backend using libhangul.
 *
 * Key processing model:
 *   - Modifier keys (Shift_L … Hyper_R) are silently ignored so that
 *     pressing Shift before a consonant (e.g. Shift+T → ㅆ) does not
 *     accidentally flush the current composition.
 *   - BackSpace is handled by hangul_ic_backspace(); if the IC is empty the
 *     key is passed through to the VTE so the shell can act on it.
 *   - Any key with a non-Shift modifier (Ctrl, Alt, …) or outside the ASCII
 *     printable range forces a flush of pending composition and then falls
 *     through to the VTE.
 *   - All other keys are fed to hangul_ic_process(); whatever the library
 *     commits is forwarded to the PTY and the new preedit char is stored
 *     for overlay rendering.
 *
 * params (--im-params): libhangul keyboard id, e.g. "2" (dubeolsik, default),
 *   "32", "3f", "3s", "ro", "ahn", …  (see hangul_ic_new() documentation).
 */

#include <errno.h>
#include <stdlib.h>
#include <string.h>

#include <hangul.h>
#include <xkbcommon/xkbcommon-keysyms.h>

#include "kmscon_im.h"
#include "shl/log.h"

#define LOG_SUBSYSTEM "im_hangul"

/* Maximum UCS4 characters that libhangul can ever commit in one keystroke.
 * In practice it is always ≤ 2 (one completed syllable + one newly started),
 * but we keep a generous buffer to be safe. */
#define COMMIT_BUF_SIZE 16

struct hangul_ctx {
	HangulInputContext *hic;
	uint32_t commit_buf[COMMIT_BUF_SIZE];
};

/* ------------------------------------------------------------------ */
/* Helpers                                                              */
/* ------------------------------------------------------------------ */

/*
 * Copy a libhangul UCS4 string (ucschar = uint32_t) into the commit buffer
 * and return the pointer, or NULL if nothing was committed.
 */
static const uint32_t *capture_commit(struct hangul_ctx *ctx, const ucschar *src)
{
	size_t i;

	if (!src || !src[0]) {
		ctx->commit_buf[0] = 0;
		return NULL;
	}

	for (i = 0; i < COMMIT_BUF_SIZE - 1 && src[i]; i++)
		ctx->commit_buf[i] = (uint32_t)src[i];
	ctx->commit_buf[i] = 0;

	return ctx->commit_buf;
}

/* Return the current preedit character (0 if none). */
static uint32_t current_preedit(struct hangul_ctx *ctx)
{
	const ucschar *pre = hangul_ic_get_preedit_string(ctx->hic);

	return (pre && pre[0]) ? (uint32_t)pre[0] : 0;
}

/*
 * Return true if this keysym+mods combination should bypass IM processing
 * and be forwarded directly to the VTE.
 *
 * Rules:
 *   - Any non-Shift modifier active → passthrough (Ctrl+C, Alt+x, etc.)
 *   - No keysym available           → passthrough
 *   - BackSpace                     → NOT passthrough (handled specially)
 *   - ASCII printable (0x20–0x7e)   → NOT passthrough (Korean jamo input)
 *   - Everything else               → passthrough (arrows, F-keys, …)
 */
static bool should_passthrough(uint32_t keysym, unsigned int mods)
{
	if (mods & ~KMSCON_IM_MOD_SHIFT)
		return true;
	if (keysym == XKB_KEY_BackSpace)
		return false;
	if (keysym >= 0x20 && keysym <= 0x7e)
		return false;
	return true;
}

/* ------------------------------------------------------------------ */
/* kmscon_im_ops implementation                                         */
/* ------------------------------------------------------------------ */

static int im_hangul_init(struct kmscon_im *im, const char *params)
{
	struct hangul_ctx *ctx;
	const char *keyboard;

	ctx = calloc(1, sizeof(*ctx));
	if (!ctx)
		return -ENOMEM;

	/* params carries the keyboard layout id; fall back to 2-beol. */
	keyboard = (params && *params) ? params : "2";

	ctx->hic = hangul_ic_new(keyboard);
	if (!ctx->hic) {
		log_error("hangul_ic_new(\"%s\") failed", keyboard);
		free(ctx);
		return -ENOMEM;
	}

	log_info("hangul input context created (keyboard: %s)", keyboard);
	im->data = ctx;
	return 0;
}

static void im_hangul_destroy(struct kmscon_im *im)
{
	struct hangul_ctx *ctx = im->data;

	hangul_ic_delete(ctx->hic);
	free(ctx);
}

static bool im_hangul_process_key(struct kmscon_im *im, uint32_t keysym, unsigned int mods,
				  struct kmscon_im_result *out)
{
	struct hangul_ctx *ctx = im->data;
	bool consumed;

	out->commit = NULL;
	out->preedit = 0;

	/*
	 * Silently ignore modifier-only keys (Shift_L … Hyper_R).
	 * Without this, pressing Shift before a consonant would appear as an
	 * unrecognised keysym to should_passthrough() and flush the IC.
	 */
	if (keysym >= XKB_KEY_Shift_L && keysym <= XKB_KEY_Hyper_R)
		return false;

	/* BackSpace: try to un-compose one jamo inside the IC. */
	if (keysym == XKB_KEY_BackSpace) {
		if (!hangul_ic_is_empty(ctx->hic) && hangul_ic_backspace(ctx->hic)) {
			out->preedit = current_preedit(ctx);
			return true; /* consumed */
		}
		/* IC was already empty — let VTE handle the backspace. */
		return false;
	}

	/*
	 * Keys that should bypass IM: flush any pending syllable first so the
	 * composed character is committed before the special key takes effect.
	 */
	if (should_passthrough(keysym, mods)) {
		if (!hangul_ic_is_empty(ctx->hic)) {
			const ucschar *fc = hangul_ic_flush(ctx->hic);

			out->commit = capture_commit(ctx, fc);
			out->preedit = 0;
		}
		return false; /* not consumed — VTE still gets the key */
	}

	/* Feed the keysym to libhangul as an ASCII value. */
	consumed = hangul_ic_process(ctx->hic, (int)keysym);

	out->commit = capture_commit(ctx, hangul_ic_get_commit_string(ctx->hic));
	out->preedit = current_preedit(ctx);

	return consumed;
}

static void im_hangul_flush(struct kmscon_im *im, struct kmscon_im_result *out)
{
	struct hangul_ctx *ctx = im->data;
	const ucschar *fc;

	out->commit = NULL;
	out->preedit = 0;

	if (hangul_ic_is_empty(ctx->hic))
		return;

	fc = hangul_ic_flush(ctx->hic);
	out->commit = capture_commit(ctx, fc);
}

static bool im_hangul_is_empty(const struct kmscon_im *im)
{
	const struct hangul_ctx *ctx = im->data;

	return hangul_ic_is_empty(ctx->hic);
}

/* ------------------------------------------------------------------ */
/* Exported ops table                                                   */
/* ------------------------------------------------------------------ */

const struct kmscon_im_ops im_hangul_ops = {
	.name = "hangul",
	.init = im_hangul_init,
	.destroy = im_hangul_destroy,
	.process_key = im_hangul_process_key,
	.flush = im_hangul_flush,
	.is_empty = im_hangul_is_empty,
};
