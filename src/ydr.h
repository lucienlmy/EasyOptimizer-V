#ifndef EO_YDR_H
#define EO_YDR_H

#include "types.h"

/* Load embedded textures from YDR/YFT/YDD files (read-only, no save support) */
YtdFile *ydr_load(const wchar_t *filepath);

#endif
