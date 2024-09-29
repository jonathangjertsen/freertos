.PHONY: fmt
ROOT_DIR:=$(shell dirname $(realpath $(firstword $(MAKEFILE_LIST))))
fmt:
	clang-format -style=Google -i *.cpp include/*.hpp include/*.h

strip:
	g++ stripcmt.cpp -o strip.exe 