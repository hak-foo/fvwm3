/* -*-c-*- */
/* This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

/****************************************************************************
 * This module was all original code
 * by Rob Nation
 * Copyright 1993, Robert Nation
 *     You may use this code for any purpose, as long as the original
 *     copyright remains in the source code and all documentation
 ****************************************************************************/

/* code for parsing the fvwm style command */

/* ---------------------------- included header files ----------------------- */

#include "config.h"
#include <stdio.h>

#include "libs/fvwmlib.h"
#include "libs/FScreen.h"
#include "fvwm.h"
#include "externs.h"
#include "cursor.h"
#include "functions.h"
#include "bindings.h"
#include "misc.h"
#include "screen.h"
#include "defaults.h"
#include "update.h"
#include "style.h"
#include "colorset.h"
#include "ewmh.h"
#include "gnome.h"
#include "icons.h"
#include "focus.h"
#include <libs/gravity.h>
#include "geometry.h"
#include "move_resize.h"
#include "add_window.h"
#include "borders.h"
#include "frame.h"

/* ---------------------------- local definitions --------------------------- */

#define SAFEFREE( p )  {if (p) {free(p);(p)=NULL;}}

/* ---------------------------- local macros -------------------------------- */

/* ---------------------------- imports ------------------------------------- */

/* ---------------------------- included code files ------------------------- */

/* ---------------------------- local types --------------------------------- */

/* ---------------------------- forward declarations ------------------------ */

/* ---------------------------- local variables ----------------------------- */

/* list of window names with attributes */
static window_style *all_styles = NULL;
static window_style *last_style_in_list = NULL;

/* ---------------------------- exported variables (globals) ---------------- */

/* ---------------------------- local functions ----------------------------- */

static Bool blockor(char *dest, char *blk1, char *blk2, int length)
{
	int i;
	char result = 0;

	for (i = 0; i < length; i++)
	{
		dest[i] = (blk1[i] | blk2[i]);
		result |= dest[i];
	}

	return (result) ? True : False;
}

static Bool blockand(char *dest, char *blk1, char *blk2, int length)
{
	int i;
	char result = (char)0xff;

	for (i = 0; i < length; i++)
	{
		dest[i] = (blk1[i] & blk2[i]);
		result |= dest[i];
	}

	return (result) ? True : False;
}

static Bool blockunmask(char *dest, char *blk1, char *blk2, int length)
{
	int i;
	char result = (char)0xff;

	for (i = 0; i < length; i++)
	{
		dest[i] = (blk1[i] & ~blk2[i]);
		result |= dest[i];
	}

	return (result) ? True : False;
}

static Bool blockissubset(char *sub, char *super, int length)
{
	int i;

	for (i = 0; i < length; i++)
	{
		if ((sub[i] & super[i]) != sub[i])
			return False;
	}

	return True;
}

static void remove_icon_boxes_from_style(window_style *pstyle)
{
	if (SHAS_ICON_BOXES(&pstyle->flags))
	{
		free_icon_boxes(SGET_ICON_BOXES(*pstyle));
		pstyle->flags.has_icon_boxes = 0;
		SSET_ICON_BOXES(*pstyle, NULL);
	}

	return;
}

static void copy_icon_boxes(icon_boxes **pdest, icon_boxes *src)
{
	icon_boxes *last = NULL;
	icon_boxes *temp;

	*pdest = NULL;
	/* copy the icon boxes */
	for ( ; src != NULL; src = src->next)
	{
		temp = (icon_boxes *)safemalloc(sizeof(icon_boxes));
		memcpy(temp, src, sizeof(icon_boxes));
		temp->next = NULL;
		if (last != NULL)
			last->next = temp;
		else
			*pdest = temp;
		last = temp;
	}
}

/* Check word after IconFill to see if its "Top,Bottom,Left,Right" */
static int Get_TBLR(char *token, unsigned char *IconFill)
{
	/* init */
	if (StrEquals(token, "B") ||
	    StrEquals(token, "BOT")||
	    StrEquals(token, "BOTTOM"))
	{
		/* turn on bottom and verical */
		*IconFill = ICONFILLBOT | ICONFILLHRZ;
	}
	else if (StrEquals(token, "T") ||
		 StrEquals(token, "TOP"))
	{
		/* turn on vertical */
		*IconFill = ICONFILLHRZ;
	}
	else if (StrEquals(token, "R") ||
		 StrEquals(token, "RGT") ||
		 StrEquals(token, "RIGHT"))
	{
		/* turn on right bit */
		*IconFill = ICONFILLRGT;
	}
	else if (StrEquals(token, "L") ||
		 StrEquals(token, "LFT") ||
		 StrEquals(token, "LEFT"))
	{
		*IconFill = 0;
	}
	else
	{
		/* anything else is bad */
		return 0;
	}

	/* return OK */
	return 1;
}

/* merge_styles - For a matching style, merge window_style to window_style
 *
 *  Returned Value:
 *      merged matching styles in callers window_style.
 *
 *  Inputs:
 *      merged_style - style resulting from the merge
 *      add_style    - the style to be added into the merge_style
 *      do_free_src_and_alloc_copy
 *                   - free allocated parts of merge_style that are replaced
 *                     from add_style.  Create a copy of of the replaced
 *                     styles in allocated memory.
 *
 *  Note:
 *      The only trick here is that on and off flags/buttons are
 *      combined into the on flag/button. */
static void merge_styles(
	window_style *merged_style, window_style *add_style,
	Bool do_free_src_and_alloc_copy)
{
	int i;
	char *merge_flags;
	char *add_flags;
	char *merge_mask;
	char *add_mask;
	char *merge_change_mask;
	char *add_change_mask;

	if (add_style->flag_mask.has_icon)
	{
		if (do_free_src_and_alloc_copy)
		{
			SAFEFREE(SGET_ICON_NAME(*merged_style));
			SSET_ICON_NAME(
				*merged_style, (SGET_ICON_NAME(*add_style)) ?
				safestrdup(SGET_ICON_NAME(*add_style)) : NULL);
		}
		else
		{
			SSET_ICON_NAME(
				*merged_style, SGET_ICON_NAME(*add_style));
		}
	}
	if (FMiniIconsSupported && add_style->flag_mask.has_mini_icon)
	{
		if (do_free_src_and_alloc_copy)
		{
			SAFEFREE(SGET_MINI_ICON_NAME(*merged_style));
			SSET_MINI_ICON_NAME(
				*merged_style,
				(SGET_MINI_ICON_NAME(*add_style)) ?
				safestrdup(SGET_MINI_ICON_NAME(*add_style)) :
				NULL);
		}
		else
		{
			SSET_MINI_ICON_NAME(
				*merged_style, SGET_MINI_ICON_NAME(*add_style));
		}
	}
#ifdef USEDECOR
	if (add_style->flag_mask.has_decor)
	{
		if (do_free_src_and_alloc_copy)
		{
			SAFEFREE(SGET_DECOR_NAME(*merged_style));
			SSET_DECOR_NAME(
				*merged_style, (SGET_DECOR_NAME(*add_style)) ?
				safestrdup(SGET_DECOR_NAME(*add_style)) : NULL);
		}
		else
		{
			SSET_DECOR_NAME(
				*merged_style, SGET_DECOR_NAME(*add_style));
		}
	}
#endif
	if (SFHAS_ICON_FONT(*add_style))
	{
		if (do_free_src_and_alloc_copy)
		{
			SAFEFREE(SGET_ICON_FONT(*merged_style));
			SSET_ICON_FONT(
				*merged_style, (SGET_ICON_FONT(*add_style)) ?
				safestrdup(SGET_ICON_FONT(*add_style)) : NULL);
		}
		else
		{
			SSET_ICON_FONT(
				*merged_style, SGET_ICON_FONT(*add_style));
		}
	}
	if (SFHAS_WINDOW_FONT(*add_style))
	{
		if (do_free_src_and_alloc_copy)
		{
			SAFEFREE(SGET_WINDOW_FONT(*merged_style));
			SSET_WINDOW_FONT(
				*merged_style, (SGET_WINDOW_FONT(*add_style)) ?
				safestrdup(SGET_WINDOW_FONT(*add_style)) :
				NULL);
		}
		else
		{
			SSET_WINDOW_FONT(
				*merged_style, SGET_WINDOW_FONT(*add_style));
		}
	}
	if (add_style->flags.use_start_on_desk)
	{
		SSET_START_DESK(*merged_style, SGET_START_DESK(*add_style));
		SSET_START_PAGE_X(*merged_style, SGET_START_PAGE_X(*add_style));
		SSET_START_PAGE_Y(*merged_style, SGET_START_PAGE_Y(*add_style));
	}
	if (add_style->flags.use_start_on_screen)
	{
		SSET_START_SCREEN(*merged_style, SGET_START_SCREEN(*add_style));
	}
	if (add_style->flag_mask.has_color_fore)
	{
		if (do_free_src_and_alloc_copy)
		{
			SAFEFREE(SGET_FORE_COLOR_NAME(*merged_style));
			SSET_FORE_COLOR_NAME(
				*merged_style,
				(SGET_FORE_COLOR_NAME(*add_style)) ?
				safestrdup(SGET_FORE_COLOR_NAME(*add_style)) :
				NULL);
		}
		else
		{
			SSET_FORE_COLOR_NAME(
				*merged_style,
				SGET_FORE_COLOR_NAME(*add_style));
		}
	}
	if (add_style->flag_mask.has_color_back)
	{
		if (do_free_src_and_alloc_copy)
		{
			SAFEFREE(SGET_BACK_COLOR_NAME(*merged_style));
			SSET_BACK_COLOR_NAME(
				*merged_style,
				(SGET_BACK_COLOR_NAME(*add_style)) ?
				safestrdup(SGET_BACK_COLOR_NAME(*add_style)) :
				NULL);
		}
		else
		{
			SSET_BACK_COLOR_NAME(
				*merged_style,
				SGET_BACK_COLOR_NAME(*add_style));
		}
	}
	if (add_style->flag_mask.has_color_fore_hi)
	{
		if (do_free_src_and_alloc_copy)
		{
			SAFEFREE(SGET_FORE_COLOR_NAME_HI(*merged_style));
			SSET_FORE_COLOR_NAME_HI(
				*merged_style,
				(SGET_FORE_COLOR_NAME_HI(*add_style)) ?
				safestrdup(
					SGET_FORE_COLOR_NAME_HI(*add_style)) :
				NULL);
		}
		else
		{
			SSET_FORE_COLOR_NAME_HI(
				*merged_style,
				SGET_FORE_COLOR_NAME_HI(*add_style));
		}
	}
	if (add_style->flag_mask.has_color_back_hi)
	{
		if (do_free_src_and_alloc_copy)
		{
			SAFEFREE(SGET_BACK_COLOR_NAME_HI(*merged_style));
			SSET_BACK_COLOR_NAME_HI(
				*merged_style,
				(SGET_BACK_COLOR_NAME_HI(*add_style)) ?
				safestrdup(
					SGET_BACK_COLOR_NAME_HI(*add_style)) :
				NULL);
		}
		else
		{
			SSET_BACK_COLOR_NAME_HI(
				*merged_style,
				SGET_BACK_COLOR_NAME_HI(*add_style));
		}
	}
	if (add_style->flags.has_border_width)
	{
		SSET_BORDER_WIDTH(*merged_style, SGET_BORDER_WIDTH(*add_style));
	}
	if (add_style->flags.has_handle_width)
	{
		SSET_HANDLE_WIDTH(*merged_style, SGET_HANDLE_WIDTH(*add_style));
	}
	if (add_style->flags.has_icon_size_limits)
	{
		SSET_MIN_ICON_WIDTH(
			*merged_style, SGET_MIN_ICON_WIDTH(*add_style));
		SSET_MIN_ICON_HEIGHT(
			*merged_style, SGET_MIN_ICON_HEIGHT(*add_style));
		SSET_MAX_ICON_WIDTH(
			*merged_style, SGET_MAX_ICON_WIDTH(*add_style));
		SSET_MAX_ICON_HEIGHT(
			*merged_style, SGET_MAX_ICON_HEIGHT(*add_style));
	}
	if (add_style->flags.has_max_window_size)
	{
		SSET_MAX_WINDOW_WIDTH(
			*merged_style, SGET_MAX_WINDOW_WIDTH(*add_style));
		SSET_MAX_WINDOW_HEIGHT(
			*merged_style, SGET_MAX_WINDOW_HEIGHT(*add_style));
	}
	if (add_style->flags.has_window_shade_steps)
	{
		SSET_WINDOW_SHADE_STEPS(
			*merged_style, SGET_WINDOW_SHADE_STEPS(*add_style));
	}

	/* Note, only one style cmd can define a windows iconboxes,
	 * the last one encountered. */
	if (SHAS_ICON_BOXES(&add_style->flag_mask))
	{
		/* If style has iconboxes */
		/* copy it */
		if (do_free_src_and_alloc_copy)
		{
			remove_icon_boxes_from_style(merged_style);
			copy_icon_boxes(
				&SGET_ICON_BOXES(*merged_style),
				SGET_ICON_BOXES(*add_style));
		}
		else
		{
			SSET_ICON_BOXES(
				*merged_style, SGET_ICON_BOXES(*add_style));
		}
	}
	if (add_style->flags.use_layer)
	{
		SSET_LAYER(*merged_style, SGET_LAYER(*add_style));
	}
	if (add_style->flags.use_colorset)
	{
		SSET_COLORSET(*merged_style, SGET_COLORSET(*add_style));
	}
	if (add_style->flags.use_colorset_hi)
	{
		SSET_COLORSET_HI(*merged_style, SGET_COLORSET_HI(*add_style));
	}
	if (add_style->flags.use_border_colorset)
	{
		SSET_BORDER_COLORSET(
			*merged_style, SGET_BORDER_COLORSET(*add_style));
	}
	if (add_style->flags.use_border_colorset_hi)
	{
		SSET_BORDER_COLORSET_HI(
			*merged_style,SGET_BORDER_COLORSET_HI(*add_style));
	}
	if (add_style->flags.has_placement_penalty)
	{
		SSET_NORMAL_PLACEMENT_PENALTY(
			*merged_style,
			SGET_NORMAL_PLACEMENT_PENALTY(*add_style));
		SSET_ONTOP_PLACEMENT_PENALTY(
			*merged_style,
			SGET_ONTOP_PLACEMENT_PENALTY(*add_style));
		SSET_ICON_PLACEMENT_PENALTY(
			*merged_style, SGET_ICON_PLACEMENT_PENALTY(*add_style));
		SSET_STICKY_PLACEMENT_PENALTY(
			*merged_style,
			SGET_STICKY_PLACEMENT_PENALTY(*add_style));
		SSET_BELOW_PLACEMENT_PENALTY(
			*merged_style,
			SGET_BELOW_PLACEMENT_PENALTY(*add_style));
		SSET_EWMH_STRUT_PLACEMENT_PENALTY(
			*merged_style,
			SGET_EWMH_STRUT_PLACEMENT_PENALTY(*add_style));
	}
	if (add_style->flags.has_placement_percentage_penalty)
	{
		SSET_99_PLACEMENT_PERCENTAGE_PENALTY(
			*merged_style,
			SGET_99_PLACEMENT_PERCENTAGE_PENALTY(*add_style));
		SSET_95_PLACEMENT_PERCENTAGE_PENALTY(
			*merged_style,
			SGET_95_PLACEMENT_PERCENTAGE_PENALTY(*add_style));
		SSET_85_PLACEMENT_PERCENTAGE_PENALTY(
			*merged_style,
			SGET_85_PLACEMENT_PERCENTAGE_PENALTY(*add_style));
		SSET_75_PLACEMENT_PERCENTAGE_PENALTY(
			*merged_style,
			SGET_75_PLACEMENT_PERCENTAGE_PENALTY(*add_style));
	}
	/* merge the style flags */

	/*** ATTENTION:
	 ***     This must be the last thing that is done in this function! */
	merge_flags = (char *)&(merged_style->flags);
	add_flags = (char *)&(add_style->flags);
	merge_mask = (char *)&(merged_style->flag_mask);
	add_mask = (char *)&(add_style->flag_mask);
	merge_change_mask = (char *)&(merged_style->change_mask);
	add_change_mask = (char *)&(add_style->change_mask);
	for (i = 0; i < sizeof(style_flags); i++)
	{
		merge_flags[i] |= (add_flags[i] & add_mask[i]);
		merge_flags[i] &= (add_flags[i] | ~add_mask[i]);
		merge_mask[i] |= add_mask[i];
		merge_change_mask[i] &= ~(add_mask[i]);
		merge_change_mask[i] |= add_change_mask[i];
		/* This allowes for default values to be copied when both
		 * styles don't haven't set that bit in the mask. */
		merge_flags[i] |=
			(add_flags[i] & ~add_mask[i] & ~merge_mask[i]);
		merge_flags[i] &=
			(add_flags[i] | ~add_mask[i] | ~merge_mask[i]);

	}

	merged_style->has_style_changed |= add_style->has_style_changed;
	return;
}

static void free_style(window_style *style)
{
	/* Free contents of style */
	SAFEFREE(SGET_NAME(*style));
	SAFEFREE(SGET_BACK_COLOR_NAME(*style));
	SAFEFREE(SGET_FORE_COLOR_NAME(*style));
	SAFEFREE(SGET_BACK_COLOR_NAME_HI(*style));
	SAFEFREE(SGET_FORE_COLOR_NAME_HI(*style));
	SAFEFREE(SGET_DECOR_NAME(*style));
	SAFEFREE(SGET_ICON_FONT(*style));
	SAFEFREE(SGET_WINDOW_FONT(*style));
	SAFEFREE(SGET_ICON_NAME(*style));
	SAFEFREE(SGET_MINI_ICON_NAME(*style));
	remove_icon_boxes_from_style(style);

	return;
}

/* Frees only selected members of a style; adjusts the flag_mask and
 * change_mask appropriately. */
static void free_style_mask(window_style *style, style_flags *mask)
{
	style_flags local_mask;
	style_flags *pmask;

	/* mask out all bits that are not set in the target style */
	pmask =&local_mask;
	blockand((char *)pmask, (char *)&style->flag_mask, (char *)mask,
		 sizeof(style_flags));

	/* Free contents of style */
	if (pmask->has_color_back)
	{
		SAFEFREE(SGET_BACK_COLOR_NAME(*style));
	}
	if (pmask->has_color_fore)
	{
		SAFEFREE(SGET_FORE_COLOR_NAME(*style));
	}
	if (pmask->has_color_back_hi)
	{
		SAFEFREE(SGET_BACK_COLOR_NAME_HI(*style));
	}
	if (pmask->has_color_fore_hi)
	{
		SAFEFREE(SGET_FORE_COLOR_NAME_HI(*style));
	}
	if (pmask->has_decor)
	{
		SAFEFREE(SGET_DECOR_NAME(*style));
	}
	if (pmask->common.has_icon_font)
	{
		SAFEFREE(SGET_ICON_FONT(*style));
	}
	if (pmask->common.has_window_font)
	{
		SAFEFREE(SGET_WINDOW_FONT(*style));
	}
	if (pmask->has_icon)
	{
		SAFEFREE(SGET_ICON_NAME(*style));
	}
	if (pmask->has_mini_icon)
	{
		SAFEFREE(SGET_MINI_ICON_NAME(*style));
	}
	if (pmask->has_icon_boxes)
	{
		remove_icon_boxes_from_style(style);
	}
	/* remove styles from definitiion */
	blockunmask((char *)&style->flag_mask, (char *)&style->flag_mask,
		    (char *)pmask, sizeof(style_flags));
	blockunmask((char *)&style->change_mask, (char *)&style->change_mask,
		    (char *)pmask, sizeof(style_flags));

	return;
}

static void add_style_to_list(window_style *new_style)
{
	/* This used to contain logic that returned if the style didn't contain
	 * anything.    I don't see why we should bother. dje.
	 *
	 * used to merge duplicate entries, but that is no longer
	 * appropriate since conflicting styles are possible, and the
	 * last match should win! */

	if (last_style_in_list != NULL)
	{
		/* not first entry in list chain this entry to the list */
		SSET_NEXT_STYLE(*last_style_in_list, new_style);
	}
	else
	{
		/* first entry in list set the list root pointer. */
		all_styles = new_style;
	}
	SSET_PREV_STYLE(*new_style, last_style_in_list);
	SSET_NEXT_STYLE(*new_style, NULL);
	last_style_in_list = new_style;
	Scr.flags.do_need_style_list_update = 1;

	return;
} /* end function */

static void remove_style_from_list(window_style *style, Bool do_free_style)
{
	window_style *prev;
	window_style *next;

	prev = SGET_PREV_STYLE(*style);
	next = SGET_NEXT_STYLE(*style);
	if (!prev)
	{
		/* first style in list */
		all_styles = next;
	}
	else
	{
		/* not first style in list */
		SSET_NEXT_STYLE(*prev, next);
	}
	if (!next)
	{
		/* last style in list */
		last_style_in_list = prev;
	}
	else
	{
		SSET_PREV_STYLE(*next, prev);
	}
	if (do_free_style)
	{
		free_style(style);
		free(style);
	}
}

