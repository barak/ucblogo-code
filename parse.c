/*
 *      parse.c         logo parser module              dvb
 *
 *	Copyright (C) 1993 by the Regents of the University of California
 *
 *      This program is free software; you can redistribute it and/or modify
 *      it under the terms of the GNU General Public License as published by
 *      the Free Software Foundation; either version 2 of the License, or
 *      (at your option) any later version.
 *  
 *      This program is distributed in the hope that it will be useful,
 *      but WITHOUT ANY WARRANTY; without even the implied warranty of
 *      MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *      GNU General Public License for more details.
 *  
 *      You should have received a copy of the GNU General Public License
 *      along with this program; if not, write to the Free Software
 *      Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 */

#ifdef WIN32
#include <windows.h>
#endif

#include "logo.h"
#include "globals.h"

#ifdef HAVE_TERMIO_H
#include <termio.h>
#else
#ifdef HAVE_SGTTY_H
#include <sgtty.h>
#endif
#endif

#include <ctype.h>

#ifdef ibm
#ifndef _MSC_VER
#include <bios.h>
extern int getch(void);
#endif /* _MSC_VER */
#endif
#ifdef __ZTC__
#include <disp.h>
#endif

FILE *readstream;
FILE *writestream;
FILE *loadstream;

FILE *dribblestream = NULL;
int input_blocking = 0;
NODE *deepend_proc_name = NIL;

int rd_getc(FILE *strm) {
    int c;
#ifdef WIN32
    MSG msg;
#endif

#ifndef WIN32 /* skip this section ... */
#ifdef __ZTC__
    if (strm == stdin) zflush();
    c = ztc_getc(strm);
#else
    c = getc(strm);
#endif
    if (strm == stdin && c != EOF) update_coords(c);
#ifdef ibm
    if (c == 17 && interactive && strm==stdin) { /* control-q */
	to_pending = 0;
	err_logo(STOP_ERROR,NIL);
    }
    if (c == 23 && interactive && strm==stdin) { /* control-w */
#ifndef __ZTC__
	getc(strm); /* eat up the return */
#endif

#ifdef SIG_TAKES_ARG
	logo_pause(0);
#else
	logo_pause();
#endif

	return(rd_getc(strm));
    }
#endif
#else /* WIN32 */
    if (strm == stdin) {
	if (!line_avail) {
	    win32_text_cursor();
	    while (GetMessage(&msg, NULL, 0, 0)) {
		TranslateMessage(&msg);
		DispatchMessage(&msg);
		if (line_avail)
		    break;
		}
	    }
      c = read_line[read_index++];
      if (c == 17 && interactive && strm==stdin) { /* control-q */
	to_pending = 0;
	err_logo(STOP_ERROR,NIL);
	line_avail = 0;
	free(read_line);
	return('\n');
    }
    if (c == 23 && interactive && strm==stdin) { /* control-w */
	line_avail = 0;
	free(read_line);
	logo_pause(0);
	return(rd_getc(strm));
    }
      if (c == '\n')
	line_avail = 0;
	free(read_line);
    }
    else /* reading from a file */
      c = getc(strm);
#endif /* WIN32 */

#ifdef ecma
    return(ecma_clear(c));
#else
    return(c);
#endif
}

void rd_print_prompt(char *str) {
#ifdef ibm
#if defined(__ZTC__) || defined(WIN32)
    if (in_graphics_mode && !in_splitscreen)
#else
#ifndef _MSC_VER
    if (in_graphics_mode && ibm_screen_top == 0)
#endif /* _MSC_VER */
#endif
	lsplitscreen(NIL);
#endif

#ifdef mac
    extern int in_fscreen(void);
    if (in_fscreen())
	lsplitscreen(NIL);
#endif

    ndprintf(stdout,"%t",str);
#if defined(__ZTC__) && !defined(WIN32) /* sowings */
    zflush();
#endif
}

#if defined(__ZTC__) && !defined(WIN32) /* sowings */
void zrd_print_prompt(char *str) {
    newline_bugfix();
    rd_print_prompt(str);
}
#else
#define zrd_print_prompt rd_print_prompt
#endif

