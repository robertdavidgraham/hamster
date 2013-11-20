/* Copyright (c) 2007 by Errata Security, All Rights Reserved
 * Programer(s): Robert David Graham [rdg]
 */
#ifndef __PLATFORM_H
#define __PLATFORM_H
#ifdef __cplusplus
extern "C" {
#endif

#include <stdio.h>

/* SOCKETS stuff*/
#ifdef WIN32
#pragma warning(disable: 4115 4616 4127)
#include <winsock.h>
#include <process.h> /* _beginthread */
#include <malloc.h>
#define snprintf _snprintf
#define socklen_t int
#ifndef UNUSEDPARM
#define UNUSEDPARM(x) x
#endif
#endif


#ifdef __GNUC__
#include <errno.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/time.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <alloca.h>

#define WSAGetLastError() (errno)
#define SOCKET_ERROR (-1)
#define closesocket(fd) close(fd)
#define WSAECONNREFUSED ECONNREFUSED
#ifndef UNUSEDPARM
#define UNUSEDPARM(x) x=(x)
#endif
#endif     

/*
 * THREADS
 */
#ifdef __GNUC__
#include <pthread.h>
typedef pthread_mutex_t CRITICAL_SECTION;
#define InitializeCriticalSection(p) pthread_mutex_init(p,0)
#define DeleteCriticalSection(p) pthread_mutex_destroy(p)
#define EnterCriticalSection(p) pthread_mutex_lock(p)
#define LeaveCriticalSection(p) pthread_mutex_unlock(p)
#endif
#ifdef WIN32
typedef int pthread_t;
#endif

/*
 * Platform tested for gcc v4 on a Fedora core system.
 * It should generally work for any Linux system using
 * the gcc compiler, even v3 and v2.
 */
#ifdef __GNUC__
#define __int64 long long
#define stricmp strcasecmp
#define strnicmp strncasecmp
#define sprintf_s snprintf

#ifndef UNUSEDPARM
#define UNUSEDPARM(x) x=(x)
#endif
#endif

/* 
 * Visual Studio 6 
 */
#if _MSC_VER==1200
#define sprintf_s _snprintf
#define alloca _alloca

#ifndef UNUSEDPARM
#define UNUSEDPARM(x) x
#endif

#define S_IFDIR _S_IFDIR

#endif

/* 
 * Visual Studio 2005 
 *
 * Supports both 32-bits and 64-bits.
 */
#if _MSC_VER==1400
#pragma warning(disable:4996)
#endif


#ifndef UNUSEDPARM
#define UNUSEDPARM(x)
#endif

#ifdef __cplusplus
}
#endif
#endif /*__PLATFORM_H*/
