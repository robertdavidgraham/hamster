#ifndef __COOKIEDB_H
#define __COOKIEDB_H
#ifdef __cplusplus
extern "C" {
#endif

extern unsigned cookiedb_record_count;
extern unsigned cookiedb_packet_count;


void coookiedb_read_file(const char *filename, void *v);
char *cookiedb_get_instance_list();
unsigned cookiedb_get_instance_count();
char *cookiedb_get_url_list(const char *instance);
char *cookiedb_get_url2_list(const char *instance);
char *cookiedb_get_userid_list(const char *instance);
char *cookiedb_get_cookie_list(const char *instance);
int cookiedb_is_empty(const char *instance);
void cookiedb_free(void *p);
void cookiedb_SET_COOKIE(const char *instance, const char *in_domain, const char *buf, unsigned buf_length);

char *cookiedb_GET_COOKIE(const char *instance, const char *domain, const char *path);
char *cookiedb_GET_REFERER(const char *instance, const char *domain, const char *path);
char *cookiedb_GET_SETCOOKIE(const char *instance, const char *domain, const char *path);



#ifdef __cplusplus
}
#endif
#endif /*__COOKIEDB_H*/