#define into_line(chr) {if (phys_line >= p_end) { \
				p_len += MAX_PHYS_LINE; \
				p_pos = phys_line - p_line; \
				p_line = realloc(p_line, p_len); \
				p_end = &p_line[p_len-1]; \
				phys_line = &p_line[p_pos]; \
			    } \
			    *phys_line++ = (chr);}

char *p_line = 0, *p_end;
int p_len = MAX_PHYS_LINE;

NODE *reader(FILE *strm, char *prompt) {
    int c = 0, dribbling, vbar = 0, paren = 0;
    int bracket = 0, brace = 0, p_pos, contin=1, insemi=0;
    static char ender[] = "\nEND\n";
    char *phys_line, *lookfor = ender;
    NODETYPES this_type = STRING;
    NODE *ret;

	if (!strcmp(prompt, "RW")) {	/* called by readword */
		prompt = "";
		contin = 0;
	}
    charmode_off();
#ifdef WIN32
    dribbling = 0;
#else
    dribbling = (dribblestream != NULL && strm == stdin);
#endif
    if (p_line == 0) {
    	p_line = malloc(MAX_PHYS_LINE);
	if (p_line == NULL) {
	    err_logo(OUT_OF_MEM, NIL);
		    return UNBOUND;
	}
    	p_end = &p_line[MAX_PHYS_LINE-1];
    }
    phys_line = p_line;
    if (strm == stdin && *prompt) {
	if (interactive) {
	  rd_print_prompt(prompt);
#ifdef WIN32
	  win32_update_text();
#endif
	}
    }
    if (strm == stdin) {
	input_blocking++;
	erract_errtype = FATAL;
    }

#ifndef TIOCSTI
    if (!setjmp(iblk_buf)) {
#endif
    c = rd_getc(strm);
    while (c != EOF && (vbar || paren || bracket || brace || c != '\n')) {
	if (dribbling) rd_putc(c, dribblestream);
	if (c == '\\' && (c = rd_getc(strm)) != EOF) {
	    if (dribbling) rd_putc(c, dribblestream);
	    c = setparity(c);
	    this_type = BACKSLASH_STRING;
	    if (c == setparity('\n') && strm == stdin) {
		if (interactive) zrd_print_prompt("\\ ");
	    }
	}
	if (c != EOF) into_line(c);
	if (*prompt && (c&0137) == *lookfor) {
		lookfor++;
		if (*lookfor == 0) {
			err_logo(DEEPEND, deepend_proc_name);
			break;
		}
	} else lookfor = ender;
	if (c == '|') {
	    vbar = !vbar;
	    this_type = VBAR_STRING;
	} else if (contin && !vbar && !insemi) {
		if (c == '(') paren++;
		else if (paren && c == ')') paren--;
		else if (c == '[') bracket++;
		else if (bracket && c == ']') bracket--;
		else if (c == '{') brace++;
		else if (brace && c == '}') brace--;
		else if (c == ';') insemi++;
	}

	if (this_type == STRING && strchr(special_chars, c))
	    this_type = VBAR_STRING;
	if (/* (vbar || paren ...) && */ c == '\n') {
	    insemi = 0;
	    if (strm == stdin) {
		if (interactive) zrd_print_prompt(vbar ? "| " : "~ ");
	    }
	}
	while (!vbar && c == '~' && (c = rd_getc(strm)) != EOF) {
	    while (c == ' ' || c == '\t')
		c = rd_getc(strm);
	    if (dribbling) rd_putc(c, dribblestream);
	    into_line(c);
	    if (c == '\n' && strm == stdin) {
		insemi = 0;
		if (interactive) zrd_print_prompt("~ ");
	    }
	}
	if (c != EOF) c = rd_getc(strm);
    }
#ifndef TIOCSTI
    }
#endif
    *phys_line = '\0';
    input_blocking = 0;
#if defined(__ZTC__) && !defined(WIN32) /* sowings */
    fix_cursor();
    if (interactive && strm == stdin) newline_bugfix();
#endif
    if (dribbling)
	rd_putc('\n', dribblestream);
    if (c == EOF && strm == stdin) {
	if (interactive) clearerr(stdin);
	rd_print_prompt("\n");
    }
    if (phys_line == p_line) return(Null_Word); /* so emptyp works */
    ret = make_strnode(p_line, (struct string_block *)NULL, (int)strlen(p_line),
		       this_type, strnzcpy);
    return(ret);
}

