/*
 * NSS plugin for looking up by extra nameservers
 */

#include <errno.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <nss.h>
#include <netdb.h>
#include <arpa/inet.h>

#include <curl/curl.h>
#include <json-c/json.h>

#define DEBUG 0
#define CONSUL_URL_BASE "http://127.0.0.1:8500/v1/catalog/service/"

#define BUFFER_SIZE (256*1024) // 256kB
#define MAX_ENTRIES 16

#define ALIGN(a) (((a+sizeof(void*)-1)/sizeof(void*))*sizeof(void*))

//#define ALIGN(idx) do { \
//  if (idx % sizeof(void*)) \
//    idx += (sizeof(void*) - idx % sizeof(void*)); /* Align on 32 bit boundary */ \
//} while(0)

typedef struct {
	uint32_t address;
} ipv4_address_t;

typedef struct {
	uint8_t address[16];
} ipv6_address_t;

struct userdata {
	int count;
	int data_len;
	union {
		ipv4_address_t ipv4[MAX_ENTRIES];
		ipv6_address_t ipv6[MAX_ENTRIES];
		char *name[MAX_ENTRIES];
	} data;
};

struct curl_write_result {
	char *data;
	int pos;
};

static void pack_hostent( /* OUT */ struct hostent *result, char *buffer,
			 size_t buflen, const char *name, const void *addr)
{
	char *aliases, *r_addr, *addrlist;
	size_t l, idx;
/* we can't allocate any memory, the buffer is where we need to
 * * return things we want to use
 * *
 * * 1st, the hostname */
	l = strlen(name);
	result->h_name = buffer;
	memcpy(result->h_name, name, l);
	buffer[l] = '\0';
	int foo = l + 1;
	idx = ALIGN(foo);
/* 2nd, the empty aliases array */
	aliases = buffer + idx;
	*(char **) aliases = NULL;
	idx += sizeof(char *);
	result->h_aliases = (char **) aliases;
	result->h_addrtype = AF_INET;
	result->h_length = sizeof(struct in_addr);
/* 3rd, address */
	r_addr = buffer + idx;
	inet_pton(AF_INET, addr, r_addr);
	idx += ALIGN(result->h_length);
/* 4th, the addresses ptr array */
	addrlist = buffer + idx;
	((char **) addrlist)[0] = r_addr;
	((char **) addrlist)[1] = NULL;
	result->h_addr_list = (char **) addrlist;
}

static size_t curl_write(char *ptr, size_t size, size_t nmemb, void *userdata) {
	struct curl_write_result *result = (struct curl_write_result *)userdata;

	if (result->pos + size * nmemb >= BUFFER_SIZE - 1) {
		fprintf(stderr, "buffer fail\n");
		return 1;
	}

	memcpy(result->data + result->pos, ptr, size * nmemb);
	result->pos += size * nmemb;

	return size * nmemb;
}

static int ends_with(const char *name, const char* suffix) {
#if DEBUG
	printf( "@ %s:: name=%s suffix=%s\n", __FUNCTION__, name, suffix ) ;
#endif
	size_t ln, ls;
	//assert(name); // causes segfaults? FIXME
	//assert(suffix);

	if ( (ls = strlen(suffix)) > (ln = strlen(name)) )
		return 0;

	return strcasecmp(name+ln-ls,suffix) == 0;
}

/*
 * Given a service name, queries Consul API and returns numeric address
 */
