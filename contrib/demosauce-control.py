#!/usr/bin/python
# a simple command linecontrol for demosauce

# ----------------------------------------------------------------------------
# "THE BEER-WARE LICENSE" (Revision 42):
# 'maep' on ircnet wrote this file. As long as you retain this notice you
# can do whatever you want with this stuff. If we meet some day, and you think
# this stuff is worth it, you can buy me a beer in return 
# ----------------------------------------------------------------------------

import socket, optparse

help_short= 'enter h for help'
help_msg = '''commands 
    s   skip currenly playing song
    m   update stream metadata
    p   set stream source
    h   print help
    q   quit this program'''

def prompt(msg = None):
    if msg:
        print msg
    return raw_input('>>> ' if msg else '] ')

def mainloop(fd):
    print help_short
    while True:
        cmd = prompt()
        if cmd == 'q':
            exit(0)
            
        elif cmd == 'h':
            print help_msg
            
        elif cmd == 's':
            fd.sendall('SKIP')
            print 'ok'
            
        elif cmd == 'm':
            artist = prompt('enter artist (optional)')
            title = prompt('enter title')
            if not title:
                print 'empty title, aborting'
                continue
            foo = 'META\ntitle=%s' % title
            if (artist):
                foo += '\nartist=%s' % artist
            fd.sendall(foo)
            print 'ok'
            
        elif cmd == 'p':
            url = prompt('enter url or path of next song to be played')
            if not url:
                print 'empty url, aborting'
                continue
            fd.sendall('PLAY\npath=%s' % url)
            print 'ok'
            
        else:
            print 'unknown command,', help_short
 
if __name__ == '__main__':
    usage = 'syntax: %prog [options]'
    parser = optparse.OptionParser(usage)
    parser.add_option('-p', '--port', dest='port', default='1911')
    (options, args) = parser.parse_args()

    try:
        fd = socket.create_connection(('localhost', options.port))
    except:
        print 'failed to connect to demosauce'
        exit(1)
    try:
        mainloop(fd)
    except:
        print 'lost connection to demosacue'
        exit(1)