NODE *list_to_array(NODE *list) {
    NODE *np = list, *result;
    FIXNUM len = 0, i;

    for (; np; np = cdr(np)) len++;

    result = make_array(len);
    setarrorg(result,1);

    for (i = 0, np = list; np; np = cdr(np))
	(getarrptr(result))[i++] = car(np);

    return(result);
}

#define parens(ch)      (ch == '(' || ch == ')' || ch == ';')
#define infixs(ch)      (ch == '*' || ch == '/' || ch == '+' || ch == '-' || ch == '=' || ch == '<' || ch == '>')
#define white_space(ch) (ch == ' ' || ch == '\t' || ch == '\n' || ch == '\0')

NODE *parser_iterate(char **inln, char *inlimit, struct string_block *inhead,
		     BOOLEAN semi, int endchar) {
    char ch, *wptr = NULL;
    static char terminate = '\0';   /* KLUDGE */
    NODE *outline = NIL, *lastnode = NIL, *tnode = NIL;
    int windex = 0, vbar = 0;
    NODETYPES this_type = STRING;
    BOOLEAN broken = FALSE;

    do {
	/* get the current character and increase pointer */
	ch = **inln;
	if (!vbar && windex == 0) wptr = *inln;
	if (++(*inln) >= inlimit) *inln = &terminate;

	/* skip through comments and line continuations */
	while (!vbar && ((semi && ch == ';') ||
#ifdef WIN32
		(ch == '~' && (**inln == 012 || **inln == 015)))) {
	    while (ch == '~' && (**inln == 012 || **inln == 015)) {
#else
		(ch == '~' && **inln == '\n'))) {
	    while (ch == '~' && **inln == '\n') {
#endif
		if (++(*inln) >= inlimit) *inln = &terminate;
		ch = **inln;
		if (windex == 0) wptr = *inln;
		else {
		    if (**inln == ']' || **inln == '[' ||
		    			 **inln == '{' || **inln == '}') {
			ch = ' ';
			break;
		    } else {
			broken = TRUE;
		    }
		}
		if (++(*inln) >= inlimit) *inln = &terminate;
	    }

	    if (semi && ch == ';') {
#ifdef WIN32
		if (**inln != 012 && **inln != 015)
#else
		if (**inln != '\n')
#endif
		do {
		    ch = **inln;
		    if (windex == 0) wptr = *inln;
		    else broken = TRUE;
		    if (++(*inln) >= inlimit) *inln = &terminate;
		} 
#ifdef WIN32
		while (ch != '\0' && ch != '~' && **inln != 012 && **inln != 015);
#else /* !Win32 */
		while (ch != '\0' && ch != '~' && **inln != '\n');
#endif
		if (ch != '\0' && ch != '~') ch = '\n';
	    }
	}

	/* flag that this word will be of BACKSLASH_STRING type */
	if (getparity(ch)) this_type = BACKSLASH_STRING;

	if (ch == '|') {
	    vbar = !vbar;
	    this_type = VBAR_STRING;
	    broken = TRUE; /* so we'll copy the chars */
	}

	else if (vbar || (!white_space(ch) && ch != ']' &&
		    ch != '{' && ch != '}' && ch != '['))
	    windex++;

	if (vbar) continue;

	else if (ch == endchar) break;

	else if (ch == ']') err_logo(UNEXPECTED_BRACKET, NIL);
	else if (ch == '}') err_logo(UNEXPECTED_BRACE, NIL);

	/* if this is a '[', parse a new list */
	else if (ch == '[') {
	    tnode = cons(parser_iterate(inln,inlimit,inhead,semi,']'), NIL);
	    if (**inln == '\0') ch = '\0';
	}

	else if (ch == '{') {
	    tnode = cons(list_to_array
			 (parser_iterate(inln,inlimit,inhead,semi,'}')), NIL);
	    if (**inln == '@') {
		int i = 0, sign = 1;

		(*inln)++;
		if (**inln == '-') {
		    sign = -1;
		    (*inln)++;
		}
		while ((ch = **inln) >= '0' && ch <= '9') {
		    i = (i*10) + ch - '0';
		    (*inln)++;
		}
		setarrorg(car(tnode),sign*i);
	    }
	    if (**inln == '\0') ch = '\0';
	}

/* if this character or the next one will terminate string, make the word */
	else if (white_space(ch) || **inln == ']' || **inln == '[' ||
			    **inln == '{' || **inln == '}') {
		if (windex > 0) {
		    if (broken == FALSE)
			 tnode = cons(make_strnode(wptr, inhead, windex,
						   this_type, strnzcpy),
				      NIL);
		    else {
			 tnode = cons(make_strnode(wptr,
				 (struct string_block *)NULL, windex,
				 this_type, (semi ? mend_strnzcpy : mend_nosemi)),
				 NIL);
			 broken = FALSE;
		    }
		    this_type = STRING;
		    windex = 0;
		}
	}

	/* put the word onto the end of the return list */
	if (tnode != NIL) {
	    if (outline == NIL) outline = tnode;
	    else setcdr(lastnode, tnode);
	    lastnode = tnode;
	    tnode = NIL;
	}
    } while (ch);
    return(outline);
}

