#include "core.h"
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

int grid_move(int current, int dx, int dy, int columns, int count) {
    if (count <= 0 || columns <= 0)
        return -1;
    if (current < 0)
        return 0;
    int row = current / columns, col = current % columns;
    if (dx) {
        int next = current + dx;
        if (next < 0 || next >= count || next / columns != row)
            return current;
        return next;
    }
    int target = (row + dy) * columns + col;
    if (target < 0)
        return current;
    if (target >= count) {
        if ((row + dy) * columns < count)
            return count - 1;
        return current;
    }
    return target;
}

FrameRect fit_frame(int sw, int sh, int width, int height) {
    FrameRect r = {0, 0, width, height};
    if (sw <= 0 || sh <= 0 || width <= 0 || height <= 0)
        return r;
    if ((int64_t)sw * height > (int64_t)sh * width)
        r.h = (int)((int64_t)width * sh / sw);
    else
        r.w = (int)((int64_t)height * sw / sh);
    r.x = (width - r.w) / 2;
    r.y = (height - r.h) / 2;
    return r;
}
