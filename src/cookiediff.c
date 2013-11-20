/*

	This module tracks the difference in cookies sent by the browser vs. the ones that
	the browser is supposed to send on the request. The difference will then be sent
	back to the browser as a "Set-Cookie:" on the response. In this way,
	we can be assured that any JavaScripts accessing cookies will have
	the correct cookies to work with
*/
#include "cookiediff.h"
#include <string.h>
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>



/**
 * Match two cookies, each formatted as a string containing:
 * <name>=<value>; ...
 */
static unsigned
MATCHES_COOKIE(const char *lhs, const char *rhs)
{
	unsigned lhs_length;
	unsigned rhs_length;
	unsigned l,r;

	/* Find the length of the left name */
	for (l=0; lhs[l] && lhs[l] != '\n' && lhs[l] != '='; l++)
		;
	lhs_length = l;
	while (lhs_length && isspace(lhs[lhs_length-1]))
		lhs_length--; /* remove trailing whitespace */
	if (lhs[l] == '=')
		l++;
	while (lhs[l] != '\n' && isspace(lhs[l]))
		l++;

	/* Find the length of the right name */
	for (r=0; rhs[r] && rhs[r] != '\n' && rhs[r] != '='; r++)
		;
	rhs_length = r;
	while (rhs_length && isspace(rhs[rhs_length-1]))
		rhs_length--; /* remove trairing whitespace */
	if (rhs[r] == '=')
		r++;
	while (rhs[r] != '\n' && isspace(rhs[r]))
		r++;

	/* compare the names */
	if (lhs_length != rhs_length)
		return 0;
	if (memcmp(lhs, rhs, rhs_length) != 0)
		return 0;

	return 1;
}

static unsigned 
MATCHES_HEADER(const char *p, const char *name)
{
	unsigned i;

	for (i=0; p[i] && name[i]; i++) {
		if (tolower(p[i]) != tolower(name[i]))
			return 0;
		if (p[i] == ':')
			return 1;
	}
	return 0;
}

/**
 * Find the first cookie in a cookie buffer. We expect the buffer
 * to simply be formatted with <name>=<value>; pairs. This simply
 * returns the first non-whitespace character.
 */
const char *cdiff_first_cookie(const char *cookies)
{
	const char *p;

	for (p=cookies; *p; p++) {
		if (!isspace(*p))
			break;
	}

	return p;
}

static unsigned
MATCHES_ANY(const char *p, unsigned len, const char *str, ...)
{
	va_list marker;
	const char *rhs;

	va_start(marker, str);

	for (rhs=str; rhs; rhs=va_arg(marker, const char *)) {
		unsigned i;
		unsigned rhs_len = strlen(rhs);
		if (len != rhs_len)
			continue;

		for (i=0; i<len && i<rhs_len; i++) {
			if (tolower(p[i]) != tolower(rhs[i]))
				break;
		}
		if (i == len) {
			return 1; /* found */
		}
	}

	return 0; /* not found */
}

/**
 * Find the first Set-Cookie in an HTTP response header
 */
const char *cdiff_first_setcookie(const char *httpresponse)
{
	const char *p = httpresponse;

	while (*p != '\n') {
		/* Skip whitespace */
		while (*p != '\n' && isspace(*p))
			p++;

		if (MATCHES_HEADER(p, "Set-Cookie:")) {
			/* Skip "Set-Cookie:" string */
			p += 11; 

			/* Skip whitespace */
			while (*p != '\n' && isspace(*p))
				p++;

			/* Go forward until we find a cookie. We are going to
			 * skip the fields "domain=<domain>;", "path=<path>;",
			 * "secure;", and "HttpOnly;" */
			while (*p != '\n') {
				unsigned i;

				/* Go forward until next delimeter, then backward skipping
				 * any whitespace that might be before the delimeter */
				for (i=0; p[i] && p[i]!='\n' && p[i]!='=' && p[i]!=';'; i++)
					;
				while (i && isspace(p[i-1]))
					i--;

				if (MATCHES_ANY(p,i,"path","domain","secure","httponly",0)) {
					/* Goto the next one */
					while (*p != '\n' && *p != ';')
						p++;
					if (*p == ';')
						p++;
					while (*p != '\n' && isspace(*p))
						p++;
					continue; /* go to next <name>=<value>; pair in the line */
				}

				/* Found a cookie, so return it */
				return p;
			}

		}

		/* Skip until end-of-line */
		while (*p != '\n')
			p++;
		if (*p == '\n')
			p++;
		while (*p == '\r')
			p++;
	}

	/* No cookies found */
	return 0;
}

