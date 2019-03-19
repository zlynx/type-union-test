Originally written for a Stack Overflow answer, this code implements an
overengineered VAL type that can be a number, string or OBJECT in a vaguely
Javascript style.

https://stackoverflow.com/a/55212163/13422

On Linux, compile it with "make".

Run it as:
./type-union-test 1 one 2 two 3 three 1 overwrite

The current test version of main is set up to build an OBJECT of key, value
with int32 keys and string values from the command line.
