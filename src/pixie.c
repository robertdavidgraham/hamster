/*
	Portable APIs modeled after Linux/Windows APIs
*/
#include "pixie.h"
#include <ctype.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <errno.h>
#include <stdarg.h>


#ifdef WIN32
#pragma warning(disable:4115)
#include <windows.h>
#include <process.h>
#include <rpc.h>
#include <rpcdce.h>
#include <fcntl.h>
#include <process.h>
#include <direct.h>
#include <io.h>
#pragma comment(lib,"rpcrt4.lib")
#else
#include <unistd.h>
#include <pthread.h>
#include <dlfcn.h>
#endif

#ifndef UNUSEDPARM
#define UNUSEDPARM(x)
#endif

/*===========================================================================
 * IPHLPAPI.H (IP helper API)
 *	This include file is not included by default with Microsoft's compilers,
 *	but requires a seperate download of their SDK. In order to make
 *	compiling easier, we are going to copy the definitions from that file
 *	directly into this file, so that the header file isn't required.
 *===========================================================================*/
#if defined(WIN32) && !defined(__IPHLPAPI_H__)
/* __IPHLPAPI_H__ is the mutual-exclusion identifier used in the
 * original Microsoft file. We are going to use the same identifier here
 * so that if the programmer chooses, they can simply include the 
 * original file up above, and these definitions will automatically be
 * excluded. */
#define MAX_ADAPTER_DESCRIPTION_LENGTH  128
#define MAX_ADAPTER_NAME_LENGTH         256
#define MAX_ADAPTER_ADDRESS_LENGTH      8
#define DEFAULT_MINIMUM_ENTITIES        32
#define MAX_HOSTNAME_LEN                128
#define MAX_DOMAIN_NAME_LEN             128
#define MAX_SCOPE_ID_LEN                256
typedef struct {
    char String[4 * 4];
} IP_ADDRESS_STRING, *PIP_ADDRESS_STRING, IP_MASK_STRING, *PIP_MASK_STRING;
typedef struct _IP_ADDR_STRING {
    struct _IP_ADDR_STRING* Next;
    IP_ADDRESS_STRING IpAddress;
    IP_MASK_STRING IpMask;
    DWORD Context;
} IP_ADDR_STRING, *PIP_ADDR_STRING;
typedef struct _IP_ADAPTER_INFO {
    struct _IP_ADAPTER_INFO* Next;
    DWORD ComboIndex;
    char AdapterName[MAX_ADAPTER_NAME_LENGTH + 4];
    char Description[MAX_ADAPTER_DESCRIPTION_LENGTH + 4];
    UINT AddressLength;
    BYTE Address[MAX_ADAPTER_ADDRESS_LENGTH];
    DWORD Index;
    UINT Type;
    UINT DhcpEnabled;
    PIP_ADDR_STRING CurrentIpAddress;
    IP_ADDR_STRING IpAddressList;
    IP_ADDR_STRING GatewayList;
    IP_ADDR_STRING DhcpServer;
    BOOL HaveWins;
    IP_ADDR_STRING PrimaryWinsServer;
    IP_ADDR_STRING SecondaryWinsServer;
    time_t LeaseObtained;
    time_t LeaseExpires;
} IP_ADAPTER_INFO, *PIP_ADAPTER_INFO;


typedef DWORD (WINAPI *GETADAPTERSINFO)(PIP_ADAPTER_INFO pAdapterInfo, PULONG pOutBufLen);
typedef DWORD (WINAPI *GETBESTINTERFACE)(DWORD ip_address, DWORD *r_interface_index);

DWORD WINAPI
GetAdaptersInfo(PIP_ADAPTER_INFO pAdapterInfo, PULONG pOutBufLen)
{
	static GETADAPTERSINFO xGetAdaptersInfo;

	if (xGetAdaptersInfo == 0) {
		void *h = pixie_load_library("iphlpapi.dll");
		if (h == NULL) {
			fprintf(stderr, "PIXIE: LoadLibrary(iphlpapi.dll) failed %d\n", GetLastError());
			return GetLastError(); 
		}
		xGetAdaptersInfo = (GETADAPTERSINFO)GetProcAddress(h, "GetAdaptersInfo");
		if (xGetAdaptersInfo == NULL) {
			fprintf(stderr, "PIXIE: GetProcAddress(iphlpapi.dll/%s) failed %d\n", "GetAdaptersInfo", GetLastError());
			return GetLastError();
		}
	}

	return xGetAdaptersInfo(pAdapterInfo, pOutBufLen);
}

