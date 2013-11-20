#include "platform.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#include "hamster.h"
#include "cookiedb.h"
//#include "debugconnection.h"
//#include "serveconsole.h"
#include "mongoose.h"
#include "pixie.h"
#include "cookiediff.h"

static unsigned
contains(const void *vpx, unsigned length, const char *sz)
{
	const unsigned char *px = (const unsigned char*)vpx;
	unsigned sz_length = (unsigned)strlen(sz);
	unsigned offset=0;

	if (length < sz_length)
		return 0;
	length -= sz_length;

	while (offset<length) {
		if (px[offset] == sz[0] && memcmp(px+offset, sz, sz_length) == 0)
			return 1;
		offset++;
	}

	return 0;
}

static void header_truncate(struct HttpHeader *header)
{
	memmove(header->buf, header->buf+header->end_of_header, header->max-header->end_of_header);
	header->max -= header->end_of_header;
	header->end_of_header = 0;
}

/**
 * Find the end of the HTTP header (a blank line), or zero if no
 * end-of-line was found
 */
static unsigned 
header_find_length(const unsigned char *header, unsigned length)
{
	unsigned i;
	unsigned found_eol = 0;

	for (i=0; i<length; i++) {
		if (header[i] == '\n') {
			if (found_eol)
				return i+1;
			else
				found_eol = 1;
		} else if (header[i] == '\r') {
			;
		} else
			found_eol = 0;
	}

	return 0;
}

static void
header_remove_value(struct HttpHeader *header, const char *name)
{
	unsigned i;
	unsigned name_length = strlen(name);

	for (i=0; i<header->end_of_header; i++) {
		if (header->buf[i] != '\n')
			continue;
		i++;
		if (i + name_length >= header->end_of_header)
			break;

		if (strnicmp(name, (const char*)header->buf+i, name_length) == 0) {
			unsigned j;

			for (j=i; j<header->end_of_header && header->buf[j] != '\n'; j++)
				;
			if (j<header->end_of_header)
				j++;

			memmove(header->buf+i, header->buf+j, header->max-j);
			header->max -= j-i;
			memset(header->buf+header->max, 0, j-i);
			header->end_of_header -= j-i;
			return;
		}
	}
}

static void
header_add_value(struct HttpHeader *header, const char *name, const char *value)
{
	unsigned char *buf = header->buf;
	unsigned name_length = strlen(name);
	unsigned value_length = strlen(value);
	unsigned end = header->end_of_header;

	if (end > 0 && buf[end-1] == '\n')
		end--;
	if (end > 0 && buf[end-1] == '\r')
		end--;

	memmove(buf+end +name_length+1+value_length+2,
			buf+end ,
			header->max - end );
	memcpy(buf+end , 
		name, name_length);
	memcpy(buf+end +name_length, 
		" ", 1);
	memcpy(buf+end +name_length+1, 
		value, value_length);
	memcpy(buf+end +name_length+1+value_length, 
		"\r\n", 2);

	header->end_of_header += name_length+1+value_length+2;
	header->max += name_length+1+value_length+2;
}

/**
 * Extract a "Name: value" pair from the HTTP header
 */
static void 
header_extract_value(struct HttpHeader *header, const char *name, const unsigned char **r_value, unsigned *r_value_length)
{
	unsigned i;
	unsigned name_length = strlen(name);

	*r_value = 0;
	*r_value_length = 0;

	for (i=0; i<header->end_of_header; i++) {
		if (header->buf[i] != '\n')
			continue;
		i++;
		if (i + name_length >= header->end_of_header)
			break;

		if (strnicmp(name, (const char*)header->buf+i, name_length) == 0) {
			i += name_length;

			while (i<header->end_of_header && (isspace(header->buf[i]) || header->buf[i]==':'))
				i++;
			if (i>=header->end_of_header)
				break;

			*r_value = header->buf+i;
			while (i<header->end_of_header && header->buf[i] != '\n') {
				(*r_value_length)++;
				i++;
			}

			while (*r_value_length && isspace((*r_value)[(*r_value_length)-1]))
				(*r_value_length)--;
			return;
		}
	}
}

