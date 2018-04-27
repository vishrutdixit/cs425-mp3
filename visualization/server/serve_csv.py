#!/usr/bin/env python

# Found on StackExchange (credit http://stackoverflow.com/users/703144/berto)
# simply provides .csv logs for the .html visualization pages to load at the default address of
# 	http://localhost:8000/<logfile>.csv

import SimpleHTTPServer

class MyHTTPRequestHandler(SimpleHTTPServer.SimpleHTTPRequestHandler):
    def end_headers(self):
        self.send_my_headers()

        SimpleHTTPServer.SimpleHTTPRequestHandler.end_headers(self)

    def send_my_headers(self):
        self.send_header("Access-Control-Allow-Origin", "*")


if __name__ == '__main__':
    SimpleHTTPServer.test(HandlerClass=MyHTTPRequestHandler)