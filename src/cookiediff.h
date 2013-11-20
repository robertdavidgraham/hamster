#ifndef COOKIEDIFF_H
#define COOKIEDIFF_H
#ifdef __cplusplus
extern "C" {
#endif

const char *cdiff_first_cookie(const char *cookies);
const char *cdiff_next_cookie(const char *p);
void cdiff_remove_cookie(char *px, unsigned *r_length, const char *cookie);

const char *cdiff_first_setcookie(const char *cookies);
const char *cdiff_next_setcookie(const char *p);
void cdiff_remove_setcookie(char *px, const char *cookie);
unsigned cdiff_contains_setcookie(const char *headers, const char *cookie);

struct CookieDiff *cookiediff_new();
void cookiediff_destroy(struct CookieDiff *d);


/**
 * Called to remember the cookies that we have to set in the 
 * the request header. We will then use this information to do
 * a Set-Cookie: in the response back to the browser so that
 * the browser also knows the correct cookies.
 */
void
cookiediff_remember_cookies(struct CookieDiff *d, const void *in_cookies, unsigned length);

/**
 * Called to remove from our list the cookies that the browser
 * already knows about, so that we don't override them. This is because
 * we have imperfect knowlege of the cookies (we don't know the exact
 * domain and path often, nor 'secure' or 'httponly' flags, or expiration).
 */
void cookiediff_forget_cookies_from_header(struct CookieDiff *d, const void *hdr);


#ifdef __cplusplus
}
#endif
#endif /*COOKIEDIFF_H*/
