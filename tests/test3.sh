#!/bin/sh

RETVAL=0

JS="$(cat 3.js)"

. ./test_helper

test_proxy http://foobar.example.com/x foobar.example.com "Found proxy DIRECT"
test_proxy http://10.0.0.10/x 10.0.0.10 "Found proxy PROXY fastproxy.example.com:8080"
test_proxy http://129.35.213.31/x 129.35.213.31 "Found proxy PROXY proxy.example.com:8080; DIRECT"

exit $RETVAL