static Bool remove_all_of_style_from_list(char *style_ref)
{
	window_style *nptr = all_styles;
	window_style *next;
	Bool is_changed = False;

	/* loop though styles */
	while (nptr)
	{
		next = SGET_NEXT_STYLE(*nptr);
		/* Check if it's to be wiped */
		if (!strcmp(SGET_NAME(*nptr), style_ref))
		{
			remove_style_from_list(nptr, True);
			is_changed = True;
		}
		/* move on */
		nptr = next;
	}

	return is_changed;
}

static Bool __simplify_style_list(void)
{
	window_style *cur;
	Bool has_modified;
	Bool is_merge_allowed;

	/* Step 1:
	 *   Remove styles that are completely overridden by later
	 *   style definitions.  At the same time...
	 * Step 2:
	 *   Merge styles with the same name if there are no
	 *   conflicting styles with other names set in between. */
	for (cur = last_style_in_list, has_modified = False,
		     is_merge_allowed = True; cur; cur = SGET_PREV_STYLE(*cur))
	{
		style_flags dummyflags;
		/* incremental flags set in styles with the same name */
		style_flags sumflags;
		/* incremental flags set in styles with other names */
		style_flags interflags;
		window_style *cmp;

		memset(&interflags, 0, sizeof(style_flags));
		memcpy(&sumflags, &cur->flag_mask, sizeof(style_flags));
		cmp = SGET_PREV_STYLE(*cur);
		while (cmp)
		{
			if (strcmp(SGET_NAME(*cur), SGET_NAME(*cmp)) != 0)
			{
				blockor((char *)&interflags,
					(char *)&interflags,
					(char *)&cmp->flag_mask,
					sizeof(style_flags));
				cmp = SGET_PREV_STYLE(*cmp);
				continue;
			}
			if (blockissubset(
				    (char *)&cmp->flag_mask,
				    (char *)&sumflags,
				    sizeof(style_flags)))
			{
				/* The style is a subset of
				 * later style definitions;
				 * nuke it */
				window_style *tmp = SGET_PREV_STYLE(*cmp);
				remove_style_from_list(cmp, True);
				cmp = tmp;
				has_modified = True;
			}
			else
			{
				/* remove all styles that are overridden later
				 * from the style */
				free_style_mask(cmp, &sumflags);

				/* Add the style to the set */
				blockor((char *)&sumflags,
					(char *)&sumflags,
					(char *)&cmp->flag_mask,
					sizeof(style_flags));
				if (is_merge_allowed &&
				    !blockand(
					    (char *)&dummyflags,
					    (char *)&sumflags,
					    (char *)&interflags,
					    sizeof(style_flags)))
				{
					window_style *tmp =
						SGET_PREV_STYLE(*cmp);
					window_style *prev =
						SGET_PREV_STYLE(*cur);
					window_style *next =
						SGET_NEXT_STYLE(*cur);

					/* merge cmp into cur and
					 * delete it afterwards */
					merge_styles(cmp, cur, True);
					memcpy(cur, cmp, sizeof(window_style));
					/* restore fields overwritten
					 * by memcpy */
					SSET_PREV_STYLE(*cur, prev);
					SSET_NEXT_STYLE(*cur, next);
					/* remove the style without
					 * freeing the memory */
					remove_style_from_list(cmp, False);
					/* release the style structure
					 */
					free(cmp);
					cmp = tmp;
					has_modified = True;
				}
				else
				{
					is_merge_allowed = False;
					cmp = SGET_PREV_STYLE(*cmp);
				}
			}
		}
	}

	return has_modified;
}

static Bool style_parse_focus_policy_style(
	char *option, char *rest, Bool is_reversed, focus_policy_t *f,
	focus_policy_t *m, focus_policy_t *c)
{
	char *optlist[] = {
		"SortWindowlistByFocus",
		"FocusClickButtons",
		"FocusClickModifiers",
		"ClickRaisesFocused",
		"ClickDecorRaisesFocused",
		"ClickIconRaisesFocused",
		"ClickRaisesUnfocused",
		"ClickDecorRaisesUnfocused",
		"ClickIconRaisesUnfocused",
		"ClickToFocus",
		"ClickDecorToFocus",
		"ClickIconToFocus",
		"EnterToFocus",
		"LeaveToUnfocus",
		"FocusByProgram",
		"FocusByFunction",
		"FocusByFunctionWarpPointer",
		"Lenient",
		"PassFocusClick",
		"PassRaiseClick",
		"IgnoreFocusClickMotion",
		"IgnoreRaiseClickMotion",
		"AllowFocusClickFunction",
		"AllowRaiseClickFunction",
		"GrabFocus",
		"GrabFocusTransient",
		"OverrideGrabFocus",
		"ReleaseFocus",
		"ReleaseFocusTransient",
		"OverrideReleaseFocus",
		NULL
	};
	Bool found;
	int val;
	int index;
	char *next;
	char *token;

	found = True;
	val = !is_reversed;
	next = GetNextTokenIndex(option, optlist, 0, &index);
	switch (index)
	{
	case 0:
		/* SortWindowlistByFocus */
		FPS_SORT_WINDOWLIST_BY(
			*f, (val) ?
			FPOL_SORT_WL_BY_FOCUS : FPOL_SORT_WL_BY_OPEN);
		FPS_SORT_WINDOWLIST_BY(*m, 1);
		FPS_SORT_WINDOWLIST_BY(*c, 1);
		break;
	case 1:
		/* FocusClickButtons */
		if (is_reversed)
		{
			found = False;
			break;
		}
		token = PeekToken(next, NULL);
		val = 0;
		for ( ; token != NULL && isdigit(*token); token++)
		{
			int button;
			char s[2];

			s[0] = *token;
			s[1] = 0;
			button = atoi(s);
			if (button == 0)
			{
				val = 0xffffffff;
			}
			else if (button > NUMBER_OF_MOUSE_BUTTONS)
			{
				break;
			}
			else
			{
				val |= (1 << (button - 1));
			}
		}
		if (token != NULL && *token != 0)
		{
			fvwm_msg(
				ERR, "style_parse_focus_policy_style",
				"illegal mouse button '%c'", *token);
			val = DEF_FP_MOUSE_BUTTONS;
		}
		if (token == NULL)
		{
			val = DEF_FP_MOUSE_BUTTONS;
		}
		FPS_MOUSE_BUTTONS(*f, val);
		FPS_MOUSE_BUTTONS(*m, 0xffffffff);
		FPS_MOUSE_BUTTONS(*c, 0xffffffff);
		break;
	case 2:
		/* FocusClickModifiers */
		if (is_reversed)
		{
			found = False;
			break;
		}
		token = PeekToken(next, NULL);
		if (token == NULL || ParseModifiers(token, &val) == True)
		{
			val = DEF_FP_MODIFIERS;
		}
		FPS_MODIFIERS(*f, val);
		FPS_MODIFIERS(*m, 0xffffffff);
		FPS_MODIFIERS(*c, 0xffffffff);
		/*!!!*/
		break;
	case 3:
		/* ClickRaisesFocused */
		FPS_RAISE_FOCUSED_CLIENT_CLICK(*f, val);
		FPS_RAISE_FOCUSED_CLIENT_CLICK(*m, 1);
		FPS_RAISE_FOCUSED_CLIENT_CLICK(*c, 1);
		break;
	case 4:
		/* ClickDecorRaisesFocused */
		FPS_RAISE_FOCUSED_DECOR_CLICK(*f, val);
		FPS_RAISE_FOCUSED_DECOR_CLICK(*m, 1);
		FPS_RAISE_FOCUSED_DECOR_CLICK(*c, 1);
		break;
	case 5:
		/* ClickIconRaisesFocused */
		FPS_RAISE_FOCUSED_ICON_CLICK(*f, val);
		FPS_RAISE_FOCUSED_ICON_CLICK(*m, 1);
		FPS_RAISE_FOCUSED_ICON_CLICK(*c, 1);
		break;
	case 6:
		/* ClickRaisesUnfocused */
		FPS_RAISE_UNFOCUSED_CLIENT_CLICK(*f, val);
		FPS_RAISE_UNFOCUSED_CLIENT_CLICK(*m, 1);
		FPS_RAISE_UNFOCUSED_CLIENT_CLICK(*c, 1);
		break;
	case 7:
		/* ClickDecorRaisesUnfocused */
		FPS_RAISE_UNFOCUSED_DECOR_CLICK(*f, val);
		FPS_RAISE_UNFOCUSED_DECOR_CLICK(*m, 1);
		FPS_RAISE_UNFOCUSED_DECOR_CLICK(*c, 1);
		break;
	case 8:
		/* ClickIconRaisesUnfocused */
		FPS_RAISE_UNFOCUSED_ICON_CLICK(*f, val);
		FPS_RAISE_UNFOCUSED_ICON_CLICK(*m, 1);
		FPS_RAISE_UNFOCUSED_ICON_CLICK(*c, 1);
		break;
	case 9:
		/* ClickToFocus */
		FPS_FOCUS_CLICK_CLIENT(*f, val);
		FPS_FOCUS_CLICK_CLIENT(*m, 1);
		FPS_FOCUS_CLICK_CLIENT(*c, 1);
		break;
	case 10:
		/* ClickDecorToFocus */
		FPS_FOCUS_CLICK_DECOR(*f, val);
		FPS_FOCUS_CLICK_DECOR(*m, 1);
		FPS_FOCUS_CLICK_DECOR(*c, 1);
		break;
	case 11:
		/* ClickIconToFocus */
		FPS_FOCUS_CLICK_ICON(*f, val);
		FPS_FOCUS_CLICK_ICON(*m, 1);
		FPS_FOCUS_CLICK_ICON(*c, 1);
		break;
	case 12:
		/* EnterToFocus */
		FPS_FOCUS_ENTER(*f, val);
		FPS_FOCUS_ENTER(*m, 1);
		FPS_FOCUS_ENTER(*c, 1);
		break;
	case 13:
		/* LeaveToUnfocus */
		FPS_UNFOCUS_LEAVE(*f, val);
		FPS_UNFOCUS_LEAVE(*m, 1);
		FPS_UNFOCUS_LEAVE(*c, 1);
		break;
	case 14:
		/* FocusByProgram */
		FPS_FOCUS_BY_PROGRAM(*f, val);
		FPS_FOCUS_BY_PROGRAM(*m, 1);
		FPS_FOCUS_BY_PROGRAM(*c, 1);
		break;
	case 15:
		/* FocusByFunction */
		FPS_FOCUS_BY_FUNCTION(*f, val);
		FPS_FOCUS_BY_FUNCTION(*m, 1);
		FPS_FOCUS_BY_FUNCTION(*c, 1);
		break;
	case 16:
		/* FocusByFunctionWarpPointer */
		FPS_WARP_POINTER_ON_FOCUS_FUNC(*f, val);
		FPS_WARP_POINTER_ON_FOCUS_FUNC(*m, 1);
		FPS_WARP_POINTER_ON_FOCUS_FUNC(*c, 1);
		break;
	case 17:
		/* Lenient */
		FPS_LENIENT(*f, val);
		FPS_LENIENT(*m, 1);
		FPS_LENIENT(*c, 1);
		break;
	case 18:
		/* PassFocusClick */
		FPS_PASS_FOCUS_CLICK(*f, val);
		FPS_PASS_FOCUS_CLICK(*m, 1);
		FPS_PASS_FOCUS_CLICK(*c, 1);
		break;
	case 19:
		/* PassRaiseClick */
		FPS_PASS_RAISE_CLICK(*f, val);
		FPS_PASS_RAISE_CLICK(*m, 1);
		FPS_PASS_RAISE_CLICK(*c, 1);
		break;
	case 20:
		/* IgnoreFocusClickMotion */
		FPS_IGNORE_FOCUS_CLICK_MOTION(*f, val);
		FPS_IGNORE_FOCUS_CLICK_MOTION(*m, 1);
		FPS_IGNORE_FOCUS_CLICK_MOTION(*c, 1);
		break;
	case 21:
		/* IgnoreRaiseClickMotion */
		FPS_IGNORE_RAISE_CLICK_MOTION(*f, val);
		FPS_IGNORE_RAISE_CLICK_MOTION(*m, 1);
		FPS_IGNORE_RAISE_CLICK_MOTION(*c, 1);
		break;
	case 22:
		/* AllowFocusClickFunction */
		FPS_ALLOW_FUNC_FOCUS_CLICK(*f, val);
		FPS_ALLOW_FUNC_FOCUS_CLICK(*m, 1);
		FPS_ALLOW_FUNC_FOCUS_CLICK(*c, 1);
		break;
	case 23:
		/* AllowRaiseClickFunction */
		FPS_ALLOW_FUNC_RAISE_CLICK(*f, val);
		FPS_ALLOW_FUNC_RAISE_CLICK(*m, 1);
		FPS_ALLOW_FUNC_RAISE_CLICK(*c, 1);
		break;
	case 24:
		/* GrabFocus */
		FPS_GRAB_FOCUS(*f, val);
		FPS_GRAB_FOCUS(*m, 1);
		FPS_GRAB_FOCUS(*c, 1);
		break;
	case 25:
		/* GrabFocusTransient */
		FPS_GRAB_FOCUS_TRANSIENT(*f, val);
		FPS_GRAB_FOCUS_TRANSIENT(*m, 1);
		FPS_GRAB_FOCUS_TRANSIENT(*c, 1);
		break;
	case 26:
		/* OverrideGrabFocus */
		FPS_OVERRIDE_GRAB_FOCUS(*f, val);
		FPS_OVERRIDE_GRAB_FOCUS(*m, 1);
		FPS_OVERRIDE_GRAB_FOCUS(*c, 1);
		break;
	case 27:
		/* ReleaseFocus */
		FPS_RELEASE_FOCUS(*f, val);
		FPS_RELEASE_FOCUS(*m, 1);
		FPS_RELEASE_FOCUS(*c, 1);
		break;
	case 28:
		/* ReleaseFocusTransient */
		FPS_RELEASE_FOCUS_TRANSIENT(*f, val);
		FPS_RELEASE_FOCUS_TRANSIENT(*m, 1);
		FPS_RELEASE_FOCUS_TRANSIENT(*c, 1);
		break;
	case 29:
		/* OverrideReleaseFocus */
		FPS_OVERRIDE_RELEASE_FOCUS(*f, val);
		FPS_OVERRIDE_RELEASE_FOCUS(*m, 1);
		FPS_OVERRIDE_RELEASE_FOCUS(*c, 1);
		break;
	default:
		found = False;
		break;
	}

	return found;
}

static void style_parse_icon_size_style(
	char *option, char *rest, window_style *ps)
{
	int vals[4];
	int i;

	switch (GetIntegerArguments(rest, &rest, vals, 4))
	{
	case 0:
		/* No arguments results in default values */
		vals[0] = vals[1] = UNSPECIFIED_ICON_DIMENSION;
		/* fall through */
	case 2:
		/* Max and min values are the same */
		vals[2] = vals[0];
		vals[3] = vals[1];
		/* fall through */
	case 4:
		/* Validate values */
		for (i = 0; i < 4; i++)
		{
			int use_default = 0;

			if (vals[i] != UNSPECIFIED_ICON_DIMENSION &&
			    (vals[i] < MIN_ALLOWABLE_ICON_DIMENSION ||
			     vals[i] > MAX_ALLOWABLE_ICON_DIMENSION))
			{
				fvwm_msg(
					ERR, "CMD_Style",
					"IconSize dimension (%d) not in valid"
					" range (%d-%d)",
					 vals[i], MIN_ALLOWABLE_ICON_DIMENSION,
					 MAX_ALLOWABLE_ICON_DIMENSION);
				use_default = 1;
			}

			/* User requests default value for this dimension */
			else if (vals[i] == UNSPECIFIED_ICON_DIMENSION)
			{
				use_default = 1;
			}

			if (use_default)
			{
				/* Set default value for this dimension.  The
				 * first two indexes refer to min values, the
				 * latter two to max values. */
				vals[i] = i < 2 ? MIN_ALLOWABLE_ICON_DIMENSION :
					MAX_ALLOWABLE_ICON_DIMENSION;
			}
		}
		SSET_MIN_ICON_WIDTH(*ps, vals[0]);
		SSET_MIN_ICON_HEIGHT(*ps, vals[1]);
		SSET_MAX_ICON_WIDTH(*ps, vals[2]);
		SSET_MAX_ICON_HEIGHT(*ps, vals[3]);

		ps->flags.has_icon_size_limits = 1;
		ps->flag_mask.has_icon_size_limits = 1;
		ps->change_mask.has_icon_size_limits = 1;
		break;
	default:
		fvwm_msg(
			ERR, "CMD_Style",
			"IconSize requires exactly 0, 2 or 4"
			" numerical arguments");
		break;
	}

	return;
}

static void style_parse_icon_box_style(
	icon_boxes **ret_ib, char *option, char *rest, window_style *ps)
{
	icon_boxes *IconBoxes = NULL;
	Bool is_screen_given = False;
	int val[4];
	int num;
	int i;

	option = PeekToken(rest, NULL);
	if (!option || StrEquals(option, "none"))
	{
		/* delete icon boxes from style */
		if (SGET_ICON_BOXES(*ps))
		{
			remove_icon_boxes_from_style(ps);
		}
		(*ret_ib) = NULL;
		if (option)
		{
			/* disable default icon box */
			SFSET_DO_IGNORE_ICON_BOXES(*ps, 1);
		}
		else
		{
			/* use default icon box */
			SFSET_DO_IGNORE_ICON_BOXES(*ps, 0);
		}
		SMSET_DO_IGNORE_ICON_BOXES(*ps, 1);
		SCSET_DO_IGNORE_ICON_BOXES(*ps, 1);
		ps->flags.has_icon_boxes = 0;
		ps->flag_mask.has_icon_boxes = 1;
		ps->change_mask.has_icon_boxes = 1;
		return;
	}

	/* otherwise try to parse the icon box */
	IconBoxes = (icon_boxes *)safemalloc(sizeof(icon_boxes));
	/* clear it */
	memset(IconBoxes, 0, sizeof(icon_boxes));
	IconBoxes->IconScreen = FSCREEN_GLOBAL;
	/* init grid x */
	IconBoxes->IconGrid[0] = 3;
	/* init grid y */
	IconBoxes->IconGrid[1] = 3;

	/* check for screen (for 4 numbers) */
	if (StrEquals(option, "screen"))
	{
		is_screen_given = True;
		option = PeekToken(rest, &rest); /* skip screen */
		option = PeekToken(rest, &rest); /* get the screen spec */
		IconBoxes->IconScreen =
			FScreenGetScreenArgument(option, FSCREEN_SPEC_PRIMARY);
	}

	/* try for 4 numbers x y x y */
	num = GetIntegerArguments(rest, NULL, val, 4);

	/* if 4 numbers */
	if (num == 4)
	{
		for (i = 0; i < 4; i++)
		{
			/* make sure the value fits into a short */
			if (val[i] < -32768)
			{
				val[i] = -32768;
			}
			if (val[i] > 32767)
			{
				val[i] = 32767;
			}
			IconBoxes->IconBox[i] = val[i];
			/* If leading minus sign */
			option = PeekToken(rest, &rest);
			if (option[0] == '-')
			{
				IconBoxes->IconSign[i]='-';
			} /* end leading minus sign */
		}
		/* Note: here there is no test for valid co-ords, use geom */
	}
	else if  (is_screen_given)
	{
		/* screen-spec is given but not 4 numbers */
		fvwm_msg(
			ERR,"CMD_Style",
			"IconBox requires 4 numbers if screen is given!"
			" Invalid: <%s>.", option);
		/* Drop the box */
		free(IconBoxes);
		/* forget about it */
		IconBoxes = 0;
	}
	else
	{
		/* Not 4 numeric args dje */
		/* bigger than =32767x32767+32767+32767 */
		int geom_flags;
		int l;
		unsigned int width;
		unsigned int height;

		/* read in 1 word w/o advancing */
		option = PeekToken(rest, NULL);
		if (!option)
		{
			return;
		}
		l = strlen(option);
		if (l > 0 && l < 24)
		{
			/* if word found, not too long */
			geom_flags = FScreenParseGeometryWithScreen(
				option, &IconBoxes->IconBox[0],
				&IconBoxes->IconBox[1], &width, &height,
				&IconBoxes->IconScreen);
			if (width == 0)
			{
				/* zero width is invalid */
				fvwm_msg(
					ERR,"CMD_Style",
					"IconBox requires 4 numbers or"
					" geometry! Invalid string <%s>.",
					option);
				/* Drop the box */
				free(IconBoxes);
				/* forget about it */
				IconBoxes = 0;
			}
			else
			{
				/* got valid iconbox geom */
				if (geom_flags & XNegative)
				{
					IconBoxes->IconBox[0] =
						/* neg x coord */
						IconBoxes->IconBox[0] -
						width - 2;
					/* save for later */
					IconBoxes->IconSign[0]='-';
					IconBoxes->IconSign[2]='-';
				}
				if (geom_flags & YNegative)
				{
					IconBoxes->IconBox[1] =
						/* neg y coord */
						IconBoxes->IconBox[1]
						- height -2;
					/* save for later */
					IconBoxes->IconSign[1]='-';
					IconBoxes->IconSign[3]='-';
				}
				/* x + wid = right x */
				IconBoxes->IconBox[2] =
					width + IconBoxes->IconBox[0];
				/* y + height = bottom y */
				IconBoxes->IconBox[3] =
					height + IconBoxes->IconBox[1];
			} /* end icon geom worked */
		}
		else
		{
			/* no word or too long; drop the box */
			free(IconBoxes);
			/* forget about it */
			IconBoxes = 0;
		} /* end word found, not too long */
	} /* end not 4 args */
	/* If we created an IconBox, put it in the chain. */
	if (IconBoxes != 0)
	{
		/* no error */
		if (SGET_ICON_BOXES(*ps) == 0)
		{
			/* first one, chain to root */
			SSET_ICON_BOXES(*ps, IconBoxes);
		}
		else
		{
			/* else not first one, add to end of chain */
			(*ret_ib)->next = IconBoxes;
		} /* end not first one */
		/* new current box. save for grid */
		(*ret_ib) = IconBoxes;
	} /* end no error */
	SFSET_DO_IGNORE_ICON_BOXES(*ps, 0);
	SMSET_DO_IGNORE_ICON_BOXES(*ps, 1);
	SCSET_DO_IGNORE_ICON_BOXES(*ps, 1);
	ps->flags.has_icon_boxes = !!(SGET_ICON_BOXES(*ps));
	ps->flag_mask.has_icon_boxes = 1;
	ps->change_mask.has_icon_boxes = 1;

	return;
}