DWORD WINAPI
GetBestInterface(DWORD  dwDestAddr, DWORD  *pdwBestIfIndex) 
{
	static GETBESTINTERFACE xGetBestInterface;
	if (xGetBestInterface == 0) {
		void *h = pixie_load_library("iphlpapi.dll");
		if (h == NULL) {
			fprintf(stderr, "PIXIE: LoadLibrary(iphlpapi.dll) failed %d\n", GetLastError());
			return GetLastError(); 
		}
		xGetBestInterface = (GETBESTINTERFACE)GetProcAddress(h, "GetBestInterface");
		if (xGetBestInterface == NULL) {
			fprintf(stderr, "PIXIE: GetProcAddress(iphlpapi.dll/%s) failed %d\n", "GetBestInterface", GetLastError());
			return GetLastError();
		}
	}

	return xGetBestInterface(dwDestAddr, pdwBestIfIndex);
}


#endif

/**
 * Load a dynamic link library. By loading this manually with code,
 * we can catch errors when the library doesn't exist on the system.
 * We can also go hunting for the library, or backoff and run without
 * that functionality. Otherwise, in the normal method, when the
 * operating system can't find the library, it simply refuses to run
 * our program
 */
void *pixie_load_library(const char *library_name)
{
#ifdef WIN32
	return LoadLibrary(library_name);
#else
	return dlopen(library_name,0);
#endif
}


/**
 * Retrieve a symbol from a library returned by "pixie_load_library()"
 */
PIXIE_FUNCTION pixie_get_proc_symbol(void *library, const char *symbol)
{
#ifdef WIN32
	return (PIXIE_FUNCTION)GetProcAddress(library, symbol);
#else
	/* ISO C doesn't allow us to cast a data pointer to a function
	 * pointer, therefore we have to cheat and use a union */
	union {
		void *data;
		PIXIE_FUNCTION func;
	} result;
	result.data = dlsym(library, symbol);
	return result.func;
#endif
}


/**
 * Retrieve the MAC address of the system
 */
unsigned pixie_get_mac_address(unsigned char macaddr[6])
{
	memset(macaddr, 0, sizeof(macaddr));
#ifdef WIN32
	{
		DWORD dwStatus;
		IP_ADAPTER_INFO *p;
		IP_ADAPTER_INFO AdapterInfo[16];
		DWORD dwBufLen = sizeof(AdapterInfo);
		DWORD interface_index = (DWORD)-1;

		GetBestInterface(0x01010101, &interface_index);
		
		dwStatus = GetAdaptersInfo(AdapterInfo, &dwBufLen);
		if (dwStatus != ERROR_SUCCESS)
			  return 1;

		for (p=AdapterInfo; p; p = p->Next) {

			if (p->Index == interface_index || interface_index == -1) {
				memcpy(macaddr, p->Address, 6);
				return 0;
			}
			/*(
			printf("[%02x:%02x:%02x:%02x:%02x:%02x]\n",
			mac_address[0], mac_address[1], mac_address[2], 
			mac_address[3], mac_address[4], mac_address[5]
			);
			printf("    %s\n", p->AdapterName);
			printf("    %s\n", p->Description);
			printf("    IP: ");
			for (a = &p->IpAddressList; a; a = a->Next) {
				printf("%s ", a->IpAddress.String);
			}
			printf("\n");
			*/
		}
	}
#else
#endif
	return (unsigned)-1;
}


/**
 * Retrieve the name of the system. 'name_size' is size of the buffer pointed
 * to by 'name'.
 * Returns the length of the name.
 */
unsigned pixie_get_host_name(char *name, unsigned name_size)
{
#ifdef WIN32
	/*
	BOOL WINAPI GetComputerName(
	  __out    LPTSTR lpBuffer,
	__inout  LPDWORD lpnSize
	);
	Return Value: If the function succeeds, the return value is a nonzero value.
	The variable 'lpnsize' must be set to the length of the number of
	bytes in the string, and it be set to the resulting length */
	if (GetComputerName(name, (DWORD*)&name_size))
		return name_size;
	else
		return 0;
#else
	/*
	int gethostname(char *name, size_t namelen)
	'namelen' is the size of the 'name' buffer.
	Returns 0 on success, -1 on failure
	*/
	if (gethostname(name, name_size) == 0) {
		/* If the buffer is too small, it might not nul terminate the
		 * string, so let's guarantee a nul-termination */
		name[name_size-1] = '\0';
		return name_size;
	} else
		return 0;
#endif
}



/* Set the thread to be low priority so it doesn't interfer with other
 * software as it attempts to use all CPU power on the computer. */
