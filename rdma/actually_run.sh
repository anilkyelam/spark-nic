#!/bin/bash

#the current script is really bad it will crash with seg faults if the server and client do not have the same number of qp
#i'm not sure why they were decoupled

#running the following will actually make it work
#be very careful the -so must be before the -o

#the -r option will also rebuild the source using the cmake and make system
qp=24
bash run.sh -r -so="-q$qp" -o="-q$qp -x"