/**
 * See if the HTTP header contains the specified Set-Cookie value
 */
unsigned cdiff_contains_setcookie(const char *header, const char *cookie)
{
	const char *p;

	for (p=cdiff_first_setcookie(header); *p != '\n'; p=cdiff_next_setcookie(p)) {
		if (MATCHES_COOKIE(p,cookie)) {
			/* FIXME: compare the 'domain' and 'path' values as well */
			return 1; /* found */
		}
	}
	return 0; /* not found */
}

/**
 * Remove the specified cookie from the header
 */
void cdiff_remove_setcookie(char *header, const char *cookie)
{
	char *p;
	char *p2;
	char *end;

	/* First, find the location of the cookie in the header */
	for (p=(char*)cdiff_first_setcookie(header); *p != '\n'; p=(char*)cdiff_next_setcookie(p)) {
		if (MATCHES_COOKIE(p,cookie)) {
			break;
		}
	}
	if (*p == '\n')
		return;

	/* Find the end of the cookie */
	for (p2=p; *p2 && *p2 != '\n' && *p2 != ';'; p2++)
		;
	if (*p2 == ';')
		p2++;
	while (*p2 != '\r' && *p2 != '\n' && isspace(*p2))
		p2++;

	/* Find the end of the buffer */
	end=p2;
	if (*end == '\n')
		end++;
	while (*end == '\r')
		end++;
	while (*end != '\n' && *end) {
		while (*end && *end != '\n')
			end++;
		if (*end == '\n')
			end++;
		while (*end == '\r')
			end++;
	}
	if (*end == '\n')
		end++;

	/* Move the data */
	memmove(p, p2, (end-p2)+1);
	memset(end-(p2-p), 0, (p2-p)+1);

}

const char *cdiff_next_setcookie(const char *p)
{
	/* Assume that 'p' points to an existing cookie. We will
	 * skip past the cookie until we reach the ending ';'
	 * terminator */
	if (*p && *p != '\n' && *p != ';') {
		while (*p != '\n' && *p != ';')
			p++;
	}

	/* We have reached the ';' terminator. Now skip that
	 * and trailing whitespace.*/
	while (*p == ';') {
		const char *q;

		/* Skip past end and go to next one */
		if (*p == ';')
			p++;

		/* Skip trailing whitespace after the end of the last cookie */
		while (*p && *p != '\n' && isspace(*p))
			p++;

		/* Grab the end of the <name> of this next cookie */
		for (q=p; *q && *q != '\n' && *q != ';' && *q != '='; q++)
			;


		/* See if this next cookie looks like a valie <name>=<value> pair */
		if (*q == '=') {
			/* Ignore trailing whitespace after the name */
			while (q>p && isspace(*(q-1)))
				q--;

			if (!MATCHES_ANY(p,(q-p),"path","domain","secure","httponly",0)) {
				return p;
			}
		} else {
			/* We don't have a cookie here, so loop again until we find one */
			;
		}

		/* We didn't find a legitimate cookie, so skip to the end */
		p=q;
		while (*p && *p != '\n' && *p != ';')
			p++;
	}

	/* At this point, we should have found the end-of-line */
	if (*p && *p != '\n') {
		printf("parse error\n");
		return 0;
	} else
		p++;

	/* Go forward until we find a "Set-Cookie: header */
	for (;;) {
		/* Skip whitespace */
		while (*p && *p != '\n' && isspace(*p))
			p++;

		if (MATCHES_HEADER(p, "Set-Cookie:")) {
			/* Skip "Set-Cookie:" string */
			p += 11; 
		}

		/* Skip whitespace */
		while (*p && *p != '\n' && isspace(*p))
			p++;

		/* Go forward until we find a cookie. We are going to
		 * skip the fields "domain=<domain>;", "path=<path>;",
		 * "secure;", and "HttpOnly;" */
		while (*p && *p != '\n') {
			unsigned i;

			/* Go forward until next delimeter, then backward skipping
			 * any whitespace that might be before the delimeter */
			for (i=0; p[i] && p[i]!='\n' && p[i]!='=' && p[i]!=';'; i++)
				;
			while (i && isspace(p[i-1]))
				i--;

			if (MATCHES_ANY(p,i,"path","domain","secure","httponly",0)) {
				/* Goto the next one */
				while (*p && *p != '\n' && *p != ';')
					p++;
				if (*p == ';')
					p++;
				while (*p && *p != '\n' && isspace(*p))
					p++;
				continue; /* go to next <name>=<value>; pair in the line */
			}

			/* Found a cookie, so return it */
			return p;
		}

		/* Skip until end-of-line */
		while (*p && *p != '\n')
			p++;
		if (*p == '\n')
			p++;
	}

	/* No cookies found */
	return 0;
}