static void style_parse_icon_grid_style(
	char *option, char *rest, window_style *ps, icon_boxes *ib)
{
	int val[4];
	int num;
	int i;

	/* The grid always affects the prior iconbox */
	if (ib == 0)
	{
		/* If no current box */
		fvwm_msg(
			ERR,"CMD_Style",
			"IconGrid must follow an IconBox in same Style"
			" command");
		return;
	}
	/* have a place to grid */
	/* 2 shorts */
	num = GetIntegerArguments(rest, NULL, val, 2);
	if (num != 2 || val[0] < 1 || val[1] < 1)
	{
		fvwm_msg(
			ERR,"CMD_Style",
			"IconGrid needs 2 numbers > 0. Got %d numbers."
			" x=%d y=%d!", num, val[0], val[1]);
		/* reset grid */
		ib->IconGrid[0] = 3;
		ib->IconGrid[1] = 3;
	}
	else
	{
		for (i = 0; i < 2; i++)
		{
			/* make sure the value fits into a short */
			if (val[i] > 32767)
			{
				val[i] = 32767;
			}
			ib->IconGrid[i] = val[i];
		}
	} /* end bad grid */

	return;
}

static void style_parse_icon_fill_style(
	char *option, char *rest, window_style *ps, icon_boxes *ib)
{
	/* first  type direction parsed */
	unsigned char IconFill_1;
	/* second type direction parsed */
	unsigned char IconFill_2;

	/* direction to fill iconbox */
	/* The fill always affects the prior iconbox */
	if (ib == 0)
	{
		/* If no current box */
		fvwm_msg(
			ERR,"CMD_Style",
			"IconFill must follow an IconBox in same Style"
			" command");
		return;
	}
	/* have a place to fill */
	option = PeekToken(rest, &rest);
	/* top/bot/lft/rgt */
	if (!option || Get_TBLR(option, &IconFill_1) == 0)
	{
		/* its wrong */
		if (!option)
		{
			option = "(none)";
		}
		fvwm_msg(
			ERR,"CMD_Style",
			"IconFill must be followed by T|B|R|L, found"
			" %s.", option);
		return;
	}
	/* first word valid */

	/* read in second word */
	option = PeekToken(rest, &rest);
	/* top/bot/lft/rgt */
	if (!option || Get_TBLR(option, &IconFill_2) == 0)
	{
		/* its wrong */
		if (!option)
		{
			option = "(none)";
		}
		fvwm_msg(
			ERR,"CMD_Style",
			"IconFill must be followed by T|B|R|L,"
			" found %s.", option);
		return;
	}
	if ((IconFill_1 & ICONFILLHRZ) == (IconFill_2 & ICONFILLHRZ))
	{
		fvwm_msg(
			ERR, "CMD_Style",
			"IconFill must specify a horizontal"
			" and vertical direction.");
		return;
	}
	/* Its valid! */
	/* merge in flags */
	ib->IconFlags |= IconFill_1;
	/* ignore horiz in 2nd arg */
	IconFill_2 &= ~ICONFILLHRZ;
	/* merge in flags */
	ib->IconFlags |= IconFill_2;

	return;
}