void pixie_lower_thread_priority()
{
#ifdef WIN32
	SetThreadPriority(GetCurrentThread(),THREAD_PRIORITY_BELOW_NORMAL);
	SetThreadPriorityBoost(GetCurrentThread(), 1);
#endif
}

void pixie_enter_critical_section(void *cs)
{
	/* check for null, allows users to compile without Multithreading 
	 * support */
	if (cs == NULL)
		return;
#ifdef _MT
#ifdef WIN32
	EnterCriticalSection(cs);
#endif
#else
	int ret;
	ret = pthread_mutex_lock(cs);
	if (ret != 0) {
		fprintf(stderr, "pthread_mutix_unlock(): err: %s\n", strerror(ret));
	}
#endif
}

void pixie_leave_critical_section(void *cs)
{

	/* check for null, allows users to compile without Multithreading 
	 * support */
	if (cs == NULL)
		return;

#ifdef _MT
#ifdef WIN32
	LeaveCriticalSection(cs);
#endif
#else
	int ret;
	ret = pthread_mutex_unlock(cs);
	if (ret != 0) {
		fprintf(stderr, "pthread_mutix_unlock(): err: %s\n", strerror(ret));
	}
#endif
}

void *pixie_initialize_critical_section()
{

#ifdef _MT
#ifdef WIN32
	CRITICAL_SECTION *cs = (CRITICAL_SECTION*)malloc(sizeof(*cs));
	memset(cs, 0, sizeof(*cs));
	InitializeCriticalSection(cs);
	return cs;
#endif
#else
	int ret;
	pthread_mutex_t *cs = (pthread_mutex_t*)malloc(sizeof(*cs));
	memset(cs, 0, sizeof(*cs));
	ret = pthread_mutex_init(cs,0);
	if (ret != 0) {
		fprintf(stderr, "pthread_mutix_init(): err: %s\n", strerror(ret));
	}
	return cs;
#endif
}

ptrdiff_t pixie_begin_thread(void (*worker_thread)(void*), unsigned flags, void *worker_data)
{
#ifdef WIN32
#ifdef _MT
	flags=flags;
	return _beginthread(worker_thread, 0, worker_data);
#else
	UNUSEDPARM(flags);
	return 0;
#endif /*!_MT*/
#else /* !_MT*/
	{
		pthread_t thread_id = 0;
		int retval;

		retval = pthread_create(&thread_id, 0, (void *(*)(void*))worker_thread, worker_data);
		if (retval != 0) {
			fprintf(stderr, "pthread_create() err: %s\n", strerror(retval));
		}

		return thread_id;
	}
#endif
}


void pixie_close_thread(ptrdiff_t thread_handle)
{
#ifdef _MT
#ifdef WIN32
	CloseHandle((HANDLE)thread_handle);
#endif
#endif
}


void pixie_end_thread()
{
#ifdef _MT
#ifdef WIN32
	_endthread();
#endif
#endif
}

void pixie_delete_critical_section(void *cs)
{
	if (cs == NULL)
		return;
	else {

#ifdef WIN32
#ifdef _MT
		if (cs) {
			DeleteCriticalSection(cs);
			free(cs);
		}
#endif
#else
		int ret;
		ret = pthread_mutex_destroy(cs);
		if (ret != 0) {
			fprintf(stderr, "pthread_mutex_destroy() err: %s\n", strerror(ret));
		}
#endif
	}
}

void pixie_sleep(unsigned milliseconds)
{
#ifdef WIN32
	Sleep(milliseconds);
#else
	usleep(milliseconds*1000);
#endif
}

int
pixie_pipe(int *fds)
{
#ifdef WIN32
	return (_pipe(fds, BUFSIZ, _O_BINARY));
#else
	return pipe(fds);
#endif
}



#ifdef WIN32
static void
fix_directory_separators(char *path)
{
	for (; *path != '\0'; path++) {
		if (*path == '\\')
			*path = '/';
		if (*path == '/')
			while (path[1] == '\\' || path[1] == '/')
				(void) memmove(path + 1,
				    path + 2, strlen(path + 2) + 1);
	}
}