/**
 * Grab the next cookie in the cookie buffer after the current
 * cookie.
 */
const char *cdiff_next_cookie(const char *p)
{
	/* Skip name */
	while (*p && *p != '=')
		p++;
	
	/* skip equals */
	if (*p && *p == '=')
		p++;

	/* skip value */
	while (*p && *p != ';')
		p++;

	/* skip trailing semicolon */
	if (*p && *p == ';')
		p++;

	/* skip trailing whitespace */
	while (*p && isspace(*p))
		p++;

	return p;
}

/**
 * Get the next cookie from the header line
 */
static char *
header_next_cookie(char *p)
{
	/* Skip name */
	while (*p != '\n' && *p != '=')
		p++;
	
	/* skip equals */
	if (*p == '=')
		p++;

	/* skip value */
	while (*p != '\n' && *p != ';')
		p++;

	/* skip trailing semicolon */
	if (*p == ';')
		p++;

	/* skip trailing whitespace */
	while (*p != '\n' && isspace(*p))
		p++;

	return p;
}




/**
 * Remove a cooke at the specified location
 */
static void
cookie_remove(char *p)
{
	char *p2 = header_next_cookie(p);
	char *end;

	/* Find the end of the buffer */
	end = p2;
	while (*end != '\n')
		end++;
	if (*end == '\n')
		end++;
	while (*end == '\r')
		end++;

	while (*end != '\n') {
		while (*end != '\n')
			end++;
		if (*end == '\n')
			end++;
		while (*end == '\r')
			end++;
	}
	if (*end == '\n')
		end++;

	/* Now move the buffer */
	memmove(p, p2, end-p2+1);
	memset(end-(p2-p), 0, (p2-p));
}

/**
 * Remove a header line from an HTTP header
 */
static void
header_line_remove(char *p)
{
	char *p2;
	char *end;

	/* Find the next header line */
	p2 = p;
	while (*p2 != '\n')
		p2++;
	if (*p2 == '\n')
		p2++;

	/* Find the end of the buffer */
	end = p2;
	while (*end == '\r')
		end++;

	while (*end != '\n') {
		while (*end != '\n')
			end++;
		if (*end == '\n')
			end++;
		while (*end == '\r')
			end++;
	}
	if (*end == '\n')
		end++;

	/* Now move the buffer */
	memmove(p, p2, end-p2+1);
	memset(end-(p2-p), 0, (p2-p));
}

/**
 * See if we've removed all the cookies from this line
 */
static unsigned
header_line_empty(const char *p)
{
	while (*p != '\n' && *p != ':')
		p++;
	if (*p == ':')
		p++;
	while (*p != '\n' && isspace(*p))
		p++;
	if (*p == '\n')
		return 1; /* is empty */
	else
		return 0; /* is not empty */
}

/**
 * Remove any cookies in the HTTP header buffer that match the specified
 * cookie
 */