static void 
header_SET_COOKIE(struct MyProx *myprox, const char *host, struct HttpHeader *header)
{
	unsigned i;
	const char *name = "SET-COOKIE:";
	unsigned name_length = strlen(name);
	char *value;
	unsigned value_length;

	value = 0;
	value_length = 0;

	for (i=0; i<header->end_of_header; i++) {
		if (header->buf[i] != '\n')
			continue;
		i++;
		if (i + name_length >= header->end_of_header)
			break;

		if (strnicmp(name, (const char*)header->buf+i, name_length) == 0) {
			i += name_length;

			while (i<header->end_of_header && (isspace(header->buf[i]) || header->buf[i]==':'))
				i++;
			if (i>=header->end_of_header)
				break;

			value = (char *)header->buf+i;
			while (i<header->end_of_header && header->buf[i] != '\n') {
				(value_length)++;
				i++;
			}

			while (value_length && isspace((value)[(value_length)-1]))
				(value_length)--;

			cookiedb_SET_COOKIE(myprox->m_instance, host, value, value_length);

			value = 0;
			value_length = 0;
		}
	}
}

struct HttpHeader *header_receive(struct MYSOCK browser)
{
	struct HttpHeader *hdr;

	/*
	 * Allocate the header
	 */
	hdr = (struct HttpHeader*)malloc(sizeof(*hdr));
	memset(hdr, 0, sizeof(*hdr));
	hdr->buf = malloc(MAX_HEADER_SIZE);
	if (hdr->buf == NULL) {
		fprintf(stderr, "%s: memory allocation error\n", "myprox");
		goto cleanup;
	}

	/*
	 * Receive the header data
	 */
	/* Read the incoming HTTP header */
	hdr->max = 0;
	for (;;) {
		int bytes_read;
		
		bytes_read = recv(browser.fd, (char*)hdr->buf+hdr->max, MAX_HEADER_SIZE-hdr->max, 0);

		/* See if there is an error */
		if (bytes_read < 0) {
			fprintf(stderr, "%s: read() error on connection from %u.%u.%u.%u:%u\n",
				"myprox",
				P_IP_ADDR(browser.ip), browser.port);
			goto cleanup;
		}

		/* See if there is a normal socket close */
		if (bytes_read == 0) {
			if (hdr->max)
				fprintf(stderr, "%s: read() unexpected close from %u.%u.%u.%u:%u\n",
					"myprox",
					P_IP_ADDR(browser.ip), browser.port);
			goto cleanup;
		}

		hdr->max += bytes_read;

		/* See if we've reached the end of the HTTP header */
		hdr->end_of_header = header_find_length(hdr->buf, hdr->max);
		if (hdr->end_of_header)
			break;
	}


	return hdr;
cleanup:
	if (hdr && hdr->buf)
		free(hdr->buf);
	if (hdr)
		free(hdr);
	return 0;
}

/**
 * Read either from the buffered data left over from reading the header, 
 * or from the socket.
 */
static int 
read_bytes(int fd, struct HttpHeader *header, void *buf, unsigned bytes_to_read)
{
	if (header->max > 0) {
		unsigned len = header->max;
		if (len > bytes_to_read)
			len = bytes_to_read;

		memmove(buf, header->buf, len);
		memmove(header->buf, header->buf+len, header->max-len);
		header->max -= len;
		return len;
	} else {
		return recv(fd, buf, bytes_to_read, 0);
	}
}

