#! /bin/sh

echo "run multiplayer game test..."

mate-terminal --geometry=80x18 -e 'sh -c "./lab2server 4490; exec bash"'
mate-terminal --geometry=80x11 -e 'sh -c "./lab2client localhost 4490 a; exec bash"'
mate-terminal --geometry=80x11 -e 'sh -c "./lab2client localhost 4490 b; exec bash"'
mate-terminal --geometry=80x11 -e 'sh -c "./lab2client localhost 4490 c; exec bash"'

echo "done."