NODE *parser(NODE *nd, BOOLEAN semi) {
    NODE *rtn;
    int slen;
    char *lnsav;

    rtn = cnv_node_to_strnode(nd);
    slen = getstrlen(rtn);
    lnsav = getstrptr(rtn);
    rtn = parser_iterate(&lnsav,lnsav + slen,getstrhead(rtn),semi,-1);
    return(rtn);
}

NODE *lparse(NODE *args) {
    NODE *arg, *val = UNBOUND;

    arg = string_arg(args);
    if (NOT_THROWING) {
	val = parser(arg, FALSE);
    }
    return(val);
}

NODE *runparse_node(NODE *nd, NODE **ndsptr) {
    NODE *outline = NIL, *tnode = NIL, *lastnode = NIL, *snd;
    char *wptr, *tptr;
    struct string_block *whead;
    int wlen, wcnt, tcnt, isnumb, gotdot;
    NODETYPES wtyp;
    BOOLEAN monadic_minus = FALSE;

    if (nd == Minus_Tight) return cons(nd, NIL);
    snd = cnv_node_to_strnode(nd);
    wptr = getstrptr(snd);
    wlen = getstrlen(snd);
    wtyp = nodetype(snd);
    wcnt = 0;
    whead = getstrhead(snd);

    while (wcnt < wlen) {
	if (*wptr == ';') {
	    *ndsptr = NIL;
	    break;
	}
	if (*wptr == '"') {
	    tcnt = 0;
	    tptr = ++wptr;
	    wcnt++;
	    while (wcnt < wlen && !parens(*wptr)) {
		if (wtyp == BACKSLASH_STRING && getparity(*wptr))
		    wtyp = PUNBOUND;    /* flag for "\( case */
		wptr++, wcnt++, tcnt++;
	    }
	    if (wtyp == PUNBOUND) {
		wtyp = BACKSLASH_STRING;
		tnode = cons(make_quote(intern(make_strnode(tptr, NULL,
					tcnt, wtyp, noparity_strnzcpy))), NIL);
	    } else
		tnode = cons(make_quote(intern(make_strnode(tptr, whead, tcnt,
				        wtyp, strnzcpy))), NIL);
	} else if (*wptr == ':') {
	    tcnt = 0;
	    tptr = ++wptr;
	    wcnt++;
	    while (wcnt < wlen && !parens(*wptr) && !infixs(*wptr))
		wptr++, wcnt++, tcnt++;
	    tnode = cons(make_colon(intern(make_strnode(tptr, whead, tcnt,
				    wtyp, strnzcpy))), NIL);
	} else if (wcnt == 0 && *wptr == '-' && monadic_minus == FALSE &&
		   wcnt+1 < wlen && !white_space(*(wptr+1))) {
	/* minus sign with space before and no space after is unary */
	    tnode = cons(make_intnode((FIXNUM)0), NIL);
	    monadic_minus = TRUE;
	} else if (parens(*wptr) || infixs(*wptr)) {
	    if (monadic_minus)
		tnode = cons(Minus_Tight, NIL);
	    else
		tnode = cons(intern(make_strnode(wptr, whead, 1,
						 STRING, strnzcpy)), NIL);
	    monadic_minus = FALSE;
	    wptr++, wcnt++;
	} else {
	    tcnt = 0;
	    tptr = wptr;
	    /* isnumb 4 means nothing yet;
		 * 0 means digits so far, 1 means just saw
	     * 'e' so minus can be next, 2 means no longer
	     * eligible even if an 'e' comes along */
	    isnumb = 4;
	    gotdot = 0;
	    if (*wptr == '?') {
		isnumb = 3; /* turn ?5 to (? 5) */
		wptr++, wcnt++, tcnt++;
	    }
	    while (wcnt < wlen && !parens(*wptr) &&
		   (!infixs(*wptr) || (isnumb == 1 && (*wptr == '-' || *wptr == '+')))) {
		if (isnumb == 4 && isdigit(*wptr)) isnumb = 0;
		if (isnumb == 0 && tcnt > 0 && (*wptr == 'e' || *wptr == 'E'))
		    isnumb = 1;
		else if (!(isdigit(*wptr) || (!gotdot && *wptr == '.')) || isnumb == 1)
		    isnumb = 2;
		if (*wptr == '.') gotdot++;
		wptr++, wcnt++, tcnt++;
	    }
	    if (isnumb == 3 && tcnt > 1) {    /* ?5 syntax */
		NODE *qmtnode;

		qmtnode = cons_list(0, Left_Paren, Query,
				    cnv_node_to_numnode
					(make_strnode(tptr+1, whead,
						      tcnt-1, wtyp, strnzcpy)),
				    END_OF_LIST);
		if (outline == NIL) {
		    outline = qmtnode;
		} else {
		    setcdr(lastnode, qmtnode);
		}
		lastnode = cddr(qmtnode);
		tnode = cons(Right_Paren, NIL);
	    } else if (isnumb < 2 && tcnt > 0) {
		tnode = cons(cnv_node_to_numnode(make_strnode(tptr, whead, tcnt,
							      wtyp, strnzcpy)),
			     NIL);
	    } else
		tnode = cons(intern(make_strnode(tptr, whead, tcnt,
						 wtyp, strnzcpy)),
			     NIL);
	}

	if (outline == NIL) outline = tnode;
	else setcdr(lastnode, tnode);
	lastnode = tnode;
    }
    return(outline);
}