static void 
header_append_sz(struct HttpHeader *hdr, const char *str)
{
	unsigned len = strlen(str);

	if (hdr->max + len + 1> MAX_HEADER_SIZE)
		return;
	memcpy(hdr->buf+hdr->max, str, len+1);
	hdr->max += len;
}
static void 
header_append_encoded(struct HttpHeader *hdr, const char *str)
{
	unsigned len = strlen(str);
	unsigned i;

	for (i=0; i<len; i++) {
		char c = str[i];

		if (isprint(c) && c != '?' && c != '%' && hdr->max + len + 1 < MAX_HEADER_SIZE) {
			hdr->buf[hdr->max] = c;
			hdr->max++;
		} else if (hdr->max + len + 3 + 1 < MAX_HEADER_SIZE) {
			hdr->buf[hdr->max+0] = '%';
			hdr->buf[hdr->max+1] = "0123456789abcdef"[(c>>4)&0x0f];
			hdr->buf[hdr->max+2] = "0123456789abcdef"[(c>>0)&0x0f];
		}
	}
	hdr->buf[hdr->max] = '\0'; /* keep nul terminated */
}
static void 
header_append_parm(struct HttpHeader *hdr, const char *name, const char *value)
{
	header_append_sz(hdr, name);
	header_append_sz(hdr, ": ");
	header_append_sz(hdr, value);
	header_append_sz(hdr, "\r\n");
}


/**
 * After handling the request side of the connection, we then call
 * this function to handle proxying the response.
 */
