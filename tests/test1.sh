#!/bin/sh

test_proxy() {
    _url="$1"
    _host="$2"
    _expected_proxy="$3"
    _js="$(cat 1.js)"
    _res=$(./test_pac "$_js" $_url $_host)
    if [ "$_res" != "$_expected_proxy" ]; then
        echo "FAILED:" $_res "!=" $_expected_proxy
        exit 1
    else
        echo "OK:" $_url $_host "->" $_res
    fi
}

test_proxy http://abcdomain.com abcdomain.com "Found proxy DIRECT"
test_proxy ftp://mydomain.com/x/ mydomain.com "Found proxy DIRECT"
test_proxy http://a.local/x/ a.local "Found proxy DIRECT"
test_proxy http://10.1.2.3/ 10.1.2.3 "Found proxy DIRECT"
test_proxy http://172.16.1.2/x/ 172.16.1.2 "Found proxy DIRECT"
test_proxy http://192.168.1.2/x/ 192.168.1.2 "Found proxy DIRECT"
test_proxy http://127.0.0.5/x/ 127.0.0.5 "Found proxy DIRECT"
test_proxy http://google.com/x google.com "Found proxy PROXY 4.5.6.7:8080; PROXY 7.8.9.10:8080"

exit 0
