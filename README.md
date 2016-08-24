### AIOTEST

This is a sample code of kernel asynchronous I/O support for Linux.
See src/aiotest.cc for details.

#### Required Software
* cmake

#### Build
1. `$ cd .; mkdir build; cd build;`
2. `$ cmake ..`

#### Sample
* Show usage.
```
$ ./aiotest -h
```

* Write 3000MB (100MB (-s) * 30 events (-e)) of data to a single file (-n) using asynchronous I/O(-a) with direct I/O mode (-d).
```
$ ./aiotest -a -d -s 100m -e 30 -n 1 -f test
```

* Write 3000MB (100MB (-s) * 30 events (-e)) of data to a single  file (-n) using pwrite.
```
$ ./aiotest -s 100m -e 30 -n 1 -f test
```

#### License
Copyright (c) 2016 Hitoshi Sato and Ryo Mizote

Permission is hereby granted, free of charge, to any person obtaining a copy of this software and associated documentation files (the "Software"), to deal in the Software without restriction, including without limitation the rights to use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of the Software, and to permit persons to whom the Software is furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
