#!/bin/bash

gcc server.c -o bin/server -Wall -lpthread
gcc client.c -o bin/client -Wall -lpthread
