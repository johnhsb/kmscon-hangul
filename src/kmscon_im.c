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

#include <errno.h>
#include <stdlib.h>
#include <string.h>

#include "kmscon_im.h"
#include "shl/log.h"

#define LOG_SUBSYSTEM "im"

/* ------------------------------------------------------------------ */
/* Backend registry                                                     */
/*                                                                      */
/* To add a new engine:                                                 */
/*   1. Implement kmscon_im_ops in a new im_<name>.c file.             */
/*   2. Add a BUILD_ENABLE_IM_<NAME> guarded extern + entry below.     */
/*   3. Add the meson option, dependency and source in meson files.     */
/* ------------------------------------------------------------------ */

#ifdef BUILD_ENABLE_IM_HANGUL
extern const struct kmscon_im_ops im_hangul_ops;
#endif

/*
 * Phase 2 extension points — uncomment and implement:
 *
 * #ifdef BUILD_ENABLE_IM_RIME
 * extern const struct kmscon_im_ops im_rime_ops;
 * #endif
 *
 * #ifdef BUILD_ENABLE_IM_SKK
 * extern const struct kmscon_im_ops im_skk_ops;
 * #endif
 */

static const struct kmscon_im_ops *backends[] = {
#ifdef BUILD_ENABLE_IM_HANGUL
	&im_hangul_ops,
#endif
	/* Phase 2: &im_rime_ops, &im_skk_ops */
	NULL,
};

/* ------------------------------------------------------------------ */
/* Public API                                                           */
/* ------------------------------------------------------------------ */

int kmscon_im_new(struct kmscon_im **out, const char *engine,
		  const char *params)
{
	const struct kmscon_im_ops *ops = NULL;
	struct kmscon_im *im;
	int ret;

	if (!out || !engine || !*engine)
		return -EINVAL;

	for (int i = 0; backends[i]; i++) {
		if (strcmp(backends[i]->name, engine) == 0) {
			ops = backends[i];
			break;
		}
	}

	if (!ops) {
		log_error("unknown IM engine '%s' (not compiled in?)", engine);
		return -ENOENT;
	}

	im = calloc(1, sizeof(*im));
	if (!im)
		return -ENOMEM;

	im->ops = ops;
	ret = ops->init(im, params ? params : "");
	if (ret) {
		log_error("IM engine '%s' init failed: %d", engine, ret);
		free(im);
		return ret;
	}

	log_debug("IM engine '%s' created", engine);
	*out = im;
	return 0;
}

void kmscon_im_destroy(struct kmscon_im *im)
{
	if (!im)
		return;
	log_debug("IM engine '%s' destroyed", im->ops->name);
	im->ops->destroy(im);
	free(im);
}

bool kmscon_im_process_key(struct kmscon_im *im, uint32_t keysym,
			   unsigned int mods, struct kmscon_im_result *out)
{
	if (!im || !out)
		return false;
	return im->ops->process_key(im, keysym, mods, out);
}

void kmscon_im_flush(struct kmscon_im *im, struct kmscon_im_result *out)
{
	if (!im || !out)
		return;
	im->ops->flush(im, out);
}

bool kmscon_im_is_empty(const struct kmscon_im *im)
{
	if (!im)
		return true;
	return im->ops->is_empty(im);
}