static int curl_lookup(const char *name, char *addr) {

#if DEBUG
	printf( "@ %s::(in) name=%s\n", __FUNCTION__, name ) ;
#endif

	CURL *curl_handle = curl_easy_init();
	CURLcode res;

	char *curl_data;
	char *consul_url;
	size_t cu_len1, cu_len2;

	/* Create API URL given *name */
	curl_data = malloc(BUFFER_SIZE);
	cu_len1 = strlen(CONSUL_URL_BASE);
	cu_len2 = (strchr(name, '.')-name); //guaranteed by ends_with
	consul_url = malloc(cu_len1+cu_len2+1);
	memcpy(consul_url, CONSUL_URL_BASE, cu_len1);
	memcpy(consul_url+cu_len1, name, cu_len2);
#if DEBUG
	printf( "@ %s::(in) api_url=%s\n", __FUNCTION__, consul_url ) ;
#endif

	struct curl_write_result wr = {
		.data = curl_data,
		.pos = 0
	};

	curl_easy_setopt(curl_handle, CURLOPT_URL, consul_url);
	curl_easy_setopt(curl_handle, CURLOPT_WRITEFUNCTION, curl_write);
	curl_easy_setopt(curl_handle, CURLOPT_WRITEDATA, &wr);
	res = curl_easy_perform(curl_handle);
	curl_data[wr.pos] = '\0';

	// Parse out the address field only
	// For now it's lame and only grabs the first one until FIXME
	json_object *jservicearray = json_tokener_parse(curl_data);
	json_object *jservice;
	json_object *jaddr;
	const char *tempaddr;
	tempaddr = malloc(sizeof(ipv6_address_t));
	jaddr = malloc(BUFFER_SIZE); // this is dumb
	int i;
	for (i=0; i < json_object_array_length(jservicearray) ; i++) {
		jservice = json_object_array_get_idx(jservicearray, i);

		// We care about the address
		if (json_object_object_get_ex(jservice, "Address", &jaddr)) {
			tempaddr = json_object_get_string(jaddr);
			strcpy(addr, tempaddr);
		} else {
			return 1;
		}
#if DEBUG
		printf("@ %s::(out) addr=%s\n", __FUNCTION__, addr);
#endif
		return 0;
	}
#if DEBUG
	printf( "@ %s::(out) no 'Address' in json\n", __FUNCTION__ );
#endif
	return 1;
	
	free(curl_data);
	curl_easy_cleanup(curl_handle);
	curl_global_cleanup();
}


/*
 * Passes through to ..._gethostbyname2
 */
enum nss_status _nss_consul_gethostbyname_r(const char *name,
					    struct hostent *result,
					    char *buffer,
					    size_t buflen,
					    int *errnop,
					    int *h_errnop)
{
#if DEBUG
	printf( "@ %s\n", __FUNCTION__ ) ;
#endif

	return _nss_consul_gethostbyname2_r(name,
					    AF_INET,
					    result,
					    buffer,
					    buflen,
					    errnop,
					    h_errnop);
}

/*
 * Resolves the hostname into an IP address.
 * This function will be called multiple times by the GNU C library to get
 * get entire list of addresses.
 *
 * This function spec is defined at http://www.gnu.org/software/libc/manual/html_node/NSS-Module-Function-Internals.html
 */
enum nss_status _nss_consul_gethostbyname2_r(const char *name,
					     int af,
					     struct hostent *result,
					     char *buffer,
					     size_t buflen,
					     int *errnop,
					     int *h_errnop)
{
#if DEBUG
	printf( "@ %s:: name=%s af=%d\n", __FUNCTION__, name, af ) ;
#endif

	// error checking is for pansies
	if (af != AF_INET) {
		*errnop = EAGAIN;
		*h_errnop = NO_RECOVERY;
		return NSS_STATUS_TRYAGAIN;
	}
	// i am a pansy
	if (!ends_with(name, ".service.consul")) {
		*errnop = EINVAL;
		*h_errnop = NO_RECOVERY;
		return NSS_STATUS_UNAVAIL;
	}
	// ∴ error checking is for me (disabled)
/*	if (buflen < (sizeof(char*) +   strlen(name) + 1 +
		      sizeof(ipv4_address_t) +
		      sizeof(char*) + 
	 	      8 )) {
		*errnop = ERANGE;
		*h_errnop = NO_RECOVERY;
		return NSS_STATUS_TRYAGAIN;
	}*/

	char * ipaddr; 
	ipaddr = malloc(sizeof(ipv6_address_t));

	int rc = curl_lookup(name, ipaddr);
	if (rc) {
		*errnop = EAGAIN;
		*h_errnop = NO_RECOVERY;
		return NSS_STATUS_TRYAGAIN;
	}

	pack_hostent(result, buffer, buflen, name, ipaddr);

	return NSS_STATUS_SUCCESS;
}

/*
 * not implemented
 */
enum nss_status _nss_etcd_gethostbyaddr_r(const void *addr, socklen_t len,
					  int af,
					  struct hostent *result,
					  char *buffer, size_t buflen,
					  int *errnop,
					  int *h_errnop)
{
#if DEBUG
	printf( "@ %s\n", __FUNCTION__ ) ;
#endif
	return NSS_STATUS_UNAVAIL;
}