static Bool style_parse_one_style_option(
	char *token, char *rest, window_style *ps, icon_boxes **cur_ib)
{
	window_style *add_style;
	/* work area for button number */
	int butt;
	int num;
	int i;
	int tmpno[3] = { -1, -1, -1 };
	int val[4];
	int spargs = 0;
	Bool found;

	found = True;
	switch (tolower(token[0]))
	{
	case '!':
		if (strncasecmp(token, "!fp", 3) == 0)
		{
			/* parse focus policy options */
			found = style_parse_focus_policy_style(
				token + 3, rest, True,
				&SF_FOCUS_POLICY(*ps),
				&SM_FOCUS_POLICY(*ps),
				&SC_FOCUS_POLICY(*ps));
		}
		else
		{
			found = False;
		}
		break;
	case 'a':
		if (StrEquals(token, "ACTIVEPLACEMENT"))
		{
			ps->flags.placement_mode &= (~PLACE_RANDOM);
			ps->flag_mask.placement_mode |= PLACE_RANDOM;
			ps->change_mask.placement_mode |= PLACE_RANDOM;
		}
		else if (StrEquals(token, "ACTIVEPLACEMENTHONORSSTARTSONPAGE"))
		{
			ps->flags.manual_placement_honors_starts_on_page = 1;
			ps->flag_mask.manual_placement_honors_starts_on_page =
				1;
			ps->change_mask.manual_placement_honors_starts_on_page =
				1;
		}
		else if (StrEquals(token, "ACTIVEPLACEMENTIGNORESSTARTSONPAGE"))
		{
			ps->flags.manual_placement_honors_starts_on_page = 0;
			ps->flag_mask.manual_placement_honors_starts_on_page =
				1;
			ps->change_mask.manual_placement_honors_starts_on_page =
				1;
		}
		else if (StrEquals(token, "AllowRestack"))
		{
			SFSET_DO_IGNORE_RESTACK(*ps, 0);
			SMSET_DO_IGNORE_RESTACK(*ps, 1);
			SCSET_DO_IGNORE_RESTACK(*ps, 1);
		}
		else
		{
			found = False;
		}
		break;

	case 'b':
		if (StrEquals(token, "BackColor"))
		{
			GetNextToken(rest, &token);
			if (token)
			{
				SAFEFREE(SGET_BACK_COLOR_NAME(*ps));
				SSET_BACK_COLOR_NAME(*ps, token);
				ps->flags.has_color_back = 1;
				ps->flag_mask.has_color_back = 1;
				ps->change_mask.has_color_back = 1;
				ps->flags.use_colorset = 0;
				ps->flag_mask.use_colorset = 1;
				ps->change_mask.use_colorset = 1;
			}
			else
			{
				fvwm_msg(
					ERR, "CMD_Style",
					"Style BackColor requires color"
					" argument");
			}
		}
		else if (StrEquals(token, "BUTTON"))
		{
			butt = -1;
			GetIntegerArguments(rest, NULL, &butt, 1);
			butt = BUTTON_INDEX(butt);
			if (butt >= 0 && butt < NUMBER_OF_BUTTONS)
			{
				ps->flags.is_button_disabled &= ~(1 << butt);
				ps->flag_mask.is_button_disabled |= (1 << butt);
				ps->change_mask.is_button_disabled |=
					(1 << butt);
			}
			else
			{
				fvwm_msg(ERR, "CMD_Style",
					 "Invalid Button Style Index given");
			}

		}
		else if (StrEquals(token, "BorderWidth"))
		{
			if (GetIntegerArguments(rest, NULL, val, 1))
			{
				SSET_BORDER_WIDTH(*ps, (short)*val);
				ps->flags.has_border_width = 1;
				ps->flag_mask.has_border_width = 1;
				ps->change_mask.has_border_width = 1;
			}
			else
			{
				fvwm_msg(
					ERR, "CMD_Style",
					"Style BorderWidth requires width"
					" argument");
			}
		}
		else if (StrEquals(token, "BackingStore"))
		{
			ps->flags.use_backing_store = BACKINGSTORE_ON;
			ps->flag_mask.use_backing_store = BACKINGSTORE_MASK;
			ps->change_mask.use_backing_store = BACKINGSTORE_MASK;
		}
		else if (StrEquals(token, "BackingStoreOff"))
		{
			ps->flags.use_backing_store = BACKINGSTORE_OFF;
			ps->flag_mask.use_backing_store = BACKINGSTORE_MASK;
			ps->change_mask.use_backing_store = BACKINGSTORE_MASK;
		}
		else if (StrEquals(token, "BackingStoreWindowDefault"))
		{
			ps->flags.use_backing_store = BACKINGSTORE_DEFAULT;
			ps->flag_mask.use_backing_store = BACKINGSTORE_MASK;
			ps->change_mask.use_backing_store = BACKINGSTORE_MASK;
		}
		else if (StrEquals(token, "BorderColorset"))
		{
			*val = -1;
			GetIntegerArguments(rest, NULL, val, 1);
			SSET_BORDER_COLORSET(*ps, *val);
			alloc_colorset(*val);
			ps->flags.use_border_colorset = (*val >= 0);
			ps->flag_mask.use_border_colorset = 1;
			ps->change_mask.use_border_colorset = 1;
		}
		else if (StrEquals(token, "BottomTitleRotated"))
		{
			SFSET_IS_BOTTOM_TITLE_ROTATED(*ps, 1);
			SMSET_IS_BOTTOM_TITLE_ROTATED(*ps, 1);
			SCSET_IS_BOTTOM_TITLE_ROTATED(*ps, 1);
		}
		else if (StrEquals(token, "BottomTitleNotRotated"))
		{
			SFSET_IS_BOTTOM_TITLE_ROTATED(*ps, 0);
			SMSET_IS_BOTTOM_TITLE_ROTATED(*ps, 1);
			SCSET_IS_BOTTOM_TITLE_ROTATED(*ps, 1);
		}
		else if (StrEquals(token, "Border"))
		{
			SFSET_HAS_NO_BORDER(*ps, 0);
			SMSET_HAS_NO_BORDER(*ps, 1);
			SCSET_HAS_NO_BORDER(*ps, 1);
		}
		else
		{
			found = False;
		}
		break;

	case 'c':
		if (StrEquals(token, "CascadePlacement"))
		{
			ps->flags.placement_mode = PLACE_CASCADE;
			ps->flag_mask.placement_mode = PLACE_MASK;
			ps->change_mask.placement_mode = PLACE_MASK;
		}
		else if (StrEquals(token, "CLEVERPLACEMENT"))
		{
			ps->flags.placement_mode |= PLACE_CLEVER;
			ps->flag_mask.placement_mode |= PLACE_CLEVER;
			ps->change_mask.placement_mode |= PLACE_CLEVER;
		}
		else if (StrEquals(token, "CleverPlacementOff"))
		{
			ps->flags.placement_mode &= (~PLACE_CLEVER);
			ps->flag_mask.placement_mode |= PLACE_CLEVER;
			ps->change_mask.placement_mode |= PLACE_CLEVER;
		}
		else if (StrEquals(token, "CAPTUREHONORSSTARTSONPAGE"))
		{
			ps->flags.capture_honors_starts_on_page = 1;
			ps->flag_mask.capture_honors_starts_on_page = 1;
			ps->change_mask.capture_honors_starts_on_page = 1;
		}
		else if (StrEquals(token, "CAPTUREIGNORESSTARTSONPAGE"))
		{
			ps->flags.capture_honors_starts_on_page = 0;
			ps->flag_mask.capture_honors_starts_on_page = 1;
			ps->change_mask.capture_honors_starts_on_page = 1;
		}
		else if (StrEquals(token, "ColorSet"))
		{
			*val = -1;
			GetIntegerArguments(rest, NULL, val, 1);
			if (*val < 0)
				*val = -1;
			SSET_COLORSET(*ps, *val);
			alloc_colorset(*val);
			ps->flags.use_colorset = (*val >= 0);
			ps->flag_mask.use_colorset = 1;
			ps->change_mask.use_colorset = 1;
		}
		else if (StrEquals(token, "COLOR"))
		{
			char c = 0;
			char *next;

			next = GetNextToken(rest, &token);
			if (token == NULL)
			{
				fvwm_msg(
					ERR, "CMD_Style",
					"Color Style requires a color"
					" argument");
				break;
			}
			if (strncasecmp(token, "rgb:", 4) == 0)
			{
				char *s;
				int i;

				/* spool to third '/' */
				for (i = 0, s = token + 4; *s && i < 3; s++)
				{
					if (*s == '/')
					{
						i++;
					}
				}
				s--;
				if (i == 3)
				{
					*s = 0;
					/* spool to third '/' in original
					 * string too */
					for (i = 0, s = rest; *s && i < 3; s++)
					{
						if (*s == '/')
						{
							i++;
						}
					}
					next = s - 1;
				}
			}
			else
			{
				free(token);
				next = DoGetNextToken(
					rest, &token, NULL, ",/", &c);
			}
			rest = next;
			SAFEFREE(SGET_FORE_COLOR_NAME(*ps));
			SSET_FORE_COLOR_NAME(*ps, token);
			ps->flags.has_color_fore = 1;
			ps->flag_mask.has_color_fore = 1;
			ps->change_mask.has_color_fore = 1;
			ps->flags.use_colorset = 0;
			ps->flag_mask.use_colorset = 1;
			ps->change_mask.use_colorset = 1;

			/* skip over '/' */
			if (c != '/')
			{
				while (rest && *rest &&
				       isspace((unsigned char)*rest) &&
				       *rest != ',' && *rest != '/')
				{
					rest++;
				}
				if (*rest == '/')
				{
					rest++;
				}
			}

			GetNextToken(rest, &token);
			if (!token)
			{
				fvwm_msg(
					ERR, "CMD_Style",
					"Color Style called with incomplete"
					" color argument.");
				break;
			}
			SAFEFREE(SGET_BACK_COLOR_NAME(*ps));
			SSET_BACK_COLOR_NAME(*ps, token);
			ps->flags.has_color_back = 1;
			ps->flag_mask.has_color_back = 1;
			ps->change_mask.has_color_back = 1;
			break;
		}
		else if (StrEquals(token, "CirculateSkipIcon"))
		{
			SFSET_DO_CIRCULATE_SKIP_ICON(*ps, 1);
			SMSET_DO_CIRCULATE_SKIP_ICON(*ps, 1);
			SCSET_DO_CIRCULATE_SKIP_ICON(*ps, 1);
		}
		else if (StrEquals(token, "CirculateSkipShaded"))
		{
			SFSET_DO_CIRCULATE_SKIP_SHADED(*ps, 1);
			SMSET_DO_CIRCULATE_SKIP_SHADED(*ps, 1);
			SCSET_DO_CIRCULATE_SKIP_SHADED(*ps, 1);
		}
		else if (StrEquals(token, "CirculateHitShaded"))
		{
			SFSET_DO_CIRCULATE_SKIP_SHADED(*ps, 0);
			SMSET_DO_CIRCULATE_SKIP_SHADED(*ps, 1);
			SCSET_DO_CIRCULATE_SKIP_SHADED(*ps, 1);
		}
		else if (StrEquals(token, "CirculateHitIcon"))
		{
			SFSET_DO_CIRCULATE_SKIP_ICON(*ps, 0);
			SMSET_DO_CIRCULATE_SKIP_ICON(*ps, 1);
			SCSET_DO_CIRCULATE_SKIP_ICON(*ps, 1);
		}
		else if (StrEquals(token, "ClickToFocus"))
		{
			FPS_GRAB_FOCUS(SF_FOCUS_POLICY(*ps), 1);
			FPS_GRAB_FOCUS(SM_FOCUS_POLICY(*ps), 1);
			FPS_GRAB_FOCUS(SC_FOCUS_POLICY(*ps), 1);
			FPS_RELEASE_FOCUS(SF_FOCUS_POLICY(*ps), 1);
			FPS_RELEASE_FOCUS(SM_FOCUS_POLICY(*ps), 1);
			FPS_RELEASE_FOCUS(SC_FOCUS_POLICY(*ps), 1);
			FPS_FOCUS_ENTER(SF_FOCUS_POLICY(*ps), 0);
			FPS_FOCUS_ENTER(SM_FOCUS_POLICY(*ps), 1);
			FPS_FOCUS_ENTER(SC_FOCUS_POLICY(*ps), 1);
			FPS_UNFOCUS_LEAVE(SF_FOCUS_POLICY(*ps), 0);
			FPS_UNFOCUS_LEAVE(SM_FOCUS_POLICY(*ps), 1);
			FPS_UNFOCUS_LEAVE(SC_FOCUS_POLICY(*ps), 1);
			FPS_FOCUS_CLICK_CLIENT(SF_FOCUS_POLICY(*ps), 1);
			FPS_FOCUS_CLICK_CLIENT(SM_FOCUS_POLICY(*ps), 1);
			FPS_FOCUS_CLICK_CLIENT(SC_FOCUS_POLICY(*ps), 1);
			FPS_FOCUS_CLICK_DECOR(SF_FOCUS_POLICY(*ps), 1);
			FPS_FOCUS_CLICK_DECOR(SM_FOCUS_POLICY(*ps), 1);
			FPS_FOCUS_CLICK_DECOR(SC_FOCUS_POLICY(*ps), 1);
			FPS_FOCUS_CLICK_ICON(SF_FOCUS_POLICY(*ps), 1);
			FPS_FOCUS_CLICK_ICON(SM_FOCUS_POLICY(*ps), 1);
			FPS_FOCUS_CLICK_ICON(SC_FOCUS_POLICY(*ps), 1);
			FPS_RAISE_FOCUSED_CLIENT_CLICK(SF_FOCUS_POLICY(*ps), 1);
			FPS_RAISE_FOCUSED_CLIENT_CLICK(SM_FOCUS_POLICY(*ps), 1);
			FPS_RAISE_FOCUSED_CLIENT_CLICK(SC_FOCUS_POLICY(*ps), 1);
			FPS_RAISE_UNFOCUSED_CLIENT_CLICK(
				SF_FOCUS_POLICY(*ps), 1);
			FPS_RAISE_UNFOCUSED_CLIENT_CLICK(
				SM_FOCUS_POLICY(*ps), 1);
			FPS_RAISE_UNFOCUSED_CLIENT_CLICK(
				SC_FOCUS_POLICY(*ps), 1);
			FPS_RAISE_FOCUSED_DECOR_CLICK(SF_FOCUS_POLICY(*ps), 1);
			FPS_RAISE_FOCUSED_DECOR_CLICK(SM_FOCUS_POLICY(*ps), 1);
			FPS_RAISE_FOCUSED_DECOR_CLICK(SC_FOCUS_POLICY(*ps), 1);
			FPS_RAISE_UNFOCUSED_DECOR_CLICK(
				SF_FOCUS_POLICY(*ps), 1);
			FPS_RAISE_UNFOCUSED_DECOR_CLICK(
				SM_FOCUS_POLICY(*ps), 1);
			FPS_RAISE_UNFOCUSED_DECOR_CLICK(
				SC_FOCUS_POLICY(*ps), 1);
			FPS_PASS_FOCUS_CLICK(SF_FOCUS_POLICY(*ps), 1);
			FPS_PASS_FOCUS_CLICK(SM_FOCUS_POLICY(*ps), 1);
			FPS_PASS_FOCUS_CLICK(SC_FOCUS_POLICY(*ps), 1);
			FPS_PASS_RAISE_CLICK(SF_FOCUS_POLICY(*ps), 1);
			FPS_PASS_RAISE_CLICK(SM_FOCUS_POLICY(*ps), 1);
			FPS_PASS_RAISE_CLICK(SC_FOCUS_POLICY(*ps), 1);
			FPS_FOCUS_BY_PROGRAM(SF_FOCUS_POLICY(*ps), 1);
			FPS_FOCUS_BY_PROGRAM(SM_FOCUS_POLICY(*ps), 1);
			FPS_FOCUS_BY_PROGRAM(SC_FOCUS_POLICY(*ps), 1);
			FPS_ALLOW_FUNC_FOCUS_CLICK(SF_FOCUS_POLICY(*ps), 1);
			FPS_ALLOW_FUNC_FOCUS_CLICK(SM_FOCUS_POLICY(*ps), 1);
			FPS_ALLOW_FUNC_FOCUS_CLICK(SC_FOCUS_POLICY(*ps), 1);
			FPS_ALLOW_FUNC_RAISE_CLICK(SF_FOCUS_POLICY(*ps), 1);
			FPS_ALLOW_FUNC_RAISE_CLICK(SM_FOCUS_POLICY(*ps), 1);
			FPS_ALLOW_FUNC_RAISE_CLICK(SC_FOCUS_POLICY(*ps), 1);
			FPS_FOCUS_BY_FUNCTION(SF_FOCUS_POLICY(*ps), 1);
			FPS_FOCUS_BY_FUNCTION(SM_FOCUS_POLICY(*ps), 1);
			FPS_FOCUS_BY_FUNCTION(SC_FOCUS_POLICY(*ps), 1);
			FPS_WARP_POINTER_ON_FOCUS_FUNC(SF_FOCUS_POLICY(*ps), 0);
			FPS_WARP_POINTER_ON_FOCUS_FUNC(SM_FOCUS_POLICY(*ps), 1);
			FPS_WARP_POINTER_ON_FOCUS_FUNC(SC_FOCUS_POLICY(*ps), 1);
			FPS_SORT_WINDOWLIST_BY(
				SF_FOCUS_POLICY(*ps), FPOL_SORT_WL_BY_OPEN);
			FPS_SORT_WINDOWLIST_BY(SM_FOCUS_POLICY(*ps), 1);
			FPS_SORT_WINDOWLIST_BY(SC_FOCUS_POLICY(*ps), 1);
		}
		else if (StrEquals(token, "ClickToFocusPassesClick"))
		{
			FPS_PASS_FOCUS_CLICK(SF_FOCUS_POLICY(*ps), 1);
			FPS_PASS_FOCUS_CLICK(SM_FOCUS_POLICY(*ps), 1);
			FPS_PASS_FOCUS_CLICK(SC_FOCUS_POLICY(*ps), 1);
			FPS_PASS_RAISE_CLICK(SF_FOCUS_POLICY(*ps), 1);
			FPS_PASS_RAISE_CLICK(SM_FOCUS_POLICY(*ps), 1);
			FPS_PASS_RAISE_CLICK(SC_FOCUS_POLICY(*ps), 1);
		}
		else if (StrEquals(token, "ClickToFocusPassesClickOff"))
		{
			FPS_PASS_FOCUS_CLICK(SF_FOCUS_POLICY(*ps), 0);
			FPS_PASS_FOCUS_CLICK(SM_FOCUS_POLICY(*ps), 1);
			FPS_PASS_FOCUS_CLICK(SC_FOCUS_POLICY(*ps), 1);
			FPS_PASS_RAISE_CLICK(SF_FOCUS_POLICY(*ps), 0);
			FPS_PASS_RAISE_CLICK(SM_FOCUS_POLICY(*ps), 1);
			FPS_PASS_RAISE_CLICK(SC_FOCUS_POLICY(*ps), 1);
		}
		else if (StrEquals(token, "ClickToFocusRaises"))
		{
			FPS_RAISE_FOCUSED_CLIENT_CLICK(SF_FOCUS_POLICY(*ps), 1);
			FPS_RAISE_FOCUSED_CLIENT_CLICK(SM_FOCUS_POLICY(*ps), 1);
			FPS_RAISE_FOCUSED_CLIENT_CLICK(SC_FOCUS_POLICY(*ps), 1);
			FPS_RAISE_UNFOCUSED_CLIENT_CLICK(
				SF_FOCUS_POLICY(*ps), 1);
			FPS_RAISE_UNFOCUSED_CLIENT_CLICK(
				SM_FOCUS_POLICY(*ps), 1);
			FPS_RAISE_UNFOCUSED_CLIENT_CLICK(
				SC_FOCUS_POLICY(*ps), 1);
			FPS_RAISE_FOCUSED_DECOR_CLICK(SF_FOCUS_POLICY(*ps), 1);
			FPS_RAISE_FOCUSED_DECOR_CLICK(SM_FOCUS_POLICY(*ps), 1);
			FPS_RAISE_FOCUSED_DECOR_CLICK(SC_FOCUS_POLICY(*ps), 1);
			FPS_RAISE_UNFOCUSED_DECOR_CLICK(
				SF_FOCUS_POLICY(*ps), 1);
			FPS_RAISE_UNFOCUSED_DECOR_CLICK(
				SM_FOCUS_POLICY(*ps), 1);
			FPS_RAISE_UNFOCUSED_DECOR_CLICK(
				SC_FOCUS_POLICY(*ps), 1);
		}
		else if (StrEquals(token, "ClickToFocusRaisesOff"))
		{
			FPS_RAISE_FOCUSED_CLIENT_CLICK(SF_FOCUS_POLICY(*ps), 0);
			FPS_RAISE_FOCUSED_CLIENT_CLICK(SM_FOCUS_POLICY(*ps), 1);
			FPS_RAISE_FOCUSED_CLIENT_CLICK(SC_FOCUS_POLICY(*ps), 1);
			FPS_RAISE_UNFOCUSED_CLIENT_CLICK(
				SF_FOCUS_POLICY(*ps), 0);
			FPS_RAISE_UNFOCUSED_CLIENT_CLICK(
				SM_FOCUS_POLICY(*ps), 1);
			FPS_RAISE_UNFOCUSED_CLIENT_CLICK(
				SC_FOCUS_POLICY(*ps), 1);
			FPS_RAISE_FOCUSED_DECOR_CLICK(SF_FOCUS_POLICY(*ps), 0);
			FPS_RAISE_FOCUSED_DECOR_CLICK(SM_FOCUS_POLICY(*ps), 1);
			FPS_RAISE_FOCUSED_DECOR_CLICK(SC_FOCUS_POLICY(*ps), 1);
			FPS_RAISE_UNFOCUSED_DECOR_CLICK(
				SF_FOCUS_POLICY(*ps), 0);
			FPS_RAISE_UNFOCUSED_DECOR_CLICK(
				SM_FOCUS_POLICY(*ps), 1);
			FPS_RAISE_UNFOCUSED_DECOR_CLICK(
				SC_FOCUS_POLICY(*ps), 1);
		}
		else if (StrEquals(token, "CirculateSkip"))
		{
			SFSET_DO_CIRCULATE_SKIP(*ps, 1);
			SMSET_DO_CIRCULATE_SKIP(*ps, 1);
			SCSET_DO_CIRCULATE_SKIP(*ps, 1);
		}
		else if (StrEquals(token, "CirculateHit"))
		{
			SFSET_DO_CIRCULATE_SKIP(*ps, 0);
			SMSET_DO_CIRCULATE_SKIP(*ps, 1);
			SCSET_DO_CIRCULATE_SKIP(*ps, 1);
		}
		else
		{
			found = False;
		}
		break;

	case 'd':
		if (StrEquals(token, "DepressableBorder"))
		{
			SFSET_HAS_DEPRESSABLE_BORDER(*ps, 1);
			SMSET_HAS_DEPRESSABLE_BORDER(*ps, 1);
			SCSET_HAS_DEPRESSABLE_BORDER(*ps, 1);
		}
		else if (StrEquals(token, "DecorateTransient"))
		{
			ps->flags.do_decorate_transient = 1;
			ps->flag_mask.do_decorate_transient = 1;
			ps->change_mask.do_decorate_transient = 1;
		}
		else if (StrEquals(token, "DumbPlacement"))
		{
			ps->flags.placement_mode &= (~PLACE_SMART);
			ps->flag_mask.placement_mode |= PLACE_SMART;
			ps->change_mask.placement_mode |= PLACE_SMART;
		}
		else if (StrEquals(token, "DONTRAISETRANSIENT"))
		{
			SFSET_DO_RAISE_TRANSIENT(*ps, 0);
			SMSET_DO_RAISE_TRANSIENT(*ps, 1);
			SCSET_DO_RAISE_TRANSIENT(*ps, 1);
		}
		else if (StrEquals(token, "DONTLOWERTRANSIENT"))
		{
			SFSET_DO_LOWER_TRANSIENT(*ps, 0);
			SMSET_DO_LOWER_TRANSIENT(*ps, 1);
			SCSET_DO_LOWER_TRANSIENT(*ps, 1);
		}
		else if (StrEquals(token, "DontStackTransientParent"))
		{
			SFSET_DO_STACK_TRANSIENT_PARENT(*ps, 0);
			SMSET_DO_STACK_TRANSIENT_PARENT(*ps, 1);
			SCSET_DO_STACK_TRANSIENT_PARENT(*ps, 1);
		}
		else
		{
			found = False;
		}
		break;

	case 'e':
		if (StrEquals(token, "ExactWindowName"))
		{
			SFSET_USE_INDEXED_WINDOW_NAME(*ps, 0);
			SMSET_USE_INDEXED_WINDOW_NAME(*ps, 1);
			SCSET_USE_INDEXED_WINDOW_NAME(*ps, 1);
		}
		else if (StrEquals(token, "ExactIconName"))
		{
			SFSET_USE_INDEXED_ICON_NAME(*ps, 0);
			SMSET_USE_INDEXED_ICON_NAME(*ps, 1);
			SCSET_USE_INDEXED_ICON_NAME(*ps, 1);
		}
		else
		{
			found = EWMH_CMD_Style(token, ps);
		}
		break;

	case 'f':
		if (strncasecmp(token, "fp", 2) == 0)
		{
			/* parse focus policy options */
			found = style_parse_focus_policy_style(
				token + 2, rest, False,
				&SF_FOCUS_POLICY(*ps),
				&SM_FOCUS_POLICY(*ps),
				&SC_FOCUS_POLICY(*ps));
		}
		else if (StrEquals(token, "Font"))
		{
			SAFEFREE(SGET_WINDOW_FONT(*ps));
			GetNextToken(rest, &token);
			SSET_WINDOW_FONT(*ps, token);
			SFSET_HAS_WINDOW_FONT(*ps, (token != NULL));
			SMSET_HAS_WINDOW_FONT(*ps, 1);
			SCSET_HAS_WINDOW_FONT(*ps, 1);

		}
		else if (StrEquals(token, "ForeColor"))
		{
			GetNextToken(rest, &token);
			if (token)
			{
				SAFEFREE(SGET_FORE_COLOR_NAME(*ps));
				SSET_FORE_COLOR_NAME(*ps, token);
				ps->flags.has_color_fore = 1;
				ps->flag_mask.has_color_fore = 1;
				ps->change_mask.has_color_fore = 1;
				ps->flags.use_colorset = 0;
				ps->flag_mask.use_colorset = 1;
				ps->change_mask.use_colorset = 1;
			}
			else
			{
				fvwm_msg(
					ERR, "CMD_Style",
					"ForeColor Style needs color argument");
			}
		}
		else if (StrEquals(token, "FVWMBUTTONS"))
		{
			SFSET_HAS_MWM_BUTTONS(*ps, 0);
			SMSET_HAS_MWM_BUTTONS(*ps, 1);
			SCSET_HAS_MWM_BUTTONS(*ps, 1);
		}
		else if (StrEquals(token, "FVWMBORDER"))
		{
			SFSET_HAS_MWM_BORDER(*ps, 0);
			SMSET_HAS_MWM_BORDER(*ps, 1);
			SCSET_HAS_MWM_BORDER(*ps, 1);
		}
		else if (StrEquals(token, "FocusFollowsMouse"))
		{
			FPS_GRAB_FOCUS(SF_FOCUS_POLICY(*ps), 0);
			FPS_GRAB_FOCUS(SM_FOCUS_POLICY(*ps), 1);
			FPS_GRAB_FOCUS(SC_FOCUS_POLICY(*ps), 1);
		}
		else if (StrEquals(token, "FirmBorder"))
		{
			SFSET_HAS_DEPRESSABLE_BORDER(*ps, 0);
			SMSET_HAS_DEPRESSABLE_BORDER(*ps, 1);
			SCSET_HAS_DEPRESSABLE_BORDER(*ps, 1);
		}
		else if (StrEquals(token, "FixedPosition") ||
			 StrEquals(token, "FixedUSPosition"))
		{
			SFSET_IS_FIXED(*ps, 1);
			SMSET_IS_FIXED(*ps, 1);
			SCSET_IS_FIXED(*ps, 1);
		}
		else if (StrEquals(token, "FixedPPosition"))
		{
			SFSET_IS_FIXED_PPOS(*ps, 1);
			SMSET_IS_FIXED_PPOS(*ps, 1);
			SCSET_IS_FIXED_PPOS(*ps, 1);
		}
		else if (StrEquals(token, "FixedSize") ||
			 StrEquals(token, "FixedUSSize"))
		{
			SFSET_IS_SIZE_FIXED(*ps, 1);
			SMSET_IS_SIZE_FIXED(*ps, 1);
			SCSET_IS_SIZE_FIXED(*ps, 1);
		}
		else if (StrEquals(token, "FixedPSize"))
		{
			SFSET_IS_PSIZE_FIXED(*ps, 1);
			SMSET_IS_PSIZE_FIXED(*ps, 1);
			SCSET_IS_PSIZE_FIXED(*ps, 1);
		}
		else
		{
			found = False;
		}
		break;

	case 'g':
		if (StrEquals(token, "GrabFocusOff"))
		{
			FPS_GRAB_FOCUS(SF_FOCUS_POLICY(*ps), 0);
			FPS_GRAB_FOCUS(SM_FOCUS_POLICY(*ps), 1);
			FPS_GRAB_FOCUS(SC_FOCUS_POLICY(*ps), 1);
		}
		else if (StrEquals(token, "GrabFocus"))
		{
			FPS_GRAB_FOCUS(SF_FOCUS_POLICY(*ps), 1);
			FPS_GRAB_FOCUS(SM_FOCUS_POLICY(*ps), 1);
			FPS_GRAB_FOCUS(SC_FOCUS_POLICY(*ps), 1);
		}
		else if (StrEquals(token, "GrabFocusTransientOff"))
		{
			FPS_GRAB_FOCUS_TRANSIENT(SF_FOCUS_POLICY(*ps), 0);
			FPS_GRAB_FOCUS_TRANSIENT(SM_FOCUS_POLICY(*ps), 1);
			FPS_GRAB_FOCUS_TRANSIENT(SC_FOCUS_POLICY(*ps), 1);
		}
		else if (StrEquals(token, "GrabFocusTransient"))
		{
			FPS_GRAB_FOCUS_TRANSIENT(SF_FOCUS_POLICY(*ps), 1);
			FPS_GRAB_FOCUS_TRANSIENT(SM_FOCUS_POLICY(*ps), 1);
			FPS_GRAB_FOCUS_TRANSIENT(SC_FOCUS_POLICY(*ps), 1);
		}
		else if (StrEquals(token, "GNOMEIgnoreHints"))
		{
			SFSET_DO_IGNORE_GNOME_HINTS(*ps, 1);
			SMSET_DO_IGNORE_GNOME_HINTS(*ps, 1);
			SCSET_DO_IGNORE_GNOME_HINTS(*ps, 1);
		}
		else if (StrEquals(token, "GNOMEUseHints"))
		{
			SFSET_DO_IGNORE_GNOME_HINTS(*ps, 0);
			SMSET_DO_IGNORE_GNOME_HINTS(*ps, 1);
			SCSET_DO_IGNORE_GNOME_HINTS(*ps, 1);
		}
		else
		{
			found = False;
		}
		break;

	case 'h':
		if (StrEquals(token, "HintOverride"))
		{
			SFSET_HAS_MWM_OVERRIDE(*ps, 1);
			SMSET_HAS_MWM_OVERRIDE(*ps, 1);
			SCSET_HAS_MWM_OVERRIDE(*ps, 1);
		}
		else if (StrEquals(token, "Handles"))
		{
			ps->flags.has_no_handles = 0;
			ps->flag_mask.has_no_handles = 1;
			ps->change_mask.has_no_handles = 1;
		}
		else if (StrEquals(token, "HandleWidth"))
		{
			if (GetIntegerArguments(rest, NULL, val, 1))
			{
				SSET_HANDLE_WIDTH(*ps, (short)*val);
				ps->flags.has_handle_width = 1;
				ps->flag_mask.has_handle_width = 1;
				ps->change_mask.has_handle_width = 1;
			}
			else
			{
				fvwm_msg(
					ERR, "CMD_Style",
					"HandleWidth Style needs width"
					" argument");
			}
		}
		else if (StrEquals(token, "HilightFore"))
		{
			GetNextToken(rest, &token);
			if (token)
			{
				SAFEFREE(SGET_FORE_COLOR_NAME_HI(*ps));
				SSET_FORE_COLOR_NAME_HI(*ps, token);
				ps->flags.has_color_fore_hi = 1;
				ps->flag_mask.has_color_fore_hi = 1;
				ps->change_mask.has_color_fore_hi = 1;
				ps->flags.use_colorset_hi = 0;
				ps->flag_mask.use_colorset_hi = 1;
				ps->change_mask.use_colorset_hi = 1;
			}
			else
			{
				fvwm_msg(
					ERR, "CMD_Style",
					"HilightFore Style needs color"
					" argument");
			}
		}
		else if (StrEquals(token, "HilightBack"))
		{
			GetNextToken(rest, &token);
			if (token)
			{
				SAFEFREE(SGET_BACK_COLOR_NAME_HI(*ps));
				SSET_BACK_COLOR_NAME_HI(*ps, token);
				ps->flags.has_color_back_hi = 1;
				ps->flag_mask.has_color_back_hi = 1;
				ps->change_mask.has_color_back_hi = 1;
				ps->flags.use_colorset_hi = 0;
				ps->flag_mask.use_colorset_hi = 1;
				ps->change_mask.use_colorset_hi = 1;
			}
			else
			{
				fvwm_msg(
					ERR, "CMD_Style",
					"HilightBack Style needs color"
					" argument");
			}
		}
		else if (StrEquals(token, "HilightColorset"))
		{
			*val = -1;
			GetIntegerArguments(rest, NULL, val, 1);
			SSET_COLORSET_HI(*ps, *val);
			alloc_colorset(*val);
			ps->flags.use_colorset_hi = (*val >= 0);
			ps->flag_mask.use_colorset_hi = 1;
			ps->change_mask.use_colorset_hi = 1;
		}
		else if (StrEquals(token, "HilightBorderColorset"))
		{
			*val = -1;
			GetIntegerArguments(rest, NULL, val, 1);
			SSET_BORDER_COLORSET_HI(*ps, *val);
			alloc_colorset(*val);
			ps->flags.use_border_colorset_hi = (*val >= 0);
			ps->flag_mask.use_border_colorset_hi = 1;
			ps->change_mask.use_border_colorset_hi = 1;
		}
		else
		{
			found = False;
		}
		break;

	case 'i':
		if (StrEquals(token, "Icon"))
		{
			GetNextToken(rest, &token);

			SAFEFREE(SGET_ICON_NAME(*ps));
			SSET_ICON_NAME(*ps,token);
			ps->flags.has_icon = (token != NULL);
			ps->flag_mask.has_icon = 1;
			ps->change_mask.has_icon = 1;

			SFSET_IS_ICON_SUPPRESSED(*ps, 0);
			SMSET_IS_ICON_SUPPRESSED(*ps, 1);
			SCSET_IS_ICON_SUPPRESSED(*ps, 1);
		}
		else if (StrEquals(token, "IconFont"))
		{
			SAFEFREE(SGET_ICON_FONT(*ps));
			GetNextToken(rest, &token);
			SSET_ICON_FONT(*ps, token);
			SFSET_HAS_ICON_FONT(*ps, (token != NULL));
			SMSET_HAS_ICON_FONT(*ps, 1);
			SCSET_HAS_ICON_FONT(*ps, 1);

		}
		else if (StrEquals(token, "IconOverride"))
		{
			SFSET_ICON_OVERRIDE(*ps, ICON_OVERRIDE);
			SMSET_ICON_OVERRIDE(*ps, ICON_OVERRIDE_MASK);
			SCSET_ICON_OVERRIDE(*ps, ICON_OVERRIDE_MASK);
		}
		else if (StrEquals(token, "IgnoreRestack"))
		{
			SFSET_DO_IGNORE_RESTACK(*ps, 1);
			SMSET_DO_IGNORE_RESTACK(*ps, 1);
			SCSET_DO_IGNORE_RESTACK(*ps, 1);
		}
		else if (StrEquals(token, "IconTitle"))
		{
			SFSET_HAS_NO_ICON_TITLE(*ps, 0);
			SMSET_HAS_NO_ICON_TITLE(*ps, 1);
			SCSET_HAS_NO_ICON_TITLE(*ps, 1);
		}
		else if (StrEquals(token, "IconSize"))
		{
			style_parse_icon_size_style(token, rest, ps);
		}
		else if (StrEquals(token, "IconBox"))
		{
			style_parse_icon_box_style(cur_ib, token, rest, ps);
		} /* end iconbox parameter */
		else if (StrEquals(token, "ICONGRID"))
		{
			style_parse_icon_grid_style(token, rest, ps, *cur_ib);
		}
		else if (StrEquals(token, "ICONFILL"))
		{
			style_parse_icon_fill_style(token, rest, ps, *cur_ib);
		} /* end iconfill */
		else if (StrEquals(token, "IconifyWindowGroups"))
		{
			SFSET_DO_ICONIFY_WINDOW_GROUPS(*ps, 1);
			SMSET_DO_ICONIFY_WINDOW_GROUPS(*ps, 1);
			SCSET_DO_ICONIFY_WINDOW_GROUPS(*ps, 1);
		}
		else if (StrEquals(token, "IconifyWindowGroupsOff"))
		{
			SFSET_DO_ICONIFY_WINDOW_GROUPS(*ps, 0);
			SMSET_DO_ICONIFY_WINDOW_GROUPS(*ps, 1);
			SCSET_DO_ICONIFY_WINDOW_GROUPS(*ps, 1);
		}
		else if (StrEquals(token, "IndexedWindowName"))
		{
			SFSET_USE_INDEXED_WINDOW_NAME(*ps, 1);
			SMSET_USE_INDEXED_WINDOW_NAME(*ps, 1);
			SCSET_USE_INDEXED_WINDOW_NAME(*ps, 1);
		}
		else if (StrEquals(token, "IndexedIconName"))
		{
			SFSET_USE_INDEXED_ICON_NAME(*ps, 1);
			SMSET_USE_INDEXED_ICON_NAME(*ps, 1);
			SCSET_USE_INDEXED_ICON_NAME(*ps, 1);
		}
		else
		{
			found = False;
		}
		break;

	case 'j':
		if (0)
		{
		}
		else
		{
			found = False;
		}
		break;

	case 'k':
		if (StrEquals(token, "KeepWindowGroupsOnDesk"))
		{
			SFSET_DO_USE_WINDOW_GROUP_HINT(*ps, 1);
			SMSET_DO_USE_WINDOW_GROUP_HINT(*ps, 1);
			SCSET_DO_USE_WINDOW_GROUP_HINT(*ps, 1);
		}
		else
		{
			found = False;
		}
		break;

	case 'l':
		if (StrEquals(token, "LeftTitleRotatedCW"))
		{
			SFSET_IS_LEFT_TITLE_ROTATED_CW(*ps, 1);
			SMSET_IS_LEFT_TITLE_ROTATED_CW(*ps, 1);
			SCSET_IS_LEFT_TITLE_ROTATED_CW(*ps, 1);
		}
		else if (StrEquals(token, "LeftTitleRotatedCCW"))
		{
			SFSET_IS_LEFT_TITLE_ROTATED_CW(*ps, 0);
			SMSET_IS_LEFT_TITLE_ROTATED_CW(*ps, 1);
			SCSET_IS_LEFT_TITLE_ROTATED_CW(*ps, 1);
		}
		else if (StrEquals(token, "Lenience"))
		{
			FPS_LENIENT(SF_FOCUS_POLICY(*ps), 1);
			FPS_LENIENT(SM_FOCUS_POLICY(*ps), 1);
			FPS_LENIENT(SC_FOCUS_POLICY(*ps), 1);
		}
		else if (StrEquals(token, "Layer"))
		{
			*val = -1;
			if (GetIntegerArguments(rest, NULL, val, 1) && *val < 0)
			{
				fvwm_msg(ERR, "CMD_Style",
					 "Layer must be positive or zero.");
			}
			if (*val < 0)
			{
				SSET_LAYER(*ps, -9);
				/* mark layer unset */
				ps->flags.use_layer = 0;
				ps->flag_mask.use_layer = 1;
				ps->change_mask.use_layer = 1;
			}
			else
			{
				SSET_LAYER(*ps, *val);
				ps->flags.use_layer = 1;
				ps->flag_mask.use_layer = 1;
				ps->change_mask.use_layer = 1;
			}
		}
		else if (StrEquals(token, "LOWERTRANSIENT"))
		{
			SFSET_DO_LOWER_TRANSIENT(*ps, 1);
			SMSET_DO_LOWER_TRANSIENT(*ps, 1);
			SCSET_DO_LOWER_TRANSIENT(*ps, 1);
		}
		else
		{
			found = False;
		}
		break;

	case 'm':
		if (StrEquals(token, "ManualPlacement"))
		{
			ps->flags.placement_mode = PLACE_MANUAL;
			ps->flag_mask.placement_mode = PLACE_MASK;
			ps->change_mask.placement_mode = PLACE_MASK;
		}
		else if (StrEquals(token, "MANUALPLACEMENTHONORSSTARTSONPAGE"))
		{
			ps->flags.manual_placement_honors_starts_on_page = 1;
			ps->flag_mask.manual_placement_honors_starts_on_page =
				1;
			ps->change_mask.manual_placement_honors_starts_on_page =
				1;
		}
		else if (StrEquals(token, "MANUALPLACEMENTIGNORESSTARTSONPAGE"))
		{
			ps->flags.manual_placement_honors_starts_on_page = 0;
			ps->flag_mask.manual_placement_honors_starts_on_page =
				1;
			ps->change_mask.manual_placement_honors_starts_on_page =
				1;
		}
		else if (StrEquals(token, "MinOverlapPlacement"))
		{
			ps->flags.placement_mode = PLACE_MINOVERLAP;
			ps->flag_mask.placement_mode = PLACE_MASK;
			ps->change_mask.placement_mode = PLACE_MASK;
		}
		else if (StrEquals(token, "MinOverlapPercentPlacement"))
		{
			ps->flags.placement_mode = PLACE_MINOVERLAPPERCENT;
			ps->flag_mask.placement_mode = PLACE_MASK;
			ps->change_mask.placement_mode = PLACE_MASK;
		}
		else if (StrEquals(token, "MWMBUTTONS"))
		{
			SFSET_HAS_MWM_BUTTONS(*ps, 1);
			SMSET_HAS_MWM_BUTTONS(*ps, 1);
			SCSET_HAS_MWM_BUTTONS(*ps, 1);
		}
		else if (StrEquals(token, "MINIICON"))
		{
			if (!FMiniIconsSupported)
			{
				break;
			}
			GetNextToken(rest, &token);
			if (token)
			{
				SAFEFREE(SGET_MINI_ICON_NAME(*ps));
				SSET_MINI_ICON_NAME(*ps, token);
				ps->flags.has_mini_icon = 1;
				ps->flag_mask.has_mini_icon = 1;
				ps->change_mask.has_mini_icon = 1;
			}
			else
			{
				fvwm_msg(ERR, "CMD_Style",
					 "MiniIcon Style requires an Argument");
			}
		}
		else if (StrEquals(token, "MWMBORDER"))
		{
			SFSET_HAS_MWM_BORDER(*ps, 1);
			SMSET_HAS_MWM_BORDER(*ps, 1);
			SCSET_HAS_MWM_BORDER(*ps, 1);
		}
		else if (StrEquals(token, "MWMDECOR"))
		{
			ps->flags.has_mwm_decor = 1;
			ps->flag_mask.has_mwm_decor = 1;
			ps->change_mask.has_mwm_decor = 1;
		}
		else if (StrEquals(token, "MWMFUNCTIONS"))
		{
			ps->flags.has_mwm_functions = 1;
			ps->flag_mask.has_mwm_functions = 1;
			ps->change_mask.has_mwm_functions = 1;
		}
		else if (StrEquals(token, "MOUSEFOCUS"))
		{
			FPS_GRAB_FOCUS(SF_FOCUS_POLICY(*ps), 0);
			FPS_GRAB_FOCUS(SM_FOCUS_POLICY(*ps), 1);
			FPS_GRAB_FOCUS(SC_FOCUS_POLICY(*ps), 1);
			FPS_RELEASE_FOCUS(SF_FOCUS_POLICY(*ps), 0);
			FPS_RELEASE_FOCUS(SM_FOCUS_POLICY(*ps), 1);
			FPS_RELEASE_FOCUS(SC_FOCUS_POLICY(*ps), 1);
			FPS_FOCUS_ENTER(SF_FOCUS_POLICY(*ps), 1);
			FPS_FOCUS_ENTER(SM_FOCUS_POLICY(*ps), 1);
			FPS_FOCUS_ENTER(SC_FOCUS_POLICY(*ps), 1);
			FPS_UNFOCUS_LEAVE(SF_FOCUS_POLICY(*ps), 1);
			FPS_UNFOCUS_LEAVE(SM_FOCUS_POLICY(*ps), 1);
			FPS_UNFOCUS_LEAVE(SC_FOCUS_POLICY(*ps), 1);
			FPS_FOCUS_CLICK_CLIENT(SF_FOCUS_POLICY(*ps), 0);
			FPS_FOCUS_CLICK_CLIENT(SM_FOCUS_POLICY(*ps), 1);
			FPS_FOCUS_CLICK_CLIENT(SC_FOCUS_POLICY(*ps), 1);
			FPS_FOCUS_CLICK_DECOR(SF_FOCUS_POLICY(*ps), 0);
			FPS_FOCUS_CLICK_DECOR(SM_FOCUS_POLICY(*ps), 1);
			FPS_FOCUS_CLICK_DECOR(SC_FOCUS_POLICY(*ps), 1);
			FPS_FOCUS_CLICK_ICON(SF_FOCUS_POLICY(*ps), 0);
			FPS_FOCUS_CLICK_ICON(SM_FOCUS_POLICY(*ps), 1);
			FPS_FOCUS_CLICK_ICON(SC_FOCUS_POLICY(*ps), 1);
			FPS_RAISE_FOCUSED_CLIENT_CLICK(SF_FOCUS_POLICY(*ps), 0);
			FPS_RAISE_FOCUSED_CLIENT_CLICK(SM_FOCUS_POLICY(*ps), 1);
			FPS_RAISE_FOCUSED_CLIENT_CLICK(SC_FOCUS_POLICY(*ps), 1);
			FPS_RAISE_UNFOCUSED_CLIENT_CLICK(
				SF_FOCUS_POLICY(*ps), 0);
			FPS_RAISE_UNFOCUSED_CLIENT_CLICK(
				SM_FOCUS_POLICY(*ps), 1);
			FPS_RAISE_UNFOCUSED_CLIENT_CLICK(
				SC_FOCUS_POLICY(*ps), 1);
			FPS_RAISE_FOCUSED_DECOR_CLICK(SF_FOCUS_POLICY(*ps), 0);
			FPS_RAISE_FOCUSED_DECOR_CLICK(SM_FOCUS_POLICY(*ps), 1);
			FPS_RAISE_FOCUSED_DECOR_CLICK(SC_FOCUS_POLICY(*ps), 1);
			FPS_RAISE_UNFOCUSED_DECOR_CLICK(
				SF_FOCUS_POLICY(*ps), 0);
			FPS_RAISE_UNFOCUSED_DECOR_CLICK(
				SM_FOCUS_POLICY(*ps), 1);
			FPS_RAISE_UNFOCUSED_DECOR_CLICK(
				SC_FOCUS_POLICY(*ps), 1);
			FPS_PASS_FOCUS_CLICK(SF_FOCUS_POLICY(*ps), 1);
			FPS_PASS_FOCUS_CLICK(SM_FOCUS_POLICY(*ps), 1);
			FPS_PASS_FOCUS_CLICK(SC_FOCUS_POLICY(*ps), 1);
			FPS_PASS_RAISE_CLICK(SF_FOCUS_POLICY(*ps), 1);
			FPS_PASS_RAISE_CLICK(SM_FOCUS_POLICY(*ps), 1);
			FPS_PASS_RAISE_CLICK(SC_FOCUS_POLICY(*ps), 1);
			FPS_FOCUS_BY_PROGRAM(SF_FOCUS_POLICY(*ps), 1);
			FPS_FOCUS_BY_PROGRAM(SM_FOCUS_POLICY(*ps), 1);
			FPS_FOCUS_BY_PROGRAM(SC_FOCUS_POLICY(*ps), 1);
			FPS_ALLOW_FUNC_FOCUS_CLICK(SF_FOCUS_POLICY(*ps), 0);
			FPS_ALLOW_FUNC_FOCUS_CLICK(SM_FOCUS_POLICY(*ps), 1);
			FPS_ALLOW_FUNC_FOCUS_CLICK(SC_FOCUS_POLICY(*ps), 1);
			FPS_ALLOW_FUNC_RAISE_CLICK(SF_FOCUS_POLICY(*ps), 0);
			FPS_ALLOW_FUNC_RAISE_CLICK(SM_FOCUS_POLICY(*ps), 1);
			FPS_ALLOW_FUNC_RAISE_CLICK(SC_FOCUS_POLICY(*ps), 1);
			FPS_FOCUS_BY_FUNCTION(SF_FOCUS_POLICY(*ps), 1);
			FPS_FOCUS_BY_FUNCTION(SM_FOCUS_POLICY(*ps), 1);
			FPS_FOCUS_BY_FUNCTION(SC_FOCUS_POLICY(*ps), 1);
			FPS_WARP_POINTER_ON_FOCUS_FUNC(SF_FOCUS_POLICY(*ps), 1);
			FPS_WARP_POINTER_ON_FOCUS_FUNC(SM_FOCUS_POLICY(*ps), 1);
			FPS_WARP_POINTER_ON_FOCUS_FUNC(SC_FOCUS_POLICY(*ps), 1);
			FPS_SORT_WINDOWLIST_BY(
				SF_FOCUS_POLICY(*ps), FPOL_SORT_WL_BY_FOCUS);
			FPS_SORT_WINDOWLIST_BY(SM_FOCUS_POLICY(*ps), 1);
			FPS_SORT_WINDOWLIST_BY(SC_FOCUS_POLICY(*ps), 1);
		}
		else if (StrEquals(token, "MouseFocusClickRaises"))
		{
			FPS_RAISE_FOCUSED_CLIENT_CLICK(SF_FOCUS_POLICY(*ps), 1);
			FPS_RAISE_FOCUSED_CLIENT_CLICK(SM_FOCUS_POLICY(*ps), 1);
			FPS_RAISE_FOCUSED_CLIENT_CLICK(SC_FOCUS_POLICY(*ps), 1);
			FPS_RAISE_UNFOCUSED_CLIENT_CLICK(
				SF_FOCUS_POLICY(*ps), 1);
			FPS_RAISE_UNFOCUSED_CLIENT_CLICK(
				SM_FOCUS_POLICY(*ps), 1);
			FPS_RAISE_UNFOCUSED_CLIENT_CLICK(
				SC_FOCUS_POLICY(*ps), 1);
		}
		else if (StrEquals(token, "MouseFocusClickRaisesOff"))
		{
			FPS_RAISE_FOCUSED_CLIENT_CLICK(SF_FOCUS_POLICY(*ps), 0);
			FPS_RAISE_FOCUSED_CLIENT_CLICK(SM_FOCUS_POLICY(*ps), 1);
			FPS_RAISE_FOCUSED_CLIENT_CLICK(SC_FOCUS_POLICY(*ps), 1);
			FPS_RAISE_UNFOCUSED_CLIENT_CLICK(
				SF_FOCUS_POLICY(*ps), 0);
			FPS_RAISE_UNFOCUSED_CLIENT_CLICK(
				SM_FOCUS_POLICY(*ps), 1);
			FPS_RAISE_UNFOCUSED_CLIENT_CLICK(
				SC_FOCUS_POLICY(*ps), 1);
		}
		else if (StrEquals(token, "MouseFocusClickIgnoreMotion"))
		{
			FPS_IGNORE_RAISE_CLICK_MOTION(SF_FOCUS_POLICY(*ps), 1);
			FPS_IGNORE_RAISE_CLICK_MOTION(SM_FOCUS_POLICY(*ps), 1);
			FPS_IGNORE_RAISE_CLICK_MOTION(SC_FOCUS_POLICY(*ps), 1);
		}
		else if (StrEquals(token, "MouseFocusClickIgnoreMotionOff"))
		{
			FPS_IGNORE_RAISE_CLICK_MOTION(SF_FOCUS_POLICY(*ps), 0);
			FPS_IGNORE_RAISE_CLICK_MOTION(SM_FOCUS_POLICY(*ps), 1);
			FPS_IGNORE_RAISE_CLICK_MOTION(SC_FOCUS_POLICY(*ps), 1);
		}
		else if (StrEquals(token, "MAXWINDOWSIZE"))
		{
			int val1;
			int val2;
			int val1_unit;
			int val2_unit;

			num = GetTwoArguments(
				rest, &val1, &val2, &val1_unit, &val2_unit);
			if (num != 2)
			{
				val1 = DEFAULT_MAX_MAX_WINDOW_WIDTH;
				val2 = DEFAULT_MAX_MAX_WINDOW_HEIGHT;
			}
			else
			{
				val1 = val1 * val1_unit / 100;
				val2 = val2 * val2_unit / 100;
			}
			if (val1 < DEFAULT_MIN_MAX_WINDOW_WIDTH)
			{
				val1 = DEFAULT_MIN_MAX_WINDOW_WIDTH;
			}
			if (val1 > DEFAULT_MAX_MAX_WINDOW_WIDTH || val1 <= 0)
			{
				val1 = DEFAULT_MAX_MAX_WINDOW_WIDTH;
			}
			if (val2 < DEFAULT_MIN_MAX_WINDOW_HEIGHT)
			{
				val2 = DEFAULT_MIN_MAX_WINDOW_HEIGHT;
			}
			if (val2 > DEFAULT_MAX_MAX_WINDOW_HEIGHT || val2 <= 0)
			{
				val2 = DEFAULT_MAX_MAX_WINDOW_HEIGHT;
			}
			SSET_MAX_WINDOW_WIDTH(*ps, val1);
			SSET_MAX_WINDOW_HEIGHT(*ps, val2);
			ps->flags.has_max_window_size = 1;
			ps->flag_mask.has_max_window_size = 1;
			ps->change_mask.has_max_window_size = 1;
		}
		else
		{
			found = False;
		}
		break;

	case 'n':
		if (StrEquals(token, "NoActiveIconOverride"))
		{
			SFSET_ICON_OVERRIDE(*ps, NO_ACTIVE_ICON_OVERRIDE);
			SMSET_ICON_OVERRIDE(*ps, ICON_OVERRIDE_MASK);
			SCSET_ICON_OVERRIDE(*ps, ICON_OVERRIDE_MASK);
		}
		else if (StrEquals(token, "NoIconOverride"))
		{
			SFSET_ICON_OVERRIDE(*ps, NO_ICON_OVERRIDE);
			SMSET_ICON_OVERRIDE(*ps, ICON_OVERRIDE_MASK);
			SCSET_ICON_OVERRIDE(*ps, ICON_OVERRIDE_MASK);
		}
		else if (StrEquals(token, "NoIconTitle"))
		{
			SFSET_HAS_NO_ICON_TITLE(*ps, 1);
			SMSET_HAS_NO_ICON_TITLE(*ps, 1);
			SCSET_HAS_NO_ICON_TITLE(*ps, 1);
		}
		else if (StrEquals(token, "NOICON"))
		{
			SFSET_IS_ICON_SUPPRESSED(*ps, 1);
			SMSET_IS_ICON_SUPPRESSED(*ps, 1);
			SCSET_IS_ICON_SUPPRESSED(*ps, 1);
		}
		else if (StrEquals(token, "NOTITLE"))
		{
			ps->flags.has_no_title = 1;
			ps->flag_mask.has_no_title = 1;
			ps->change_mask.has_no_title = 1;
		}
		else if (StrEquals(token, "NoPPosition"))
		{
			ps->flags.use_no_pposition = 1;
			ps->flag_mask.use_no_pposition = 1;
			ps->change_mask.use_no_pposition = 1;
		}
		else if (StrEquals(token, "NoUSPosition"))
		{
			ps->flags.use_no_usposition = 1;
			ps->flag_mask.use_no_usposition = 1;
			ps->change_mask.use_no_usposition = 1;
		}
		else if (StrEquals(token, "NoTransientPPosition"))
		{
			ps->flags.use_no_transient_pposition = 1;
			ps->flag_mask.use_no_transient_pposition = 1;
			ps->change_mask.use_no_transient_pposition = 1;
		}
		else if (StrEquals(token, "NoTransientUSPosition"))
		{
			ps->flags.use_no_transient_usposition = 1;
			ps->flag_mask.use_no_transient_usposition = 1;
			ps->change_mask.use_no_transient_usposition = 1;
		}
		else if (StrEquals(token, "NoIconPosition"))
		{
			SFSET_USE_ICON_POSITION_HINT(*ps, 0);
			SMSET_USE_ICON_POSITION_HINT(*ps, 1);
			SCSET_USE_ICON_POSITION_HINT(*ps, 1);
		}
		else if (StrEquals(token, "NakedTransient"))
		{
			ps->flags.do_decorate_transient = 0;
			ps->flag_mask.do_decorate_transient = 1;
			ps->change_mask.do_decorate_transient = 1;
		}
		else if (StrEquals(token, "NODECORHINT"))
		{
			ps->flags.has_mwm_decor = 0;
			ps->flag_mask.has_mwm_decor = 1;
			ps->change_mask.has_mwm_decor = 1;
		}
		else if (StrEquals(token, "NOFUNCHINT"))
		{
			ps->flags.has_mwm_functions = 0;
			ps->flag_mask.has_mwm_functions = 1;
			ps->change_mask.has_mwm_functions = 1;
		}
		else if (StrEquals(token, "NOOVERRIDE"))
		{
			SFSET_HAS_MWM_OVERRIDE(*ps, 0);
			SMSET_HAS_MWM_OVERRIDE(*ps, 1);
			SCSET_HAS_MWM_OVERRIDE(*ps, 1);
		}
		else if (StrEquals(token, "NORESIZEOVERRIDE"))
		{
			SFSET_HAS_OVERRIDE_SIZE(*ps, 0);
			SMSET_HAS_OVERRIDE_SIZE(*ps, 1);
			SCSET_HAS_OVERRIDE_SIZE(*ps, 1);
		}
		else if (StrEquals(token, "NOHANDLES"))
		{
			ps->flags.has_no_handles = 1;
			ps->flag_mask.has_no_handles = 1;
			ps->change_mask.has_no_handles = 1;
		}
		else if (StrEquals(token, "NOLENIENCE"))
		{
			FPS_LENIENT(SF_FOCUS_POLICY(*ps), 0);
			FPS_LENIENT(SM_FOCUS_POLICY(*ps), 1);
			FPS_LENIENT(SC_FOCUS_POLICY(*ps), 1);
		}
		else if (StrEquals(token, "NoButton"))
		{
			butt = -1;
			GetIntegerArguments(rest, NULL, &butt, 1);
			butt = BUTTON_INDEX(butt);
			if (butt >= 0 && butt < NUMBER_OF_BUTTONS)
			{
				ps->flags.is_button_disabled |= (1 << butt);
				ps->flag_mask.is_button_disabled |= (1 << butt);
				ps->change_mask.is_button_disabled |=
					(1 << butt);
			}
			else
			{
				fvwm_msg(ERR, "CMD_Style",
					 "NoButton Style requires an argument");
			}
		}
		else if (StrEquals(token, "NOOLDECOR"))
		{
			ps->flags.has_ol_decor = 0;
			ps->flag_mask.has_ol_decor = 1;
			ps->change_mask.has_ol_decor = 1;
		}
		else if (StrEquals(token, "NEVERFOCUS"))
		{
			FPS_GRAB_FOCUS(SF_FOCUS_POLICY(*ps), 0);
			FPS_GRAB_FOCUS(SM_FOCUS_POLICY(*ps), 1);
			FPS_GRAB_FOCUS(SC_FOCUS_POLICY(*ps), 1);
			FPS_RELEASE_FOCUS(SF_FOCUS_POLICY(*ps), 0);
			FPS_RELEASE_FOCUS(SM_FOCUS_POLICY(*ps), 1);
			FPS_RELEASE_FOCUS(SC_FOCUS_POLICY(*ps), 1);
			FPS_FOCUS_ENTER(SF_FOCUS_POLICY(*ps), 0);
			FPS_FOCUS_ENTER(SM_FOCUS_POLICY(*ps), 1);
			FPS_FOCUS_ENTER(SC_FOCUS_POLICY(*ps), 1);
			FPS_UNFOCUS_LEAVE(SF_FOCUS_POLICY(*ps), 0);
			FPS_UNFOCUS_LEAVE(SM_FOCUS_POLICY(*ps), 1);
			FPS_UNFOCUS_LEAVE(SC_FOCUS_POLICY(*ps), 1);
			FPS_FOCUS_CLICK_CLIENT(SF_FOCUS_POLICY(*ps), 0);
			FPS_FOCUS_CLICK_CLIENT(SM_FOCUS_POLICY(*ps), 1);
			FPS_FOCUS_CLICK_CLIENT(SC_FOCUS_POLICY(*ps), 1);
			FPS_FOCUS_CLICK_DECOR(SF_FOCUS_POLICY(*ps), 0);
			FPS_FOCUS_CLICK_DECOR(SM_FOCUS_POLICY(*ps), 1);
			FPS_FOCUS_CLICK_DECOR(SC_FOCUS_POLICY(*ps), 1);
			FPS_FOCUS_CLICK_ICON(SF_FOCUS_POLICY(*ps), 0);
			FPS_FOCUS_CLICK_ICON(SM_FOCUS_POLICY(*ps), 1);
			FPS_FOCUS_CLICK_ICON(SC_FOCUS_POLICY(*ps), 1);
			FPS_RAISE_FOCUSED_CLIENT_CLICK(SF_FOCUS_POLICY(*ps), 0);
			FPS_RAISE_FOCUSED_CLIENT_CLICK(SM_FOCUS_POLICY(*ps), 1);
			FPS_RAISE_FOCUSED_CLIENT_CLICK(SC_FOCUS_POLICY(*ps), 1);
			FPS_RAISE_UNFOCUSED_CLIENT_CLICK(
				SF_FOCUS_POLICY(*ps), 0);
			FPS_RAISE_UNFOCUSED_CLIENT_CLICK(
				SM_FOCUS_POLICY(*ps), 1);
			FPS_RAISE_UNFOCUSED_CLIENT_CLICK(
				SC_FOCUS_POLICY(*ps), 1);
			FPS_RAISE_FOCUSED_DECOR_CLICK(SF_FOCUS_POLICY(*ps), 0);
			FPS_RAISE_FOCUSED_DECOR_CLICK(SM_FOCUS_POLICY(*ps), 1);
			FPS_RAISE_FOCUSED_DECOR_CLICK(SC_FOCUS_POLICY(*ps), 1);
			FPS_RAISE_UNFOCUSED_DECOR_CLICK(
				SF_FOCUS_POLICY(*ps), 0);
			FPS_RAISE_UNFOCUSED_DECOR_CLICK(
				SM_FOCUS_POLICY(*ps), 1);
			FPS_RAISE_UNFOCUSED_DECOR_CLICK(
				SC_FOCUS_POLICY(*ps), 1);
			FPS_PASS_FOCUS_CLICK(SF_FOCUS_POLICY(*ps), 1);
			FPS_PASS_FOCUS_CLICK(SM_FOCUS_POLICY(*ps), 1);
			FPS_PASS_FOCUS_CLICK(SC_FOCUS_POLICY(*ps), 1);
			FPS_PASS_RAISE_CLICK(SF_FOCUS_POLICY(*ps), 1);
			FPS_PASS_RAISE_CLICK(SM_FOCUS_POLICY(*ps), 1);
			FPS_PASS_RAISE_CLICK(SC_FOCUS_POLICY(*ps), 1);
			FPS_FOCUS_BY_PROGRAM(SF_FOCUS_POLICY(*ps), 1);
			FPS_FOCUS_BY_PROGRAM(SM_FOCUS_POLICY(*ps), 1);
			FPS_FOCUS_BY_PROGRAM(SC_FOCUS_POLICY(*ps), 1);
			FPS_ALLOW_FUNC_FOCUS_CLICK(SF_FOCUS_POLICY(*ps), 1);
			FPS_ALLOW_FUNC_FOCUS_CLICK(SM_FOCUS_POLICY(*ps), 1);
			FPS_ALLOW_FUNC_FOCUS_CLICK(SC_FOCUS_POLICY(*ps), 1);
			FPS_ALLOW_FUNC_RAISE_CLICK(SF_FOCUS_POLICY(*ps), 1);
			FPS_ALLOW_FUNC_RAISE_CLICK(SM_FOCUS_POLICY(*ps), 1);
			FPS_ALLOW_FUNC_RAISE_CLICK(SC_FOCUS_POLICY(*ps), 1);
			FPS_FOCUS_BY_FUNCTION(SF_FOCUS_POLICY(*ps), 0);
			FPS_FOCUS_BY_FUNCTION(SM_FOCUS_POLICY(*ps), 1);
			FPS_FOCUS_BY_FUNCTION(SC_FOCUS_POLICY(*ps), 1);
			FPS_WARP_POINTER_ON_FOCUS_FUNC(SF_FOCUS_POLICY(*ps), 0);
			FPS_WARP_POINTER_ON_FOCUS_FUNC(SM_FOCUS_POLICY(*ps), 1);
			FPS_WARP_POINTER_ON_FOCUS_FUNC(SC_FOCUS_POLICY(*ps), 1);
			FPS_SORT_WINDOWLIST_BY(
				SF_FOCUS_POLICY(*ps), FPOL_SORT_WL_BY_FOCUS);
			FPS_SORT_WINDOWLIST_BY(SM_FOCUS_POLICY(*ps), 1);
			FPS_SORT_WINDOWLIST_BY(SC_FOCUS_POLICY(*ps), 1);
		}
		else if (StrEquals(token, "NoBorder"))
		{
			SFSET_HAS_NO_BORDER(*ps, 1);
			SMSET_HAS_NO_BORDER(*ps, 1);
			SCSET_HAS_NO_BORDER(*ps, 1);
		}
		else
		{
			found = False;
		}
		break;

	case 'o':
		if (StrEquals(token, "OLDECOR"))
		{
			ps->flags.has_ol_decor = 1;
			ps->flag_mask.has_ol_decor = 1;
			ps->change_mask.has_ol_decor = 1;
		}
		else if (StrEquals(token, "Opacity"))
		{
			ps->flags.use_parent_relative = 0;
			ps->flag_mask.use_parent_relative = 1;
			ps->change_mask.use_parent_relative = 1;
		}
		else
		{
			found = False;
		}
		break;

	case 'p':
		if (StrEquals(token, "ParentalRelativity"))
		{
			ps->flags.use_parent_relative = 1;
			ps->flag_mask.use_parent_relative = 1;
			ps->change_mask.use_parent_relative = 1;
		}
		else if (StrEquals(token, "PlacementOverlapPenalties"))
		{
			float f[6] = {-1, -1, -1, -1, -1, -1};
			Bool bad = False;

			num = 0;
			if (rest != NULL)
			{
				num = sscanf(
					rest, "%f %f %f %f %f %f", &f[0],
					&f[1], &f[2], &f[3], &f[4], &f[5]);
				for (i=0; i < num; i++)
				{
					if (f[i] < 0)
						bad = True;
				}
			}
			if (bad)
			{
				fvwm_msg(
					ERR, "CMD_Style",
					"Bad argument to"
					" PlacementOverlapPenalties: %s", rest);
				break;
			}
			SSET_NORMAL_PLACEMENT_PENALTY(*ps, 1);
			SSET_ONTOP_PLACEMENT_PENALTY(
				*ps, PLACEMENT_AVOID_ONTOP);
			SSET_ICON_PLACEMENT_PENALTY(*ps, PLACEMENT_AVOID_ICON);
			SSET_STICKY_PLACEMENT_PENALTY(
				*ps, PLACEMENT_AVOID_STICKY);
			SSET_BELOW_PLACEMENT_PENALTY(
				*ps, PLACEMENT_AVOID_BELOW);
			SSET_EWMH_STRUT_PLACEMENT_PENALTY(
				*ps, PLACEMENT_AVOID_EWMH_STRUT);
			for (i=0; i < num; i++)
			{
				(*ps).placement_penalty[i] = f[i];
			}
			ps->flags.has_placement_penalty = 1;
			ps->flag_mask.has_placement_penalty = 1;
			ps->change_mask.has_placement_penalty = 1;
		}
		else if (StrEquals(token, "PlacementOverlapPercentPenalties"))
		{
			Bool bad = False;

			num = GetIntegerArguments(rest, NULL, val, 4);
			for (i=0; i < num; i++)
			{
				if (val[i] < 0)
					bad = True;
			}
			if (bad)
			{
				fvwm_msg(
					ERR, "CMD_Style",
					"Bad argument to PlacementOverlap"
					"PercentagePenalties: %s", rest);
				break;
			}
			SSET_99_PLACEMENT_PERCENTAGE_PENALTY(
				*ps, PLACEMENT_AVOID_COVER_99);
			SSET_95_PLACEMENT_PERCENTAGE_PENALTY(
				*ps, PLACEMENT_AVOID_COVER_95);
			SSET_85_PLACEMENT_PERCENTAGE_PENALTY(
				*ps, PLACEMENT_AVOID_COVER_85);
			SSET_75_PLACEMENT_PERCENTAGE_PENALTY(
				*ps, PLACEMENT_AVOID_COVER_75);
			for (i=0; i < num; i++)
			{
				(*ps).placement_percentage_penalty[i] = val[i];
			}
			ps->flags.has_placement_percentage_penalty = 1;
			ps->flag_mask.has_placement_percentage_penalty = 1;
			ps->change_mask.has_placement_percentage_penalty = 1;
		}
		else
		{
			found = False;
		}
		break;

	case 'q':
		if (0)
		{
		}
		else
		{
			found = False;
		}
		break;

	case 'r':
		if (StrEquals(token, "RAISETRANSIENT"))
		{
			SFSET_DO_RAISE_TRANSIENT(*ps, 1);
			SMSET_DO_RAISE_TRANSIENT(*ps, 1);
			SCSET_DO_RAISE_TRANSIENT(*ps, 1);
		}
		else if (StrEquals(token, "RANDOMPLACEMENT"))
		{
			ps->flags.placement_mode |= PLACE_RANDOM;
			ps->flag_mask.placement_mode |= PLACE_RANDOM;
			ps->change_mask.placement_mode |= PLACE_RANDOM;
		}
		else if (StrEquals(token, "RECAPTUREHONORSSTARTSONPAGE"))
		{
			ps->flags.recapture_honors_starts_on_page = 1;
			ps->flag_mask.recapture_honors_starts_on_page = 1;
			ps->change_mask.recapture_honors_starts_on_page = 1;
		}
		else if (StrEquals(token, "RECAPTUREIGNORESSTARTSONPAGE"))
		{
			ps->flags.recapture_honors_starts_on_page = 0;
			ps->flag_mask.recapture_honors_starts_on_page = 1;
			ps->change_mask.recapture_honors_starts_on_page = 1;
		}
		else if (StrEquals(token, "RESIZEHINTOVERRIDE"))
		{
			SFSET_HAS_OVERRIDE_SIZE(*ps, 1);
			SMSET_HAS_OVERRIDE_SIZE(*ps, 1);
			SCSET_HAS_OVERRIDE_SIZE(*ps, 1);
		}
		else if (StrEquals(token, "ResizeOpaque"))
		{
			SFSET_DO_RESIZE_OPAQUE(*ps, 1);
			SMSET_DO_RESIZE_OPAQUE(*ps, 1);
			SCSET_DO_RESIZE_OPAQUE(*ps, 1);
		}
		else if (StrEquals(token, "ResizeOutline"))
		{
			SFSET_DO_RESIZE_OPAQUE(*ps, 0);
			SMSET_DO_RESIZE_OPAQUE(*ps, 1);
			SCSET_DO_RESIZE_OPAQUE(*ps, 1);
		}
		else if (StrEquals(token, "RightTitleRotatedCW"))
		{
			SFSET_IS_RIGHT_TITLE_ROTATED_CW(*ps, 1);
			SMSET_IS_RIGHT_TITLE_ROTATED_CW(*ps, 1);
			SCSET_IS_RIGHT_TITLE_ROTATED_CW(*ps, 1);
		}
		else if (StrEquals(token, "RightTitleRotatedCCW"))
		{
			SFSET_IS_RIGHT_TITLE_ROTATED_CW(*ps, 0);
			SMSET_IS_RIGHT_TITLE_ROTATED_CW(*ps, 1);
			SCSET_IS_RIGHT_TITLE_ROTATED_CW(*ps, 1);
		}
		else
		{
			found = False;
		}
		break;

	case 's':
		if (StrEquals(token, "SMARTPLACEMENT"))
		{
			ps->flags.placement_mode |= PLACE_SMART;
			ps->flag_mask.placement_mode |= PLACE_SMART;
			ps->change_mask.placement_mode |= PLACE_SMART;
		}
		else if (StrEquals(token, "SkipMapping"))
		{
			SFSET_DO_NOT_SHOW_ON_MAP(*ps, 1);
			SMSET_DO_NOT_SHOW_ON_MAP(*ps, 1);
			SCSET_DO_NOT_SHOW_ON_MAP(*ps, 1);
		}
		else if (StrEquals(token, "ShowMapping"))
		{
			SFSET_DO_NOT_SHOW_ON_MAP(*ps, 0);
			SMSET_DO_NOT_SHOW_ON_MAP(*ps, 1);
			SCSET_DO_NOT_SHOW_ON_MAP(*ps, 1);
		}
		else if (StrEquals(token, "StackTransientParent"))
		{
			SFSET_DO_STACK_TRANSIENT_PARENT(*ps, 1);
			SMSET_DO_STACK_TRANSIENT_PARENT(*ps, 1);
			SCSET_DO_STACK_TRANSIENT_PARENT(*ps, 1);
		}
		else if (StrEquals(token, "StickyIcon"))
		{
			SFSET_IS_ICON_STICKY(*ps, 1);
			SMSET_IS_ICON_STICKY(*ps, 1);
			SCSET_IS_ICON_STICKY(*ps, 1);
		}
		else if (StrEquals(token, "SlipperyIcon"))
		{
			SFSET_IS_ICON_STICKY(*ps, 0);
			SMSET_IS_ICON_STICKY(*ps, 1);
			SCSET_IS_ICON_STICKY(*ps, 1);
		}
		else if (StrEquals(token, "SLOPPYFOCUS"))
		{
			FPS_GRAB_FOCUS(SF_FOCUS_POLICY(*ps), 0);
			FPS_GRAB_FOCUS(SM_FOCUS_POLICY(*ps), 1);
			FPS_GRAB_FOCUS(SC_FOCUS_POLICY(*ps), 1);
			FPS_RELEASE_FOCUS(SF_FOCUS_POLICY(*ps), 1);
			FPS_RELEASE_FOCUS(SM_FOCUS_POLICY(*ps), 1);
			FPS_RELEASE_FOCUS(SC_FOCUS_POLICY(*ps), 1);
			FPS_FOCUS_ENTER(SF_FOCUS_POLICY(*ps), 1);
			FPS_FOCUS_ENTER(SM_FOCUS_POLICY(*ps), 1);
			FPS_FOCUS_ENTER(SC_FOCUS_POLICY(*ps), 1);
			FPS_UNFOCUS_LEAVE(SF_FOCUS_POLICY(*ps), 0);
			FPS_UNFOCUS_LEAVE(SM_FOCUS_POLICY(*ps), 1);
			FPS_UNFOCUS_LEAVE(SC_FOCUS_POLICY(*ps), 1);
			FPS_FOCUS_CLICK_CLIENT(SF_FOCUS_POLICY(*ps), 0);
			FPS_FOCUS_CLICK_CLIENT(SM_FOCUS_POLICY(*ps), 1);
			FPS_FOCUS_CLICK_CLIENT(SC_FOCUS_POLICY(*ps), 1);
			FPS_FOCUS_CLICK_DECOR(SF_FOCUS_POLICY(*ps), 0);
			FPS_FOCUS_CLICK_DECOR(SM_FOCUS_POLICY(*ps), 1);
			FPS_FOCUS_CLICK_DECOR(SC_FOCUS_POLICY(*ps), 1);
			FPS_FOCUS_CLICK_ICON(SF_FOCUS_POLICY(*ps), 0);
			FPS_FOCUS_CLICK_ICON(SM_FOCUS_POLICY(*ps), 1);
			FPS_FOCUS_CLICK_ICON(SC_FOCUS_POLICY(*ps), 1);
			FPS_RAISE_FOCUSED_CLIENT_CLICK(SF_FOCUS_POLICY(*ps), 0);
			FPS_RAISE_FOCUSED_CLIENT_CLICK(SM_FOCUS_POLICY(*ps), 1);
			FPS_RAISE_FOCUSED_CLIENT_CLICK(SC_FOCUS_POLICY(*ps), 1);
			FPS_RAISE_UNFOCUSED_CLIENT_CLICK(
				SF_FOCUS_POLICY(*ps), 0);
			FPS_RAISE_UNFOCUSED_CLIENT_CLICK(
				SM_FOCUS_POLICY(*ps), 1);
			FPS_RAISE_UNFOCUSED_CLIENT_CLICK(
				SC_FOCUS_POLICY(*ps), 1);
			FPS_RAISE_FOCUSED_DECOR_CLICK(SF_FOCUS_POLICY(*ps), 0);
			FPS_RAISE_FOCUSED_DECOR_CLICK(SM_FOCUS_POLICY(*ps), 1);
			FPS_RAISE_FOCUSED_DECOR_CLICK(SC_FOCUS_POLICY(*ps), 1);
			FPS_RAISE_UNFOCUSED_DECOR_CLICK(
				SF_FOCUS_POLICY(*ps), 0);
			FPS_RAISE_UNFOCUSED_DECOR_CLICK(
				SM_FOCUS_POLICY(*ps), 1);
			FPS_RAISE_UNFOCUSED_DECOR_CLICK(
				SC_FOCUS_POLICY(*ps), 1);
			FPS_PASS_FOCUS_CLICK(SF_FOCUS_POLICY(*ps), 1);
			FPS_PASS_FOCUS_CLICK(SM_FOCUS_POLICY(*ps), 1);
			FPS_PASS_FOCUS_CLICK(SC_FOCUS_POLICY(*ps), 1);
			FPS_PASS_RAISE_CLICK(SF_FOCUS_POLICY(*ps), 1);
			FPS_PASS_RAISE_CLICK(SM_FOCUS_POLICY(*ps), 1);
			FPS_PASS_RAISE_CLICK(SC_FOCUS_POLICY(*ps), 1);
			FPS_FOCUS_BY_PROGRAM(SF_FOCUS_POLICY(*ps), 1);
			FPS_FOCUS_BY_PROGRAM(SM_FOCUS_POLICY(*ps), 1);
			FPS_FOCUS_BY_PROGRAM(SC_FOCUS_POLICY(*ps), 1);
			FPS_ALLOW_FUNC_FOCUS_CLICK(SF_FOCUS_POLICY(*ps), 0);
			FPS_ALLOW_FUNC_FOCUS_CLICK(SM_FOCUS_POLICY(*ps), 1);
			FPS_ALLOW_FUNC_FOCUS_CLICK(SC_FOCUS_POLICY(*ps), 1);
			FPS_ALLOW_FUNC_RAISE_CLICK(SF_FOCUS_POLICY(*ps), 0);
			FPS_ALLOW_FUNC_RAISE_CLICK(SM_FOCUS_POLICY(*ps), 1);
			FPS_ALLOW_FUNC_RAISE_CLICK(SC_FOCUS_POLICY(*ps), 1);
			FPS_FOCUS_BY_FUNCTION(SF_FOCUS_POLICY(*ps), 1);
			FPS_FOCUS_BY_FUNCTION(SM_FOCUS_POLICY(*ps), 1);
			FPS_FOCUS_BY_FUNCTION(SC_FOCUS_POLICY(*ps), 1);
			FPS_WARP_POINTER_ON_FOCUS_FUNC(SF_FOCUS_POLICY(*ps), 1);
			FPS_WARP_POINTER_ON_FOCUS_FUNC(SM_FOCUS_POLICY(*ps), 1);
			FPS_WARP_POINTER_ON_FOCUS_FUNC(SC_FOCUS_POLICY(*ps), 1);
			FPS_SORT_WINDOWLIST_BY(
				SF_FOCUS_POLICY(*ps), FPOL_SORT_WL_BY_FOCUS);
			FPS_SORT_WINDOWLIST_BY(SM_FOCUS_POLICY(*ps), 1);
			FPS_SORT_WINDOWLIST_BY(SC_FOCUS_POLICY(*ps), 1);
		}
		else if (StrEquals(token, "StartIconic"))
		{
			ps->flags.do_start_iconic = 1;
			ps->flag_mask.do_start_iconic = 1;
			ps->change_mask.do_start_iconic = 1;
		}
		else if (StrEquals(token, "StartNormal"))
		{
			ps->flags.do_start_iconic = 0;
			ps->flag_mask.do_start_iconic = 1;
			ps->change_mask.do_start_iconic = 1;
		}
		else if (StrEquals(token, "StaysOnBottom"))
		{
			SSET_LAYER(*ps, Scr.BottomLayer);
			ps->flags.use_layer = 1;
			ps->flag_mask.use_layer = 1;
			ps->change_mask.use_layer = 1;
		}
		else if (StrEquals(token, "StaysOnTop"))
		{
			SSET_LAYER(*ps, Scr.TopLayer);
			ps->flags.use_layer = 1;
			ps->flag_mask.use_layer = 1;
			ps->change_mask.use_layer = 1;
		}
		else if (StrEquals(token, "StaysPut"))
		{
			SSET_LAYER(*ps, Scr.DefaultLayer);
			ps->flags.use_layer = 1;
			ps->flag_mask.use_layer = 1;
			ps->change_mask.use_layer = 1;
		}
		else if (StrEquals(token, "Sticky"))
		{
			SFSET_IS_STICKY(*ps, 1);
			SMSET_IS_STICKY(*ps, 1);
			SCSET_IS_STICKY(*ps, 1);
		}
		else if (StrEquals(token, "Slippery"))
		{
			SFSET_IS_STICKY(*ps, 0);
			SMSET_IS_STICKY(*ps, 1);
			SCSET_IS_STICKY(*ps, 1);
		}
		else if (StrEquals(token, "STARTSONDESK"))
		{
			spargs = GetIntegerArguments(rest, NULL, tmpno, 1);
			if (spargs == 1)
			{
				ps->flags.use_start_on_desk = 1;
				ps->flag_mask.use_start_on_desk = 1;
				ps->change_mask.use_start_on_desk = 1;
				/*  RBW - 11/20/1998 - allow for the special
				 * case of -1  */
				SSET_START_DESK(
					*ps, (tmpno[0] > -1) ?
					tmpno[0] + 1 : tmpno[0]);
			}
			else
			{
				fvwm_msg(ERR,"CMD_Style",
					 "bad StartsOnDesk arg: %s", rest);
			}
		}
		/* StartsOnPage is like StartsOnDesk-Plus */
		else if (StrEquals(token, "STARTSONPAGE"))
		{
			spargs = GetIntegerArguments(rest, NULL, tmpno, 3);
			if (spargs == 1 || spargs == 3)
			{
				/* We have a desk no., with or without page. */
				/* RBW - 11/20/1998 - allow for the special
				 * case of -1 */
				/* Desk is now actual + 1 */
				SSET_START_DESK(
					*ps, (tmpno[0] > -1) ?
					tmpno[0] + 1 : tmpno[0]);
			}
			if (spargs == 2 || spargs == 3)
			{
				if (spargs == 3)
				{
					/*  RBW - 11/20/1998 - allow for the
					 * special case of -1  */
					SSET_START_PAGE_X(
						*ps, (tmpno[1] > -1) ?
						tmpno[1] + 1 : tmpno[1]);
					SSET_START_PAGE_Y(
						*ps, (tmpno[2] > -1) ?
						tmpno[2] + 1 : tmpno[2]);
				}
				else
				{
					SSET_START_PAGE_X(
						*ps, (tmpno[0] > -1) ?
						tmpno[0] + 1 : tmpno[0]);
					SSET_START_PAGE_Y(
						*ps, (tmpno[1] > -1) ?
						tmpno[1] + 1 : tmpno[1]);
				}
			}
			if (spargs < 1 || spargs > 3)
			{
				fvwm_msg(ERR, "CMD_Style",
					 "bad StartsOnPage args: %s", rest);
			}
			else
			{
				ps->flags.use_start_on_desk = 1;
				ps->flag_mask.use_start_on_desk = 1;
				ps->change_mask.use_start_on_desk = 1;
			}
		}
		else if (StrEquals(token, "STARTSONPAGEINCLUDESTRANSIENTS"))
		{
			ps->flags.use_start_on_page_for_transient = 1;
			ps->flag_mask.use_start_on_page_for_transient = 1;
			ps->change_mask.use_start_on_page_for_transient = 1;
		}
		else if (StrEquals(token, "STARTSONPAGEIGNORESTRANSIENTS"))
		{
			ps->flags.use_start_on_page_for_transient = 0;
			ps->flag_mask.use_start_on_page_for_transient = 1;
			ps->change_mask.use_start_on_page_for_transient = 1;
		}
		else if (StrEquals(token, "StartsOnScreen"))
		{
			if (rest)
			{
				tmpno[0] = FScreenGetScreenArgument(rest, 'c');
				ps->flags.use_start_on_screen = 1;
				ps->flag_mask.use_start_on_screen = 1;
				ps->change_mask.use_start_on_screen = 1;
				SSET_START_SCREEN(*ps, tmpno[0]);
			}
			else
			{
				ps->flags.use_start_on_screen = 0;
				ps->flag_mask.use_start_on_screen = 1;
				ps->change_mask.use_start_on_screen = 1;
			}
		}
		else if (StrEquals(token, "STARTSANYWHERE"))
		{
			ps->flags.use_start_on_desk = 0;
			ps->flag_mask.use_start_on_desk = 1;
			ps->change_mask.use_start_on_desk = 1;
		}
		else if (StrEquals(token, "STARTSLOWERED"))
		{
			ps->flags.do_start_lowered = 1;
			ps->flag_mask.do_start_lowered = 1;
			ps->change_mask.do_start_lowered = 1;
		}
		else if (StrEquals(token, "STARTSRAISED"))
		{
			ps->flags.do_start_lowered = 0;
			ps->flag_mask.do_start_lowered = 1;
			ps->change_mask.do_start_lowered = 1;
		}
		else if (StrEquals(token, "SaveUnder"))
		{
			ps->flags.do_save_under = 1;
			ps->flag_mask.do_save_under = 1;
			ps->change_mask.do_save_under = 1;
		}
		else if (StrEquals(token, "SaveUnderOff"))
		{
			ps->flags.do_save_under = 0;
			ps->flag_mask.do_save_under = 1;
			ps->change_mask.do_save_under = 1;
		}
		else if (StrEquals(token, "StippledTitle"))
		{
			SFSET_HAS_STIPPLED_TITLE(*ps, 1);
			SMSET_HAS_STIPPLED_TITLE(*ps, 1);
			SCSET_HAS_STIPPLED_TITLE(*ps, 1);
		}
		else if (StrEquals(token, "StippledTitleOff"))
		{
			SFSET_HAS_STIPPLED_TITLE(*ps, 0);
			SMSET_HAS_STIPPLED_TITLE(*ps, 1);
			SCSET_HAS_STIPPLED_TITLE(*ps, 1);
		}
		else if (StrEquals(token, "ScatterWindowGroups"))
		{
			SFSET_DO_USE_WINDOW_GROUP_HINT(*ps, 0);
			SMSET_DO_USE_WINDOW_GROUP_HINT(*ps, 1);
			SCSET_DO_USE_WINDOW_GROUP_HINT(*ps, 1);
		}
		else
		{
			found = False;
		}
		break;

	case 't':
		if (StrEquals(token, "TileCascadePlacement"))
		{
			ps->flags.placement_mode = PLACE_TILECASCADE;
			ps->flag_mask.placement_mode = PLACE_MASK;
			ps->change_mask.placement_mode = PLACE_MASK;
		}
		else if (StrEquals(token, "TileManualPlacement"))
		{
			ps->flags.placement_mode = PLACE_TILEMANUAL;
			ps->flag_mask.placement_mode = PLACE_MASK;
			ps->change_mask.placement_mode = PLACE_MASK;
		}
		else if (StrEquals(token, "Title"))
		{
			ps->flags.has_no_title = 0;
			ps->flag_mask.has_no_title = 1;
			ps->change_mask.has_no_title = 1;
		}
		else if (StrEquals(token, "TitleAtBottom"))
		{
			SFSET_TITLE_DIR(*ps, DIR_S);
			SMSET_TITLE_DIR(*ps, 1);
			SCSET_TITLE_DIR(*ps, 1);
		}
		else if (StrEquals(token, "TitleAtTop"))
		{
			SFSET_TITLE_DIR(*ps, DIR_N);
			SMSET_TITLE_DIR(*ps, 1);
			SCSET_TITLE_DIR(*ps, 1);
		}
		else if (StrEquals(token, "TitleAtLeft"))
		{
			SFSET_TITLE_DIR(*ps, DIR_W);
			SMSET_TITLE_DIR(*ps, 1);
			SCSET_TITLE_DIR(*ps, 1);
		}
		else if (StrEquals(token, "TitleAtRight"))
		{
			SFSET_TITLE_DIR(*ps, DIR_E);
			SMSET_TITLE_DIR(*ps, 1);
			SCSET_TITLE_DIR(*ps, 1);
		}
		else if (StrEquals(token, "TopTitleRotated"))
		{
			SFSET_IS_TOP_TITLE_ROTATED(*ps, 1);
			SMSET_IS_TOP_TITLE_ROTATED(*ps, 1);
			SCSET_IS_TOP_TITLE_ROTATED(*ps, 1);
		}
		else if (StrEquals(token, "TopTitleNotRotated"))
		{
			SFSET_IS_TOP_TITLE_ROTATED(*ps, 0);
			SMSET_IS_TOP_TITLE_ROTATED(*ps, 1);
			SCSET_IS_TOP_TITLE_ROTATED(*ps, 1);
		}
		else
		{
			found = False;
		}
		break;

	case 'u':
		if (StrEquals(token, "UsePPosition"))
		{
			ps->flags.use_no_pposition = 0;
			ps->flag_mask.use_no_pposition = 1;
			ps->change_mask.use_no_pposition = 1;
		}
		else if (StrEquals(token, "UseUSPosition"))
		{
			ps->flags.use_no_usposition = 0;
			ps->flag_mask.use_no_usposition = 1;
			ps->change_mask.use_no_usposition = 1;
		}
		else if (StrEquals(token, "UseTransientPPosition"))
		{
			ps->flags.use_no_transient_pposition = 0;
			ps->flag_mask.use_no_transient_pposition = 1;
			ps->change_mask.use_no_transient_pposition = 1;
		}
		else if (StrEquals(token, "UseTransientUSPosition"))
		{
			ps->flags.use_no_transient_usposition = 0;
			ps->flag_mask.use_no_transient_usposition = 1;
			ps->change_mask.use_no_transient_usposition = 1;
		}
		else if (StrEquals(token, "UseIconPosition"))
		{
			SFSET_USE_ICON_POSITION_HINT(*ps, 1);
			SMSET_USE_ICON_POSITION_HINT(*ps, 1);
			SCSET_USE_ICON_POSITION_HINT(*ps, 1);
		}
#ifdef USEDECOR
		else if (StrEquals(token, "UseDecor"))
		{
			SAFEFREE(SGET_DECOR_NAME(*ps));
			GetNextToken(rest, &token);
			SSET_DECOR_NAME(*ps, token);
			ps->flags.has_decor = (token != NULL);
			ps->flag_mask.has_decor = 1;
			ps->change_mask.has_decor = 1;
		}
#endif
		else if (StrEquals(token, "UseStyle"))
		{
			int hit;

			token = PeekToken(rest, &rest);
			if (!token)
			{
				fvwm_msg(ERR, "CMD_Style",
					 "UseStyle needs an argument");
				break;
			}
			hit = 0;
			/* changed to accum multiple Style definitions
			 * (veliaa@rpi.edu) */
			for (add_style = all_styles; add_style;
			     add_style = SGET_NEXT_STYLE(*add_style))
			{
				if (StrEquals(token, SGET_NAME(*add_style)))
				{
					/* match style */
					hit = 1;
					merge_styles(ps, add_style, True);
				} /* end found matching style */
			} /* end looking at all styles */
			/* move forward one word */
			if (!hit)
			{
				fvwm_msg(
					ERR, "CMD_Style",
					"UseStyle: %s style not found", token);
			}
		}
		else
		{
			found = False;
		}
		break;

	case 'v':
		if (StrEquals(token, "VariablePosition") ||
		    StrEquals(token, "VariableUSPosition"))
		{
			SFSET_IS_FIXED(*ps, 0);
			SMSET_IS_FIXED(*ps, 1);
			SCSET_IS_FIXED(*ps, 1);
		}
		else if (StrEquals(token, "VariablePPosition"))
		{
			SFSET_IS_FIXED_PPOS(*ps, 0);
			SMSET_IS_FIXED_PPOS(*ps, 1);
			SCSET_IS_FIXED_PPOS(*ps, 1);
		}
		else if (StrEquals(token, "VariableSize") ||
			 StrEquals(token, "VariableUSSize"))
		{
			SFSET_IS_SIZE_FIXED(*ps, 0);
			SMSET_IS_SIZE_FIXED(*ps, 1);
			SCSET_IS_SIZE_FIXED(*ps, 1);
		}
		else if (StrEquals(token, "VariablePSize"))
		{
			SFSET_IS_PSIZE_FIXED(*ps, 0);
			SMSET_IS_PSIZE_FIXED(*ps, 1);
			SCSET_IS_PSIZE_FIXED(*ps, 1);
		}
		else
		{
			found = False;
		}
		break;

	case 'w':
		if (StrEquals(token, "WindowListSkip"))
		{
			SFSET_DO_WINDOW_LIST_SKIP(*ps, 1);
			SMSET_DO_WINDOW_LIST_SKIP(*ps, 1);
			SCSET_DO_WINDOW_LIST_SKIP(*ps, 1);
		}
		else if (StrEquals(token, "WindowListHit"))
		{
			SFSET_DO_WINDOW_LIST_SKIP(*ps, 0);
			SMSET_DO_WINDOW_LIST_SKIP(*ps, 1);
			SCSET_DO_WINDOW_LIST_SKIP(*ps, 1);
		}
		else if (StrEquals(token, "WindowShadeSteps"))
		{
			int n = 0;
			int val = 0;
			int unit = 0;

			n = GetOnePercentArgument(rest, &val, &unit);
			if (n != 1)
			{
				val = 0;
			}
			/* we have a 'pixel' suffix if unit != 0; negative
			 * values mean pixels */
			val = (unit != 0) ? -val : val;
			ps->flags.has_window_shade_steps = 1;
			ps->flag_mask.has_window_shade_steps = 1;
			ps->change_mask.has_window_shade_steps = 1;
			SSET_WINDOW_SHADE_STEPS(*ps, val);
		}
		else if (StrEquals(token, "WindowShadeScrolls"))
		{
			SFSET_DO_SHRINK_WINDOWSHADE(*ps, 0);
			SMSET_DO_SHRINK_WINDOWSHADE(*ps, 1);
			SCSET_DO_SHRINK_WINDOWSHADE(*ps, 1);
		}
		else if (StrEquals(token, "WindowShadeShrinks"))
		{
			SFSET_DO_SHRINK_WINDOWSHADE(*ps, 1);
			SMSET_DO_SHRINK_WINDOWSHADE(*ps, 1);
			SCSET_DO_SHRINK_WINDOWSHADE(*ps, 1);
		}
		else if (StrEquals(token, "WindowShadeLazy"))
		{
			SFSET_WINDOWSHADE_LAZINESS(*ps, WINDOWSHADE_LAZY);
			SMSET_WINDOWSHADE_LAZINESS(*ps, WINDOWSHADE_LAZY_MASK);
			SCSET_WINDOWSHADE_LAZINESS(*ps, WINDOWSHADE_LAZY_MASK);
		}
		else if (StrEquals(token, "WindowShadeAlwaysLazy"))
		{
			SFSET_WINDOWSHADE_LAZINESS(
				*ps, WINDOWSHADE_ALWAYS_LAZY);
			SMSET_WINDOWSHADE_LAZINESS(*ps, WINDOWSHADE_LAZY_MASK);
			SCSET_WINDOWSHADE_LAZINESS(*ps, WINDOWSHADE_LAZY_MASK);
		}
		else if (StrEquals(token, "WindowShadeBusy"))
		{
			SFSET_WINDOWSHADE_LAZINESS(*ps, WINDOWSHADE_BUSY);
			SMSET_WINDOWSHADE_LAZINESS(*ps, WINDOWSHADE_LAZY_MASK);
			SCSET_WINDOWSHADE_LAZINESS(*ps, WINDOWSHADE_LAZY_MASK);
		}
		else
		{
			found = False;
		}
		break;

	case 'x':
	case 'y':
	case 'z':
		if (0)
		{
		}
		else
		{
			found = False;
		}
		break;

	default:
		found = False;
		break;
	}

	return found;
}