void cdiff_remove_cookie(char *hdr, unsigned *r_length, const char *cookie)
{
	char *p;

	/* skip first line */
	for (p=hdr; *p && *p != '\n'; p++)
		;
	if (*p == '\n')
		p++;

	/* For all header lines */
	for (;;) {
		char *header_line;

		/* skip leading whitespace */
		while (*p && *p != '\n' && isspace(*p))
			p++;

		/* If this is an empty line, stop */
		if (*p == '\n')
			break;

		/* If this isn't a cookie header, skip it and go
		 * to next */
		if (!MATCHES_HEADER(p, "Cookie:")) {
			while (*p != '\n')
				p++;
			p++;
			continue;
		}

		/* Skip the beginning "Cookie:" */
		header_line = p;
		p += 7;

		/* Skip whitespace */
		while (*p != '\n' && isspace(*p))
			p++;

		/* For all cookies in this line, see if they match */
		while (*p != '\n') {
			if (MATCHES_COOKIE(p, cookie)) {
				cookie_remove(p);
			} else
				p = header_next_cookie(p);
		}
		if (*p == '\n')
			p++;

		if (header_line_empty(header_line)) {
			header_line_remove(header_line);
			p = header_line;
		}
	}

	/* skip trailing '\n' */
	if (*p == '\n')
		p++;
	*r_length = (p-hdr);
}

struct CookieDiffItem
{
	char *name;
	unsigned name_length;
	char *value;
	unsigned value_length;
};

struct CookieDiff
{
	struct CookieDiffItem **items;
	unsigned item_count;
};

/**
 * Create a new cookie diff object. This will have a lifetime of only
 * a single request/response pair
 */
struct CookieDiff *
cookiediff_new()
{
	struct CookieDiff *result;

	result = (struct CookieDiff*)malloc(sizeof(*result));
	memset(result, 0, sizeof(*result));

	return result;	
}

void
cookiediff_destroy(struct CookieDiff *d)
{
	unsigned i;
	for (i=0; i<d->item_count; i++) {
		struct CookieDiffItem *item = d->items[i];
		free(item->name);
		free(item->value);
		free(item);
	}
	free(d->items);
	free(d);
}

static unsigned
MATCHES(const char *lhs, unsigned lhs_length, const char *rhs, unsigned rhs_length)
{
	if (lhs_length != rhs_length)
		return 0;
	return memcmp(lhs, rhs, rhs_length) == 0;
}

/**
 * Add a request Cookie: to the list of cookies to track in the 
 * response
 */
void
cookiediff_add(struct CookieDiff *d, const char *name, unsigned name_length, const char *value, unsigned value_length)
{
	unsigned i;

	/* Check for duplicates */
	for (i=0; i<d->item_count; i++) {
		struct CookieDiffItem *item = d->items[i];
		if (MATCHES(name, name_length, item->name, item->name_length)) {
			if (MATCHES(value, value_length, item->value, item->value_length)) {
				/* We have found a match, this is a duplicate cookie for
				 * some reason, so ignore it */
				return;
			}
		}
	}

	/* Now update the list and add the cookie */
	{
		struct CookieDiffItem **new_list = (struct CookieDiffItem**)malloc((d->item_count+1) * sizeof(*new_list));
		struct CookieDiffItem *item = (struct CookieDiffItem*)malloc(sizeof(*item));
		new_list[d->item_count] = item;
		free(d->items);
		d->items = new_list;

		item->name = (char*)malloc(name_length+1);
		memcpy(item->name, name, name_length);
		item->name[name_length] = '\0';
		item->name_length = name_length;
		item->value = (char*)malloc(value_length+1);
		memcpy(item->value, value, value_length);
		item->value[value_length] = '\0';
		item->value_length = value_length;

		d->item_count++;
	}
}

/**
 * Remove a cookie from the list, telling us that it's not important
 * for the response.
 */
void
cookiediff_remove(struct CookieDiff *d, const char *name, unsigned name_length, const char *value, unsigned value_length)
{
	unsigned i;

	/* Check for duplicates */
	for (i=0; i<d->item_count; i++) {
		struct CookieDiffItem *item = d->items[i];
		if (MATCHES(name, name_length, item->name, item->name_length)) {
			if (MATCHES(value, value_length, item->value, item->value_length)) {
				free(item->name);
				free(item->value);
				free(item);
				memmove(d->items+i, d->items+i+1, d->item_count-i-1);
				d->item_count--;
			}
		}
	}

	/* not found, but that's ok */
}


/**
 * Parse an HTTP Cookie: header and add the cookies we find to our list.
 * This will likely be called by the proxy to tell us the cookies that
 * it is adding to the HTTP request
 */
