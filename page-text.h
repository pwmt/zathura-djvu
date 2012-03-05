/* See LICENSE file for license and copyright information */

#ifndef DJVU_PAGE_H
#define DJVU_PAGE_H

#include <girara/datastructures.h>
#include <girara/macros.h>
#include <zathura/document.h>

#include "djvu.h"

/**
 * DjVu page text
 */
typedef struct djvu_page_text_s {
  miniexp_t text_information; /**< Text by ddjvu_document_get_pagetext */
  char* content; /**< Actual content */

  miniexp_t begin; /**< Begin index */
  miniexp_t end; /**< End index */
  girara_list_t* text_positions; /**< Position/Expression duple */
  zathura_rectangle_t* rectangle; /**< Rectangle */

  djvu_document_t* document; /**< Correspondening document */
  zathura_page_t* page; /**< Correspondening page */
} djvu_page_text_t;

/**
 * Creates a new djvu page object
 *
 * @param document The document
 * @param page_number The number of the page
 * @return The page object or NULL if an error occured
 */
GIRARA_HIDDEN djvu_page_text_t* djvu_page_text_new(djvu_document_t* document,
    zathura_page_t* page);

/**
 * Frees a djvu page object
 *
 * @param page The page to be freed
 */
GIRARA_HIDDEN void djvu_page_text_free(djvu_page_text_t* page_text);

/**
 * Searchs the page for a specific key word and returns a list of results
 *
 * @param page_text The djvu page text object
 * @param text The text to search
 * @return List of results or NULL if an error occured
 */
GIRARA_HIDDEN girara_list_t* djvu_page_text_search(djvu_page_text_t* page_text,
    const char* text);

/**
 * Returns the text on the page under the given rectangle
 *
 * @param page_text The djvu page text object
 * @param rectangle The area of where the text should be copied
 * @return Copy of the text or NULL if an error occured or if the area is empty
 */
GIRARA_HIDDEN char* djvu_page_text_select(djvu_page_text_t* page_text,
    zathura_rectangle_t rectangle);

#endif // DJVU_PAGE_H
