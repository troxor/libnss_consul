libnss_consul
===========

A libnss resolver for Consul

The resolver will query the HTTP API of a Consul server to lookup hosts.
```
$ ping web.service.consul
PING web.service.consul (192.168.1.1) 56(84) bytes of data.
```

Installation
------------

This plugin requires json-c and libcurl.

```
$ make
# cp libnss_consul.so.2 /lib/libnss_consul.so.2
# ln -s /lib/libnss_consul.so.2 /lib/libnss_consul.so
```

Then, include "consul" in your /etc/nsswitch.conf files section:
```
...
hosts = files consul dns
...
```