NODE *runparse(NODE *ndlist) {
    NODE *curnd = NIL, *outline = NIL, *tnode = NIL, *lastnode = NIL;

    if (nodetype(ndlist) == RUN_PARSE)
		return parsed__runparse(ndlist);
	if (!is_list(ndlist)) {
		err_logo(BAD_DATA_UNREC, ndlist);
		return(NIL);
	}
    while (ndlist != NIL) {
	curnd = car(ndlist);
	ndlist = cdr(ndlist);
	if (!is_word(curnd))
	    tnode = cons(curnd, NIL);
	else {
	    if (!numberp(curnd))
		tnode = runparse_node(curnd, &ndlist);
	    else
		tnode = cons(cnv_node_to_numnode(curnd), NIL);
	}
	if (tnode != NIL) {
	    if (outline == NIL) outline = tnode;
	    else setcdr(lastnode, tnode);
	    lastnode = tnode;
	    while (cdr(lastnode) != NIL) {
		lastnode = cdr(lastnode);
		if (check_throwing) break;
	    }
	}
	if (check_throwing) break;
    }
    return(outline);
}

NODE *lrunparse(NODE *args) {
    NODE *arg;

    arg = car(args);
    while (nodetype(arg) == ARRAY && NOT_THROWING) {
	setcar(args, err_logo(BAD_DATA, arg));
	arg = car(args);
    }
    if (NOT_THROWING && !aggregate(arg))
	arg = parser(arg, TRUE);
    if (NOT_THROWING)
	return runparse(arg);
    return UNBOUND;
}
