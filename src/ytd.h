#ifndef EO_YTD_H
#define EO_YTD_H

#include "types.h"

YtdFile *ytd_load(const wchar_t *filepath);
bool     ytd_save(YtdFile *ytd, const wchar_t *filepath);
void     ytd_free(YtdFile *ytd);

#endif