static void 
handle_response2(struct MyProx *myprox, struct mg_connection *conn, struct MYSOCK server, struct CookieDiff *track_cookies, char *response_setcookies)
{
	struct HttpHeader *header;
	unsigned content_length = 0;
	unsigned is_no_content_length = 1;
	unsigned is_chunked_encoding = 0;

	/*
	 * Receive the header from the server
	 */
	header = header_receive(server);
	if (header == NULL)
		goto cleanup;
	//header_remove_value(header, "CONNECTION:");


	/*
	 * Handle incoming cookies back from the server.
	 */
	header_SET_COOKIE(myprox, server.hostname, header);

	/*
	 * Add in any "Set-Cookies:" that we need
	 */
	if (0 && response_setcookies) {
		const char *p;

		/* First, remove any cookies in this spec that are already coming back from the
		 * server. */
		for (p=cdiff_first_setcookie((char*)header->buf); p && *p && *p != '\n'; p=cdiff_next_setcookie(p)) {
			if (cdiff_contains_setcookie(response_setcookies, p))
				cdiff_remove_setcookie(response_setcookies, p);
		}
	}
	

	/*
	 * Get Content-Length that we'll need to use to continue sending bytes
	 */
	content_length = 0;
	{
		const unsigned char *val;
		unsigned val_length;

		header_extract_value(header, "CONTENT-LENGTH:", &val, &val_length);
		if (val != NULL && val_length > 0 && isdigit(val[0])) {
			content_length = strtoul((const char*)val,0,0);
			is_no_content_length = 0;
		}
	}

	if (content_length == 0) {
		const unsigned char *val;
		unsigned val_length;

		header_extract_value(header, "TRANSFER-ENCODING:", &val, &val_length);
		if (val != NULL && val_length >= 7 && strnicmp((const char*)val, "chunked", 7) == 0) {
			is_chunked_encoding = 1;
		}
	}

	/*
	 * Send the header up to the server
	 */
	if (mg_write(conn, (const char*)header->buf, header->end_of_header) != (int)header->end_of_header) {
		fprintf(stderr, "%s: send() to browser prematurely ended, err=%d\n",
			"myprox.response", WSAGetLastError());
		goto cleanup;
	}
	/*
	 * Remove the header from the buffer.
	 */
	header_truncate(header);

	/*
	 * Handle chunked encoding
	 */
	while (is_chunked_encoding) {
		unsigned chunk_length = 0;
		int bytes_read;
		int new_chunk = 1;

		/* Read the chunk length */
		for (;;) {
			char c;

			/* Read the chunk_length from the input (and echo it to the browser)  */
			bytes_read = read_bytes(server.fd, header, &c, 1);
			if (bytes_read == 0) {
				fprintf(stderr, "%s: read() from server %u.%u.%u.%u:%u prematurely ended\n",
					"",
					P_IP_ADDR(server.ip), server.port);
				goto cleanup;
			} else if (bytes_read < 0) {
				fprintf(stderr, "%s: read() from server %u.%u.%u.%u:%u prematurely ended, err=%d\n",
					"",
					P_IP_ADDR(server.ip), server.port, WSAGetLastError());
				goto cleanup;
				break;
			} else {
				if (mg_write(conn, &c, bytes_read) != (int)bytes_read) {
					fprintf(stderr, "%s: send() to browser prematurely ended, err=%d\n",
						"",
						WSAGetLastError());
					goto cleanup;
				}
			}

			if (new_chunk && c == '\r')
				continue;
			if (new_chunk && c == '\n') {
				new_chunk = 0;
				continue;
			}
			new_chunk = 0;

			if (c == '\n')
				break;
			if (isspace(c))
				continue;

			{
				unsigned n;

				if ('0' <= c && c <= '9')
					n = c-'0';
				else if ('a' <= c && c <= 'f')
					n = c-'a'+10;
				else if ('A' <= c && c <= 'F')
					n = c-'A'+10;
				else {
					fprintf(stderr, "%s: read() from server %u.%u.%u.%u:%u prematurely ended, err=%d\n",
						"",
						P_IP_ADDR(server.ip), server.port, WSAGetLastError());
					n = 0;
					goto cleanup;
				}

				chunk_length *= 16;
				chunk_length += n;
			}
		}

		if (chunk_length == 0) {
			is_chunked_encoding = 0;
			goto cleanup;
		}

		/* Process the chunk */
		while (chunk_length) {
			char buf[1024];
			int bytes_read;
			int bytes_to_read;

			bytes_to_read = chunk_length;
			if (bytes_to_read > sizeof(buf))
				bytes_to_read = sizeof(buf);

			bytes_read = read_bytes(server.fd, header, buf, bytes_to_read);
			if (bytes_read == 0) {
				fprintf(stderr, "%s: read() from server %u.%u.%u.%u:%u prematurely ended\n",
					"",
					P_IP_ADDR(server.ip), server.port);
				chunk_length = 0;
			} else if (bytes_read < 0) {
				fprintf(stderr, "%s: read() from server %u.%u.%u.%u:%u prematurely ended, err=%d\n",
					"",
					P_IP_ADDR(server.ip), server.port, WSAGetLastError());
				chunk_length = 0;
			} else {
				if (mg_write(conn, buf, bytes_read) != (int)bytes_read) {
					fprintf(stderr, "%s: send() to browser prematurely ended, err=%d\n",
						"",
						WSAGetLastError());
					goto cleanup;
				}
			}

			chunk_length -= bytes_read;
		}
	}

	/*
	 * If there is a content-length AND remaining data in the buffer, send
	 * those bytes now.
	 */
	if ((is_no_content_length || content_length) && header->max) {
		unsigned len = content_length;
		if (len > header->max)
			len = header->max;
		if (is_no_content_length)
			len = header->max;

		if (mg_write(conn, (const char*)header->buf, len) != (int)len) {
			fprintf(stderr, "%s: send() to browser prematurely ended, err=%d\n",
				"myprox",
				WSAGetLastError());
				goto cleanup;
		}

		memmove(header->buf, header->buf+len, header->max-len);
		header->max -= len;
		if (!is_no_content_length)
			content_length -= len;
	}
	if (header->max > 0) {
		fprintf(stderr, "response pipelining error\n");
		goto cleanup;
	}


	/*
	 * While there is more content length to send
	 */
	while (content_length) {
		char buf[1024];
		int bytes_read;
		int bytes_to_read;

		bytes_to_read = content_length;
		if (bytes_to_read > sizeof(buf))
			bytes_to_read = sizeof(buf);

		bytes_read = recv(server.fd, buf, bytes_to_read, 0);
		if (bytes_read == 0) {
			fprintf(stderr, "%s: read() from server %u.%u.%u.%u:%u prematurely ended\n",
				"",
				P_IP_ADDR(server.ip), server.port);
			content_length = 0;
		} else if (bytes_read < 0) {
			fprintf(stderr, "%s: read() from server %u.%u.%u.%u:%u prematurely ended, err=%d\n",
				"",
				P_IP_ADDR(server.ip), server.port, WSAGetLastError());
			content_length = 0;
		} else {
			if (mg_write(conn, buf, bytes_read) != (int)bytes_read) {
				fprintf(stderr, "%s: send() to browser prematurely ended, err=%d\n",
					"",
					WSAGetLastError());
			}
		}

		content_length -= bytes_read;
	}
	while (is_no_content_length) {
		char buf[1024];
		int bytes_read;
		int bytes_to_read;

		bytes_to_read = sizeof(buf);

		bytes_read = recv(server.fd, buf, bytes_to_read, 0);
		if (bytes_read == 0) {
			is_no_content_length = 0;
		} else if (bytes_read < 0) {
			fprintf(stderr, "%s: read() from server %u.%u.%u.%u:%u prematurely ended, err=%d\n",
				"",
				P_IP_ADDR(server.ip), server.port, WSAGetLastError());
			content_length = 0;
			is_no_content_length = 0;
		} else {
			if (mg_write(conn, buf, bytes_read) != (int)bytes_read) {
				fprintf(stderr, "%s: send() to browser prematurely ended, err=%d\n",
					"",
					WSAGetLastError());
			}
		}
	}

cleanup:
	if (header && header->buf)
		free(header->buf);
	if (header)
		free(header);
}



