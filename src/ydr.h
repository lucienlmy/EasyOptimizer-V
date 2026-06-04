#ifndef EO_YDR_H
#define EO_YDR_H

#include "types.h"

/* Load embedded textures from YDR/YFT/YDD files. The original RSC7 payload is
 * retained (ArchiveFile.model_meta) so the model can be recomposed on save. */
YtdFile *ydr_load(const wchar_t *filepath);

/* Recompose the original YDR/YFT/YDD with the (possibly edited) textures written
 * back into its embedded texture dictionary. Returns true on success. */
bool ydr_save(YtdFile *archive, const wchar_t *filepath);

/* Free the retained model payload. */
void ydr_free_model_meta(YtdFile *archive);

#endif
