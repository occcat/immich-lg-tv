#include "ui/ui-internal.h"
#include "media.h"
#include "ui-paint.h"
#include <SDL_image.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

ImageSlot *ui_image_get(App *a, const char *id, bool preview) {
    if (!valid_asset_id(id))
        return NULL;
    ImageSlot *slot = NULL;
    uint32_t oldest = UINT32_MAX;
    for (int i = 0; i < IMAGE_CACHE; i++) {
        ImageSlot *s = &a->images[i];
        if (!strcmp(s->id, id) && s->preview == preview) {
            s->used = a->frames;
            return s;
        }
        if (!*s->id) {
            slot = s;
            oldest = 0;
        } else if (oldest && s->used < oldest && !s->pending) {
            slot = s;
            oldest = s->used;
        }
    }
    if (!slot)
        return NULL;
    if (slot->texture)
        SDL_DestroyTexture(slot->texture);
    memset(slot, 0, sizeof *slot);
    copy_text(slot->id, sizeof slot->id, id);
    slot->preview = preview;
    slot->used = a->frames;
    Job job = {
        .kind = JOB_IMAGE, .generation = a->generation, .config = a->config, .preview = preview};
    copy_text(job.id, sizeof job.id, id);
    slot->pending = worker_submit(&job, preview);
    return slot;
}

void ui_images_clear(App *a) {
    for (int i = 0; i < IMAGE_CACHE; i++) {
        if (a->images[i].texture)
            SDL_DestroyTexture(a->images[i].texture);
        memset(&a->images[i], 0, sizeof a->images[i]);
    }
}