/**
 * Handle a single HTTP request from the browser. This entails:
 * - getting the request from the Mongoose HTTP daemon (which
 *   got it from the browser)
 * - connecting to the server
 * - sending the request to the server
 * - receiving the response from the server
 * - sending the response to the Mongoose HTTP daemon (which 
 *   sends to the browser
 */
void
hamster_proxy(struct mg_connection *conn, const struct mg_request_info *ri,
		void *user_data)
{
	unsigned a3a3a3a3 = 0xa3a3a3a3;
	struct MyProx *myprox = (struct MyProx*)user_data;
	struct HttpHeader *header = 0;
	struct sockaddr_in sin;
	unsigned content_length=0;
	struct MYSOCK server;
	unsigned is_no_content_length = 1;
	unsigned a3a3a3a1 = 0xa3a3a3a1;
	struct CookieDiff *track_cookies = cookiediff_new();
	char *response_setcookies=0; /* cookies that will be set with Set-Cookie: on the response */
	//void * dbgcon=0;


	server.fd = -1;
	server.port = 80;
	server.ip = 0xffffffff;
	snprintf(server.hostname, sizeof(server.hostname), "%s", ri->proxy_host);

	/* Allocate an object for debugging connections */
	//dbgcon = DBGCON_NEW(browser.ip, browser.port);


	/*
	 * Copy the header from the request
	 */
	{
		int i;

		header = (struct HttpHeader*)malloc(sizeof(*header));
		memset(header, 0, sizeof(*header));
		header->buf = malloc(MAX_HEADER_SIZE);
		header_append_sz(header, ri->request_method);
		header_append_sz(header, " ");
		header_append_encoded(header, ri->uri);
		if (ri->query_string) {
			header_append_sz(header, "?");
			header_append_sz(header, ri->query_string);
		}
		header_append_sz(header, " HTTP/1.1\r\n");

		for (i=0; i<ri->num_headers; i++) {
			header_append_parm(header, ri->http_headers[i].name, ri->http_headers[i].value);
		}
		header_append_sz(header, "\r\n");
		header->end_of_header = header->max;
	}
	
	/*
	 * I don't want to tell the far server that the data is being 
	 * proxied, so let's remove headers related to proxies.
	 */
	header_remove_value(header, "PROXY-CONNECTION:");

	/*
	 * Servers tend to GZIP the results, which makes it harder to read
	 * with a sniffer. Therefore, I'm un-gzipping them by removing
	 * this header in the requests that tell it to gzip.
	 */
	header_remove_value(header, "ACCEPT-ENCODING:");
	
	//DBGCON_UPDATE_URL(dbgcon, ri->request_method, ri->proxy_host, strtoul(ri->proxy_port,0,0), ri->uri, strlen(ri->uri));

	printf("%s %s%s%s\n", ri->request_method, ri->uri, ri->query_string?"?":"", ri->query_string?ri->query_string:"");

	/* 
	 * Lookup the host. This is using the standard "gethostbyname" APIs
	 * to perform a DNS lookup on the hostname.
	 */
	{
		struct hostent *h;

		//DBGCON_PRINTF(dbgcon, "calling: gethostbyname(%s)", server.hostname);

		h = gethostbyname(ri->proxy_host);
		if (h == NULL)
		{
			mg_header_printf(conn, "HTTP/1.1 404 Not Found\r\n"
							 "Server: hamster/2.0\r\n"
							 "Connection: close\r\n"
							 "Content-Type: text/plain\r\n"
							 "\r\n");
			mg_printf(conn, "%s: host not found\n", ri->proxy_host);
			//DBGCON_PRINTF(dbgcon, "ERR: gethostbyname(%s): failed %d", server.hostname, WSAGetLastError());
			goto cleanup;
		} 



		memset(&sin, 0, sizeof(sin));
		memcpy(&sin.sin_addr, h->h_addr_list[0], sizeof(int));
		
		/*DBGCON_PRINTF(dbgcon, "gethostbyname(%s) returned %d.%d.%d.%d", 
				server.hostname, 
				(ntohl(sin.sin_addr.s_addr)>>24)&0xFF,
				(ntohl(sin.sin_addr.s_addr)>>16)&0xFF,
				(ntohl(sin.sin_addr.s_addr)>>8)&0xFF,
				(ntohl(sin.sin_addr.s_addr)>>0)&0xFF
				);
				*/
	}

	/*
	 * Get Content-Length that we'll need to use to continue sending bytes
	 */
	content_length = 0;
	is_no_content_length = 1;
	{
		const char *val;
		val = mg_get_header(conn, "Content-Length");
		if (val != NULL) {
			content_length = strtoul(val,0,0);
			is_no_content_length = 0;
		}
	}

	/*
	 * Track which cookies the browser already knows about
	 */


	/*
	 * Replace the cookies
	 */
	{
		char *tmp_url = (char*)alloca(strlen(ri->uri)+1);
		char *cookies;
		const char *p;

		/* Grab the cookies that we need to had to this header */
		memcpy(tmp_url, ri->uri, strlen(ri->uri)+1);
		cookies = cookiedb_GET_COOKIE(myprox->m_instance, server.hostname, tmp_url);
		

		if (cookies) {
			if (cookies[0]) {

				/* Track the cookies that we added, minus the ones the browser
				 * already knows about. We are going to do a Set-Cookie: on
				 * these in the response header so that the browser knows about
				 * them. */
				cookiediff_remember_cookies(track_cookies, cookies, strlen(cookies));
				cookiediff_forget_cookies_from_header(track_cookies, header->buf);
				response_setcookies = cookiedb_GET_SETCOOKIE(myprox->m_instance, server.hostname, tmp_url);


				/* Remove any cookies from the request header that match
				 * the cookies we are going to add from our internal state.
				 * We leave non-matching cookies untouched.
				 */ 
				for (p=cdiff_first_cookie(cookies); *p; p=cdiff_next_cookie(p)) {
					cdiff_remove_cookie((char*)header->buf, &header->end_of_header, p);
				}
				header->max = header->end_of_header;

				/*header_remove_value(header, "COOKIE:");
				header_remove_value(header, "COOKIE:");
				header_remove_value(header, "COOKIE:");
				header_remove_value(header, "COOKIE:");
				header_remove_value(header, "COOKIE:");
				header_remove_value(header, "COOKIE:");
				header_remove_value(header, "COOKIE:");
				header_remove_value(header, "COOKIE:");
				*/
				header_add_value(header, "Cookie:", cookies);
			}
			cookiedb_free(cookies);
		}

		/*
		 * Remove "hamster" as a referer
		 */
		{
			const unsigned char *val;
			unsigned val_length;

			header_extract_value(header, "REFERER:", &val, &val_length);
			if (val != NULL && val_length > 0) {
				if (contains(val, val_length, "hamster")) {
					char *referer;
					header_remove_value(header, "REFERER:");

					referer = cookiedb_GET_REFERER(myprox->m_instance, server.hostname, tmp_url);
					if (referer) {
						if (referer[0]) {
							header_remove_value(header, "REFERER:");
							header_add_value(header, "Referer:", referer);
						}
						cookiedb_free(referer);
					}
				}
			}
		}

	}


	/*
	 * 
	 */
	{
		int r;

		sin.sin_family = AF_INET;
		sin.sin_port = htons((unsigned short)server.port);

		server.fd = socket(AF_INET, SOCK_STREAM, 0);
	
		/*DBGCON_PRINTF(dbgcon, "connect(\"%s\" [%d.%d.%d.%d : %d]) ...", 
				server.hostname, 
				(ntohl(sin.sin_addr.s_addr)>>24)&0xFF,
				(ntohl(sin.sin_addr.s_addr)>>16)&0xFF,
				(ntohl(sin.sin_addr.s_addr)>>8)&0xFF,
				(ntohl(sin.sin_addr.s_addr)>>0)&0xFF,
				ntohs(sin.sin_port));*/
		r = connect(server.fd, (struct sockaddr*)&sin, sizeof(sin));
		if (r != 0) {
			int err = WSAGetLastError();

			switch (err) {
			case WSAECONNREFUSED:
				fprintf(stderr, "connection refused\n");
				/*DBGCON_PRINTF(dbgcon, "ERR: connect(\"%s\" [%d.%d.%d.%d : %d]) CONNECTION REFUSED", 
						server.hostname, 
						(ntohl(sin.sin_addr.s_addr)>>24)&0xFF,
						(ntohl(sin.sin_addr.s_addr)>>16)&0xFF,
						(ntohl(sin.sin_addr.s_addr)>>8)&0xFF,
						(ntohl(sin.sin_addr.s_addr)>>0)&0xFF,
						ntohs(sin.sin_port));
						*/
				goto cleanup;
				break;
			default:
				fprintf(stderr, "connect err = %d\n", err);
				/*DBGCON_PRINTF(dbgcon, "ERR: connect(\"%s\" [%d.%d.%d.%d : %d]) ERRCODE=%d", 
						server.hostname, 
						(ntohl(sin.sin_addr.s_addr)>>24)&0xFF,
						(ntohl(sin.sin_addr.s_addr)>>16)&0xFF,
						(ntohl(sin.sin_addr.s_addr)>>8)&0xFF,
						(ntohl(sin.sin_addr.s_addr)>>0)&0xFF,
						ntohs(sin.sin_port),
						err);*/
				goto cleanup;
			}
		}

	}

	/*
	 * Send the header
	 */
	//DBGCON_PRINTF(dbgcon, "Sending headers to server...");
	if (send(server.fd, (const char*)header->buf, header->end_of_header, 0) != (int)header->end_of_header) {
		fprintf(stderr, "Error sending %d\n", WSAGetLastError());
		/*DBGCON_PRINTF(dbgcon, "ERR: send([%d.%d.%d.%d : %d]) headers to server ERRCODE=%d", 
				(ntohl(sin.sin_addr.s_addr)>>24)&0xFF,
				(ntohl(sin.sin_addr.s_addr)>>16)&0xFF,
				(ntohl(sin.sin_addr.s_addr)>>8)&0xFF,
				(ntohl(sin.sin_addr.s_addr)>>0)&0xFF,
				ntohs(sin.sin_port),
				WSAGetLastError());*/
 		goto cleanup;
	}
	//DBGCON_PRINTF(dbgcon, "Sent headers to server.");

	printf("%.*s", header->end_of_header, header->buf);


	/*
	 * Remove the header from the buffer.
	 */
	header_truncate(header);

	/*
	 * If there is a content-length AND remaining data in the buffer, send
	 * those bytes now.
	 */
	if (content_length && header->max) {
		unsigned len = content_length;
		if (len > header->max)
			len = header->max;

		if (send(server.fd, (const char*)header->buf, len, 0) != (int)len) {
			fprintf(stderr, "Error sending %d\n", WSAGetLastError());
			/*DBGCON_PRINTF(dbgcon, "ERR: send([%d.%d.%d.%d : %d]) data to server ERRCODE=%d", 
					(ntohl(sin.sin_addr.s_addr)>>24)&0xFF,
					(ntohl(sin.sin_addr.s_addr)>>16)&0xFF,
					(ntohl(sin.sin_addr.s_addr)>>8)&0xFF,
					(ntohl(sin.sin_addr.s_addr)>>0)&0xFF,
					ntohs(sin.sin_port),
					WSAGetLastError());*/
			goto cleanup;
		}

		memmove(header->buf, header->buf+len, header->max-len);
		header->max -= len;
		if (!is_no_content_length)
			content_length -= len;
	}
	if (header->max > 0) {
		fprintf(stderr, "request pipelining error\n");
		//DBGCON_PRINTF(dbgcon, "ERR: PIPELINED REQUEST FROM BROWSER RECEIVED");
		goto cleanup;
	}

	/*
	 * While there is more content length to send
	 */
	while (content_length) {
		char buf[1024];
		int bytes_read;
		int bytes_to_read;

		bytes_to_read = content_length;
		if (bytes_to_read > sizeof(buf))
			bytes_to_read = sizeof(buf);

		bytes_read = mg_read(conn ,buf, bytes_to_read);
		if (bytes_read == 0) {
			fprintf(stderr, "%s: read() from browser prematurely ended\n",
				"");
			content_length = 0;
		} else if (bytes_read < 0) {
			fprintf(stderr, "%s: read() from browser prematurely ended, err=%d\n",
				"",
				WSAGetLastError());
			content_length = 0;
		} else {
			if (send(server.fd, buf, bytes_read, 0) != (int)bytes_read) {
				fprintf(stderr, "%s: send() to server prematurely ended, err=%d\n",
					"",
					WSAGetLastError());
			}
		}

		content_length -= bytes_read;
	}

	/*
	 * Now receive everything from server
	 */
	//DBGCON_PRINTF(dbgcon, "All data received from browser, headers sent to server");
	handle_response2(myprox, conn, server, track_cookies, response_setcookies);

cleanup:
	if (a3a3a3a3 != 0xa3a3a3a3 || a3a3a3a1 != 0xa3a3a3a1)
		printf("corruption 1\n");
	//DBGCON_DELETE(dbgcon);
	if (server.fd > 0)
		closesocket(server.fd);
	if (header && header->buf)
		free(header->buf);
	if (header)
		free(header);
	if (a3a3a3a3 != 0xa3a3a3a3 || a3a3a3a1 != 0xa3a3a3a1)
		printf("corruption 2\n");
	if (track_cookies)
		cookiediff_destroy(track_cookies);
	if (response_setcookies)
		free(response_setcookies);
}