static
void parse_and_set_window_style(char *action, window_style *ps)
{
	char *line;
	char *option;
	char *token;
	char *rest;
	Bool found;
	/* which current boxes to chain to */
	icon_boxes *cur_ib = NULL;

	while (isspace((unsigned char)*action))
	{
		action++;
	}
	line = action;
	while (action && *action && *action != '\n')
	{
		action = GetNextFullOption(action, &option);
		if (!option)
		{
			break;
		}
		token = PeekToken(option, &rest);
		if (!token)
		{
			free(option);
			break;
		}

		/* It might make more sense to capture the whole word, fix its
		 * case, and use strcmp, but there aren't many caseless compares
		 * because of this "switch" on the first letter. */
		found = style_parse_one_style_option(token, rest, ps, &cur_ib);

		if (found == False)
		{
			fvwm_msg(
				ERR, "CMD_Style", "Bad style option: %s",
				option);
			/* Can't return here since all malloced memory will be
			 * lost. Ignore rest of line instead. */
			/* No, I think we /can/ return here. In fact, /not/
			 * bombing out leaves a half-done style in the list!
			 * N.Bird 07-Sep-1999 */
			/* domivogt (01-Oct-1999): Which is exactly what we
			 * want! Why should all the styles be thrown away if a
			 * single one is mis-spelled? Let's just continue
			 * parsing styles. */
		}
		free(option);
	} /* end while still stuff on command */

	return;
}

