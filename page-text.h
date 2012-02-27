/* See LICENSE file for license and copyright information */

#ifndef DJVU_PAGE_H
#define DJVU_PAGE_H

#include "djvu.h"

/**
 * DjVu page text
 */
typedef struct djvu_page_text_s {
  miniexp_t text_information; /**< Text by ddjvu_document_get_pagetext */
  char* content; /**< Actual content */
  miniexp_t begin; /**< Begin index */
  miniexp_t end; /**< End index */
  djvu_document_t* document; /**< Correspondening document */
} djvu_page_text_t;

/**
 * Creates a new djvu page object
 *
 * @param document The document
 * @param page_number The number of the page
 * @return The page object or NULL if an error occured
 */
djvu_page_text_t* djvu_page_text_new(djvu_document_t* document, unsigned int page_number);

/**
 * Frees a djvu page object
 *
 * @param page The page to be freed
 */
void djvu_page_text_free(djvu_page_text_t* page_text);

/**
 * Searchs the page for a specific key word and returns a list of results
 *
 * @param page_text The djvu page text object
 * @param text The text to search
 * @return List of results or NULL if an error occured
 */
girara_list_t* djvu_page_text_search(djvu_page_text_t* page_text, const char* text);

#endif // DJVU_PAGE_H