int
pixie_spawn_process(const char *prog, const char *args, 
					int fd_stdin, int fd_stdout)
{
	HANDLE	me;
	char	cmdline[FILENAME_MAX];
	int	retval;
	STARTUPINFOA		si;
	PROCESS_INFORMATION	pi;

	(void) memset(&si, 0, sizeof(si));
	(void) memset(&pi, 0, sizeof(pi));

	/* XXX redirect CGI errors to the error log file */
	si.cb		= sizeof(si);
	si.dwFlags	= STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
	si.wShowWindow	= SW_HIDE;

	me = GetCurrentProcess();
	DuplicateHandle(me, (HANDLE) _get_osfhandle(fd_stdin), me,
	    &si.hStdInput, 0, TRUE, DUPLICATE_SAME_ACCESS);
	DuplicateHandle(me, (HANDLE) _get_osfhandle(fd_stdout), me,
	    &si.hStdOutput, 0, TRUE, DUPLICATE_SAME_ACCESS);

	/* Create a commandline */
	pixie_snprintf(cmdline, sizeof(cmdline), "%s %s", prog, args);


	fix_directory_separators(cmdline);

	if (CreateProcessA(NULL, cmdline, NULL, NULL, TRUE,
	    CREATE_NEW_PROCESS_GROUP, 0, 0, &si, &pi) == 0) {
		fprintf(stderr, "pixie_spawn_process: CreateProcess(%s): %d\n", cmdline, GetLastError());
		//cry("%s: CreateProcess(%s): %d", __func__, cmdline, ERRNO);
		retval = 0;
	} else {
		close(fd_stdin);
		close(fd_stdout);
		retval = TRUE;
	}

	CloseHandle(si.hStdOutput);
	CloseHandle(si.hStdInput);
	CloseHandle(pi.hThread);
	CloseHandle(pi.hProcess);

	return (retval);
}
#else
static FILE *error_log = NULL;
static void
cry(const char *fmt, ...)
{
	FILE	*fp;
	va_list	ap;

	fp = ((error_log == NULL) ? stderr : error_log);
	va_start(ap, fmt);
	(void) vfprintf(fp, fmt, ap);
	va_end(ap);

	fputc('\n', fp);
}

int
pixie_spawn_process(
		const char *prog, const char *args,
		int fd_stdin, int fd_stdout)
{
	int		ret;
	pid_t		pid;
	char *argarray[32];
	unsigned arg_count;

	argarray[0] = prog;
	for (arg_count=1; arg_count<15 && *args; arg_count++) {
		unsigned len;
		char *a;
		for (len=0; args[len] && !isspace(args[len]); len++)
			;
		a = malloc(len+1);
		memcpy(a, args, len);
		a[len] = '\0';
		argarray[arg_count] = a;
		args += len;
		while (*args && isspace(*args))
			args++;
	}
	argarray[arg_count] = 0;


	ret = 0;

	if ((pid = fork()) == -1) {
		/* Parent */
		ret = -1;
		fprintf(stderr, "fork(): %s", strerror(errno));
	} else if (pid == 0) {
		/* Child */
		if (dup2(fd_stdin, 0) == -1) {
			cry("dup2(stdin, %d): %s", fd_stdin, strerror(errno));
		} else if (dup2(fd_stdout, 1) == -1) {
			cry("dup2(stdout, %d): %s", fd_stdout, strerror(errno));
		} else {
			/* If error file is specified, send errors there */
			if (error_log != NULL)
				(void) dup2(fileno(error_log), 2);

			(void) close(fd_stdin);
			(void) close(fd_stdout);

			/* Execute CGI program */
			(void) execv(prog, argarray);
			cry("execle(%s): %s", prog, strerror(errno));
		}
		exit(-1);
	} else {
		/* Parent. Suspended until child does execle() */
		(void) close(fd_stdin);
		(void) close(fd_stdout);
		ret = 1;
	}

	return (ret);
}
#endif


int
pixie_spawn(const char *prog, const char *args, int *r_fd_stdin, int *r_fd_stdout)
{
	int fd_stdin[2];
	int fd_stdout[2];

	fd_stdin[0] = fd_stdin[1] = fd_stdout[0] = fd_stdout[1] = -1;

	if (pixie_pipe(fd_stdin) != 0 || pixie_pipe(fd_stdout) != 0) {
		fprintf(stderr, "Cannot create pipes %s\n", strerror(errno));
		return errno;
	}

	if (!pixie_spawn_process(prog, args, fd_stdin[0], fd_stdout[1])) {
		fprintf(stderr, "couldn't spanw process %s\n", prog);
		return -1;
	}	

	*r_fd_stdin = fd_stdin[1];
	*r_fd_stdout = fd_stdout[0];

	return 0;
}

int pixie_strcpy(char *lhs, unsigned lhs_size, const char *rhs)
{
	return pixie_snprintf(lhs, lhs_size, "%s", rhs);
}