/* ---------------------------- interface functions ------------------------- */

/* Compare two flag structures passed as byte arrays. Only compare bits set in
 * the mask.
 *
 *  Returned Value:
 *      zero if the flags are the same
 *      non-zero otherwise
 *
 *  Inputs:
 *      flags1 - first byte array of flags to compare
 *      flags2 - second byte array of flags to compare
 *      mask   - byte array of flags to be considered for the comparison
 *      len    - number of bytes to compare */
Bool blockcmpmask(char *blk1, char *blk2, char *mask, int length)
{
	int i;

	for (i = 0; i < length; i++)
	{
		if ((blk1[i] & mask[i]) != (blk2[i] & mask[i]))
		{
			/* flags are not the same, return 1 */
			return False;
		}
	}
	return True;
}

void free_icon_boxes(icon_boxes *ib)
{
	icon_boxes *temp;

	for ( ; ib != NULL; ib = temp)
	{
		temp = ib->next;
		if (ib->use_count == 0)
		{
			free(ib);
		}
		else
		{
			/* we can't delete the icon box yet, it is still in use
			 */
			ib->is_orphan = True;
		}
	}

	return;
}

void simplify_style_list(void)
{
	Scr.flags.do_need_style_list_update = 0;
	while (__simplify_style_list())
	{
		/* repeat until nothing has been done for a complete pass */
	}

	return;
}

