#!/bin/sh

RETVAL=0

JS="$(cat 1.js)"

. ./test_helper

test_proxy http://abcdomain.com abcdomain.com "Found proxy DIRECT"
test_proxy ftp://mydomain.com/x/ mydomain.com "Found proxy DIRECT"
test_proxy http://a.local/x/ a.local "Found proxy DIRECT"
test_proxy http://10.1.2.3/ 10.1.2.3 "Found proxy DIRECT"
test_proxy http://172.16.1.2/x/ 172.16.1.2 "Found proxy DIRECT"
test_proxy http://192.168.1.2/x/ 192.168.1.2 "Found proxy DIRECT"
test_proxy http://127.0.0.5/x/ 127.0.0.5 "Found proxy DIRECT"
test_proxy http://google.com/x google.com "Found proxy PROXY 4.5.6.7:8080; PROXY 7.8.9.10:8080"

exit $RETVAL
