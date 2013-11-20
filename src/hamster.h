#ifndef __MYPROX_H
#define __MYPROX_H

struct MyProx
{
	/* Critical Section */
	void *cs;

	int fd;

	unsigned server_port;
	unsigned server_ip;

	unsigned is_shutting_down;

	struct {
		char adaptername[256];
	} adapters[16];
	unsigned adapter_count;
	
	char m_instance[256];
};

struct FerretThreadParms {
	char adaptername[256];
	struct MyProx *myprox;
};

struct MyProx_ThreadParms
{
	int fd;
	struct MyProx *myprox;
	unsigned from_ip;
	unsigned from_port;
};



#ifndef P_IP_ADDR
#define P_IP_ADDR(ip) ((ip>>24)&0xFF), ((ip>>16)&0xFF), ((ip>>8)&0xFF), ((ip>>0)&0xFF)
#endif

static const unsigned MAX_HEADER_SIZE = 32768;

struct MYSOCK
{
	int fd;
	unsigned ip;
	unsigned port;
	char hostname[256];
};

struct HttpHeader
{
	unsigned char *buf;
	unsigned max;
	unsigned end_of_header;
};

void
cookiedb_read_file_handle(FILE *fp);

#endif /*__MYPROX_H*/