/* lookup_style - look through a list for a window name, or class
 *
 *  Returned Value:
 *      merged matching styles in callers window_style.
 *
 *  Inputs:
 *      fw     - FvwmWindow structure to match against
 *      styles - callers return area
 */
void lookup_style(FvwmWindow *fw, window_style *styles)
{
	window_style *nptr;

	/* clear callers return area */
	memset(styles, 0, sizeof(window_style));

	/* look thru all styles in order defined. */
	for (nptr = all_styles; nptr != NULL; nptr = SGET_NEXT_STYLE(*nptr))
	{
		/* If name/res_class/res_name match, merge */
		if (matchWildcards(SGET_NAME(*nptr),fw->class.res_class) ==
		    TRUE)
		{
			merge_styles(styles, nptr, False);
		}
		else if (matchWildcards(SGET_NAME(*nptr),fw->class.res_name) ==
			 TRUE)
		{
			merge_styles(styles, nptr, False);
		}
		else if (matchWildcards(SGET_NAME(*nptr),fw->name.name) == TRUE)
		{
			merge_styles(styles, nptr, False);
		}
	}
	if (!DO_IGNORE_GNOME_HINTS(fw))
	{
		window_style gnome_style;

		/* use GNOME hints if not overridden by user with
		 * GNOMEIgnoreHitns */
		memset(&gnome_style, 0, sizeof(window_style));
		GNOME_GetStyle(fw, &gnome_style);
		merge_styles(&gnome_style, styles, False);
		memcpy(styles, &gnome_style, sizeof(window_style));
	}
	EWMH_GetStyle(fw, styles);
	return;
}

