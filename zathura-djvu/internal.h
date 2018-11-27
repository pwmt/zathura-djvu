/* SPDX-License-Identifier: Zlib */

#ifndef DJVU_INTERNAL_H
#define DJVU_INTERNAL_H

#include <girara/macros.h>

#define ZATHURA_DJVU_SCALE 0.2

GIRARA_HIDDEN void handle_messages(djvu_document_t* document, bool wait);

#endif // DJVU_INTERNAL_H