void
cookiediff_remember_cookies(struct CookieDiff *d, const void *in_cookies, unsigned length)
{
	const char *px = (const char *)in_cookies;
	unsigned i = 0;

	if (memcmp(in_cookies, "Cookie:", 7)==0)
		i = 7;

	for (; i<length; i++) {
		const char *name;
		unsigned name_length;
		const char *value;
		unsigned value_length;

		/* Remove leading whitespace */
		while (i<length && isspace(px[i]))
			i++;

		/* Grab the <name> */
		name = px+i;
		for (name_length=0; i+name_length<length; i++) {
			char c = px[i+name_length];
			if (c == '\n' || c == '\r' || c == ';' || c == '=')
				break;
		}
		while (name_length && isspace(name[name_length-1]))
			name_length--; /* remove trailing whitespace from name */
		i += name_length;
		if (i<length && px[i]=='=')
			i++;
		while (i<length && isspace(px[i]))
			i++;


		/* Grab the <value> */
		value = px+i;
		for (value_length=0; i+value_length<length; i++) {
			char c = px[i+value_length];
			if (c == '\n' || c == '\r' || c == ';')
				break;
		}
		while (value_length && isspace(value[value_length-1]))
			value_length--; /* remove trailing whitespace from the <value> */
		i += value_length;
		if (i<length && px[i]==';')
			i++;
		while (i<length && isspace(px[i]))
			i++;

		/* Add this <name>=<value>; pair to our list of cookies */
		cookiediff_add(d, name, name_length, value, value_length);
	}
}



/**
 * Parse an HTTP Cookie: header and REMOVE the cookies from our list. This
 * will probably be the cookies that the browser sent to us. This will tell
 * us that there is no need to do a Set-Cookie: in the response for this 
 * value because the browser already knows the cookie
 */
void
cookiediff_request_remove_cookies(struct CookieDiff *d, const void *in_cookies, unsigned length)
{
	const char *px = (const char *)in_cookies;
	unsigned i = 0;

	if (memcmp(in_cookies, "Cookie:", 7)==0)
		i = 7;

	for (; i<length; i++) {
		const char *name;
		unsigned name_length;
		const char *value;
		unsigned value_length;

		/* Remove leading whitespace */
		while (i<length && isspace(px[i]))
			i++;

		/* Grab the <name> */
		name = px+i;
		for (name_length=0; i+name_length<length; i++) {
			char c = px[i+name_length];
			if (c == '\n' || c == '\r' || c == ';' || c == '=')
				break;
		}
		while (name_length && isspace(name[name_length-1]))
			name_length--; /* remove trailing whitespace from name */
		i += name_length;
		if (i<length && px[i]=='=')
			i++;
		while (i<length && isspace(px[i]))
			i++;


		/* Grab the <value> */
		value = px+i;
		for (value_length=0; i+value_length<length; i++) {
			char c = px[i+value_length];
			if (c == '\n' || c == '\r' || c == ';')
				break;
		}
		while (value_length && isspace(value[value_length-1]))
			value_length--; /* remove trailing whitespace from the <value> */
		i += value_length;
		if (i<length && px[i]==';')
			i++;
		while (i<length && isspace(px[i]))
			i++;

		/* Remove this <name>=<value>; pair to our list of cookies */
		cookiediff_remove(d, name, name_length, value, value_length);
	}
}


/**
 * Parse the HTTP header for any cookies it contains;
 */
void
cookiediff_forget_cookies_from_header(struct CookieDiff *d, const void *hdr)
{
	const char *p = (const char*)hdr;

	/* Skip the first line */
	while (*p != '\n')
		p++;
	if (*p == '\n')
		p++;

	/* For all header lines */
	for (;;) {
		char *header_line;

		/* skip leading whitespace */
		while (*p && *p != '\n' && isspace(*p))
			p++;

		/* If this is an empty line, stop */
		if (*p == '\n')
			break;

		/* If this isn't a cookie header, skip it and go
		 * to next */
		if (!MATCHES_HEADER(p, "Cookie:")) {
			while (*p != '\n')
				p++;
			p++;
			continue;
		}

		/* Skip the beginning "Cookie:" */
		p += 7;

		/* Skip whitespace */
		while (*p != '\n' && isspace(*p))
			p++;

		/* Remember where the line starts*/
		header_line = (char*)p;

		/* Find end of line */
		while (*p != '\n')
			p++;
		if (*p == '\n')
			p++;

		cookiediff_request_remove_cookies(d, header_line, (p-header_line));
	}
}