/* This function sets the style update flags as necessary */
void check_window_style_change(
	FvwmWindow *t, update_win *flags, window_style *ret_style)
{
	int i;
	char *wf;
	char *sf;
	char *sc;

	lookup_style(t, ret_style);
	if (!ret_style->has_style_changed && !IS_STYLE_DELETED(t))
	{
		/* nothing to do */
		return;
	}

	/*
	 * do_ignore_gnome_hints
	 *
	 * must handle these first because they may alter the style
	 */
	if (SCDO_IGNORE_GNOME_HINTS(*ret_style) &&
	    !SDO_IGNORE_GNOME_HINTS(ret_style->flags))
	{
		GNOME_GetStyle(t, ret_style);
		/* may need further treatment for some styles */
		flags->do_update_gnome_styles = True;
	}

	/****** common style flags ******/

	/* All static common styles can simply be copied. For some there is
	 * additional work to be done below. */
	wf = (char *)(&FW_COMMON_STATIC_FLAGS(t));
	sf = (char *)(&SFGET_COMMON_STATIC_FLAGS(*ret_style));
	sc = (char *)(&SCGET_COMMON_STATIC_FLAGS(*ret_style));
	/* copy the static common window flags */
	for (i = 0; i < sizeof(SFGET_COMMON_STATIC_FLAGS(*ret_style)); i++)
	{
		wf[i] = (wf[i] & ~sc[i]) | (sf[i] & sc[i]);
		sf[i] = wf[i];
	}

	if (IS_STYLE_DELETED(t))
	{
		/* update all styles */
		memset(flags, 0xff, sizeof(*flags));
		SET_STYLE_DELETED(t, 0);
		return;
	}

	/*
	 * is_sticky
	 * is_icon_sticky
	 */
	if (SCIS_STICKY(*ret_style))
	{
		flags->do_update_stick = True;
	}
	if (SCIS_ICON_STICKY(*ret_style) && IS_ICONIFIED(t) && !IS_STICKY(t))
	{
		flags->do_update_stick_icon = True;
	}

	/*
	 * focus policy
	 */
	if (fpol_is_policy_changed(&SC_FOCUS_POLICY(*ret_style)))
	{
		flags->do_setup_focus_policy = True;
	}

	/*
	 * is_left_title_rotated_cw
	 * is_right_title_rotated_cw
	 * is_top_title_rotated
	 * is_bottom_title_rotated
	 */
	if (SCIS_LEFT_TITLE_ROTATED_CW(*ret_style) ||
	    SCIS_RIGHT_TITLE_ROTATED_CW(*ret_style) ||
	    SCIS_TOP_TITLE_ROTATED(*ret_style) ||
	    SCIS_BOTTOM_TITLE_ROTATED(*ret_style))
	{
		flags->do_update_title_text_dir = True;
	}

	/*
	 * title_dir
	 */
	if (SCTITLE_DIR(*ret_style))
	{
		flags->do_update_title_dir = True;
	}

	/*
	 * has_mwm_border
	 * has_mwm_buttons
	 */
	if (SCHAS_MWM_BORDER(*ret_style) ||
	    SCHAS_MWM_BUTTONS(*ret_style))
	{
		flags->do_redecorate = True;
	}

	/*
	 * has_icon_font
	 */
	if (SCHAS_ICON_FONT(*ret_style))
	{
		flags->do_update_icon_font = True;
	}

	/*
	 * has_window_font
	 */
	if (SCHAS_WINDOW_FONT(*ret_style))
	{
		flags->do_update_window_font = True;
	}

	/*
	 * has_stippled_title
	 */
	if (SCHAS_STIPPLED_TITLE(*ret_style))
	{
		flags->do_redraw_decoration = True;
	}

	/*
	 * has_no_icon_title
	 * is_icon_suppressed
	 *
	 * handled below
	 */

	/****** private style flags ******/

	/* nothing to do for these flags (only used when mapping new windows):
	 *
	 *   do_place_random
	 *   do_place_smart
	 *   do_start_lowered
	 *   use_no_pposition
	 *   use_no_usposition
	 *   use_no_transient_pposition
	 *   use_no_transient_usposition
	 *   use_start_on_desk
	 *   use_start_on_page_for_transient
	 *   use_start_on_screen
	 *   manual_placement_honors_starts_on_page
	 *   capture_honors_starts_on_page
	 *   recapture_honors_starts_on_page
	 *   use_layer
	 *   ewmh_placement_mode
	 */

	/* not implemented yet:
	 *
	 *   handling the 'usestyle' style
	 */

	/*
	 * do_window_list_skip
	 */
	if (SCDO_WINDOW_LIST_SKIP(*ret_style))
	{
		flags->do_update_modules_flags = True;
		flags->do_update_ewmh_state_hints = True;
	}
	/*
	 * has_icon
	 * icon_override
	 */
	if (ret_style->change_mask.has_icon ||
	    SCICON_OVERRIDE(*ret_style))
	{
		flags->do_update_icon_font = True;
		flags->do_update_icon = True;
	}

	/*
	 * has_no_icon_title
	 * is_icon_suppressed
	 */
	if (SCHAS_NO_ICON_TITLE(*ret_style) ||
	    SCIS_ICON_SUPPRESSED(*ret_style))
	{
		flags->do_update_icon_font = True;
		flags->do_update_icon_title = True;
		flags->do_update_icon = True;
		flags->do_update_modules_flags = True;
	}

	/*
	 *   has_icon_boxes
	 */
	if (ret_style->change_mask.has_icon_boxes)
	{
		flags->do_update_icon_boxes = True;
		flags->do_update_icon = True;
	}

	/*
	 * do_ewmh_donate_icon
	 */
	if (SCDO_EWMH_DONATE_ICON(*ret_style))
	{
		flags->do_update_ewmh_icon = True;
	}

	/*
	 * has_mini_icon
	 * do_ewmh_mini_icon_override
	 */
	if (FMiniIconsSupported && ret_style->change_mask.has_mini_icon ||
	    SCDO_EWMH_MINI_ICON_OVERRIDE(*ret_style))
	{
		flags->do_update_mini_icon = True;
		flags->do_update_ewmh_mini_icon = True;
		flags->do_redecorate = True;
	}

	/*
	 * do_ewmh_donate_mini_icon
	 */
	if (FMiniIconsSupported && SCDO_EWMH_DONATE_MINI_ICON(*ret_style))
	{
		flags->do_update_ewmh_mini_icon = True;
	}

	/*
	 * has_max_window_size
	 */
	if (ret_style->change_mask.has_max_window_size)
	{
		flags->do_resize_window = True;
		flags->do_update_ewmh_allowed_actions = True;
	}

	/*
	 * has_color_back
	 * has_color_fore
	 * use_colorset
	 * use_border_colorset
	 */
	if (ret_style->change_mask.has_color_fore ||
	    ret_style->change_mask.has_color_back ||
	    ret_style->change_mask.use_colorset ||
	    ret_style->change_mask.use_border_colorset)
	{
		flags->do_update_window_color = True;
	}
	/*
	 * has_color_back_hi
	 * has_color_fore_hi
	 * use_colorset_hi
	 * use_border_colorset_hi
	 */
	if (ret_style->change_mask.has_color_fore_hi ||
	    ret_style->change_mask.has_color_back_hi ||
	    ret_style->change_mask.use_colorset_hi ||
	    ret_style->change_mask.use_border_colorset_hi)
	{
		flags->do_update_window_color_hi = True;
	}

	/*
	 * has_decor
	 */
	if (ret_style->change_mask.has_decor)
	{
		flags->do_redecorate = True;
		flags->do_update_window_font_height = True;
	}

	/*
	 * has_no_title
	 */
	if (ret_style->change_mask.has_no_title)
	{
		flags->do_redecorate = True;
		flags->do_update_window_font = True;
	}

	/*
	 * do_decorate_transient
	 */
	if (ret_style->change_mask.do_decorate_transient)
	{
		flags->do_redecorate_transient = True;
	}

	/*
	 * has_ol_decor
	 */
	if (ret_style->change_mask.has_ol_decor)
	{
		/* old decor overrides 'has_no_icon_title'! */
		flags->do_update_icon_font = True;
		flags->do_update_icon_title = True;
		flags->do_update_icon = True;
		flags->do_redecorate = True;
	}

	/*
	 * has_no_border
	 * has_border_width
	 * has_handle_width
	 * has_mwm_decor
	 * has_mwm_functions
	 * has_no_handles
	 * is_button_disabled
	 */
	if (SCHAS_NO_BORDER(*ret_style) ||
	    ret_style->change_mask.has_border_width ||
	    ret_style->change_mask.has_handle_width ||
	    ret_style->change_mask.has_mwm_decor ||
	    ret_style->change_mask.has_mwm_functions ||
	    ret_style->change_mask.has_no_handles ||
	    ret_style->change_mask.is_button_disabled)
	{
		flags->do_redecorate = True;
		flags->do_update_ewmh_allowed_actions = True;
	}

	if (ret_style->change_mask.do_save_under ||
	    ret_style->change_mask.use_backing_store ||
	    ret_style->change_mask.use_parent_relative)
	{
		flags->do_update_frame_attributes = True;
	}

	if (ret_style->change_mask.use_parent_relative &&
	    ret_style->flags.use_parent_relative)
	{
		/* needed only for Opacity -> ParentalRelativity */
		flags->do_refresh = True;
	}

	/*
	 * has_placement_penalty
	 * has_placement_percentage_penalty
	 */
	if (ret_style->change_mask.has_placement_penalty ||
	    ret_style->change_mask.has_placement_percentage_penalty)
	{
		flags->do_update_placement_penalty = True;
	}

	/*
	 * do_ewmh_ignore_strut_hints
	 */
	if (SCDO_EWMH_IGNORE_STRUT_HINTS(*ret_style))
	{
		flags->do_update_working_area = True;
	}

	/*
	 * do_ewmh_ignore_state_hints
	 */
	if (SCDO_EWMH_IGNORE_STATE_HINTS(*ret_style))
	{
		flags->do_update_ewmh_state_hints = True;
		flags->do_update_modules_flags = True;
	}

	/*
	 * do_ewmh_use_staking_hints
	 */
	if (SCDO_EWMH_USE_STACKING_HINTS(*ret_style))
	{
		flags->do_update_ewmh_stacking_hints = True;
	}

	/*
	 *  use_indexed_window_name
	 */
	if (SCUSE_INDEXED_WINDOW_NAME(*ret_style))
	{
		flags->do_update_visible_window_name = True;
		flags->do_redecorate = True;
	}

	/*
	 *  use_indexed_icon_name
	 */
	if (SCUSE_INDEXED_ICON_NAME(*ret_style))
	{
		flags->do_update_visible_icon_name = True;
		flags->do_update_icon_title = True;
	}

	/*
	 *  is_fixed
	 */
	if (SCIS_FIXED(*ret_style) ||
	    SCIS_FIXED_PPOS(*ret_style) ||
	    SCIS_SIZE_FIXED(*ret_style) ||
	    SCIS_PSIZE_FIXED(*ret_style) ||
	    SCHAS_OVERRIDE_SIZE(*ret_style))
	{
		flags->do_update_ewmh_allowed_actions = True;
	}

	return;
}

/* Mark all styles as unchanged. */
void reset_style_changes(void)
{
	window_style *temp;

	for (temp = all_styles; temp != NULL; temp = SGET_NEXT_STYLE(*temp))
	{
		temp->has_style_changed = 0;
		memset(&SCGET_COMMON_STATIC_FLAGS(*temp), 0,
		       sizeof(SCGET_COMMON_STATIC_FLAGS(*temp)));
		memset(&(temp->change_mask), 0, sizeof(temp->change_mask));
	}

	return;
}

/* Mark styles as updated if their colorset changed. */
void update_style_colorset(int colorset)
{
	window_style *temp;

	for (temp = all_styles; temp != NULL; temp = SGET_NEXT_STYLE(*temp))
	{
		if (SUSE_COLORSET(&temp->flags) &&
		    SGET_COLORSET(*temp) == colorset)
		{
			temp->has_style_changed = 1;
			temp->change_mask.use_colorset = 1;
			Scr.flags.do_need_window_update = 1;
		}
		if (SUSE_COLORSET_HI(&temp->flags) &&
		    SGET_COLORSET_HI(*temp) == colorset)
		{
			temp->has_style_changed = 1;
			temp->change_mask.use_colorset_hi = 1;
			Scr.flags.do_need_window_update = 1;
		}
		if (SUSE_BORDER_COLORSET(&temp->flags) &&
		    SGET_BORDER_COLORSET(*temp) == colorset)
		{
			temp->has_style_changed = 1;
			temp->change_mask.use_border_colorset = 1;
			Scr.flags.do_need_window_update = 1;
		}
		if (SUSE_BORDER_COLORSET_HI(&temp->flags) &&
		    SGET_BORDER_COLORSET_HI(*temp) == colorset)
		{
			temp->has_style_changed = 1;
			temp->change_mask.use_border_colorset_hi = 1;
			Scr.flags.do_need_window_update = 1;
		}
	}

	return;
}

/* Update fore and back colours for a specific window */
void update_window_color_style(FvwmWindow *fw, window_style *pstyle)
{
	int cs = Scr.DefaultColorset;

	if (SUSE_COLORSET(&pstyle->flags))
	{
		cs = SGET_COLORSET(*pstyle);
		fw->cs = cs;
	}
	else
	{
		fw->cs = -1;
	}
	if (SGET_FORE_COLOR_NAME(*pstyle) != NULL &&
	   !SUSE_COLORSET(&pstyle->flags))
	{
		fw->colors.fore = GetColor(SGET_FORE_COLOR_NAME(*pstyle));
	}
	else
	{
		fw->colors.fore = Colorset[cs].fg;
	}
	if (SGET_BACK_COLOR_NAME(*pstyle) != NULL &&
	   !SUSE_COLORSET(&pstyle->flags))
	{
		fw->colors.back = GetColor(SGET_BACK_COLOR_NAME(*pstyle));
		fw->colors.shadow = GetShadow(fw->colors.back);
		fw->colors.hilight = GetHilite(fw->colors.back);
	}
	else
	{
		fw->colors.hilight = Colorset[cs].hilite;
		fw->colors.shadow = Colorset[cs].shadow;
		fw->colors.back = Colorset[cs].bg;
	}
	if (SUSE_BORDER_COLORSET(&pstyle->flags))
	{
		cs = SGET_BORDER_COLORSET(*pstyle);
		fw->border_cs = cs;
		fw->border_colors.hilight = Colorset[cs].hilite;
		fw->border_colors.shadow = Colorset[cs].shadow;
		fw->border_colors.back = Colorset[cs].bg;
	}
	else
	{
		fw->border_cs = -1;
		fw->border_colors.hilight = fw->colors.hilight;
		fw->border_colors.shadow = fw->colors.shadow;
		fw->border_colors.back = fw->colors.back;
	}
}

void update_window_color_hi_style(FvwmWindow *fw, window_style *pstyle)
{
	int cs = Scr.DefaultColorset;

	if (SUSE_COLORSET_HI(&pstyle->flags))
	{
		cs = SGET_COLORSET_HI(*pstyle);
		fw->cs_hi = cs;
	}
	else
	{
		fw->cs_hi = -1;
	}
	if (SGET_FORE_COLOR_NAME_HI(*pstyle) != NULL &&
	   !SUSE_COLORSET_HI(&pstyle->flags))
	{
		fw->hicolors.fore = GetColor(SGET_FORE_COLOR_NAME_HI(*pstyle));
	}
	else
	{
		fw->hicolors.fore = Colorset[cs].fg;
	}
	if (SGET_BACK_COLOR_NAME_HI(*pstyle) != NULL &&
	   !SUSE_COLORSET_HI(&pstyle->flags))
	{
		fw->hicolors.back = GetColor(SGET_BACK_COLOR_NAME_HI(*pstyle));
		fw->hicolors.shadow = GetShadow(fw->hicolors.back);
		fw->hicolors.hilight = GetHilite(fw->hicolors.back);
	}
	else
	{
		fw->hicolors.hilight = Colorset[cs].hilite;
		fw->hicolors.shadow = Colorset[cs].shadow;
		fw->hicolors.back = Colorset[cs].bg;
	}
	if (SUSE_BORDER_COLORSET_HI(&pstyle->flags))
	{
		cs = SGET_BORDER_COLORSET_HI(*pstyle);
		fw->border_cs_hi = cs;
		fw->border_hicolors.hilight = Colorset[cs].hilite;
		fw->border_hicolors.shadow = Colorset[cs].shadow;
		fw->border_hicolors.back = Colorset[cs].bg;
	}
	else
	{
		fw->border_cs_hi = -1;
		fw->border_hicolors.hilight = fw->hicolors.hilight;
		fw->border_hicolors.shadow = fw->hicolors.shadow;
		fw->border_hicolors.back = fw->hicolors.back;
	}
}

/* ---------------------------- builtin commands ---------------------------- */

/* Process a style command.  First built up in a temp area.
 * If valid, added to the list in a malloced area.
 *
 *                    *** Important note ***
 *
 * Remember that *all* styles need a flag, flag_mask and change_mask.
 * It is not enough to add the code for new styles in this function.
 * There *must* be corresponding code in handle_new_window_style()
 * and merge_styles() too.  And don't forget that allocated memory
 * must be freed in ProcessDestroyStyle().
 */
void CMD_Style(F_CMD_ARGS)
{
	/* temp area to build name list */
	window_style *ps;

	ps = (window_style *)safemalloc(sizeof(window_style));
	/* init temp window_style area */
	memset(ps, 0, sizeof(window_style));
	/* init default focus policy */
	fpol_init_default_fp(&SF_FOCUS_POLICY(*ps));
	/* mark style as changed */
	ps->has_style_changed = 1;
	/* set global flag */
	Scr.flags.do_need_window_update = 1;
	/* default StartsOnPage behavior for initial capture */
	ps->flags.capture_honors_starts_on_page = 1;

	/* parse style name */
	action = GetNextToken(action, &SGET_NAME(*ps));
	/* in case there was no argument! */
	if (SGET_NAME(*ps) == NULL)
	{
		free(ps);
		return;
	}
	if (action == NULL)
	{
		free(SGET_NAME(*ps));
		free(ps);
		return;
	}

	parse_and_set_window_style(action, ps);

	/* capture default icons */
	if (StrEquals(SGET_NAME(*ps), "*"))
	{
		if (ps->flags.has_icon == 1)
		{
			if (Scr.DefaultIcon)
				free(Scr.DefaultIcon);
			Scr.DefaultIcon = SGET_ICON_NAME(*ps);
			ps->flags.has_icon = 0;
			ps->flag_mask.has_icon = 0;
			ps->change_mask.has_icon = 1;
			SSET_ICON_NAME(*ps, NULL);
		}
	}
	if (last_style_in_list &&
	    strcmp(SGET_NAME(*ps), SGET_NAME(*last_style_in_list)) == 0)
	{
		/* merge with previous style */
		merge_styles(last_style_in_list, ps, True);
		free_style(ps);
		free(ps);
	}
	else
	{
		/* add temp name list to list */
		add_style_to_list(ps);
	}

	return;
}

void CMD_DestroyStyle(F_CMD_ARGS)
{
	char *name;
	FvwmWindow *t;

	/* parse style name */
	name = PeekToken(action, &action);

	/* in case there was no argument! */
	if (name == NULL)
		return;

	/* Do it */
	if (remove_all_of_style_from_list(name))
	{
		/* compact the current list of styles */
		Scr.flags.do_need_style_list_update = 1;
	}
	/* mark windows for update */
	for (t = Scr.FvwmRoot.next; t != NULL && t != &Scr.FvwmRoot;
	     t = t->next)
	{
		if (matchWildcards(name, t->class.res_class) == TRUE ||
		    matchWildcards(name, t->class.res_name) == TRUE ||
		    matchWildcards(name, t->name.name) == TRUE)
		{
			SET_STYLE_DELETED(t, 1);
			Scr.flags.do_need_window_update = 1;
		}
	}

	return;
}
